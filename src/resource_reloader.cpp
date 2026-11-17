#include "falcon/resource_reloader.hpp"
#include "falcon/util/log.hpp"

#include "falcon/game/edition_badge.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iterator>
#include <unordered_set>

namespace falcon {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kFontName = "default";
constexpr auto kSliceBudget = std::chrono::microseconds(2000);  // main-thread GL work per frame

double millisSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Runs the tasks on the calling thread plus a few helpers, each task exactly once.
void runParallel(std::vector<std::function<void()>>& tasks, unsigned maxThreads) {
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= tasks.size()) return;
            tasks[i]();
        }
    };
    std::vector<std::thread> helpers;
    for (unsigned t = 1; t < maxThreads && t < tasks.size(); ++t) helpers.emplace_back(work);
    work();
    for (std::thread& helper : helpers) helper.join();
}

}  // namespace

// One request's worth of loaded data. The background thread fills in the CPU-side parts; the main
// thread turns them into GL objects (the "staged" parts) and finally swaps them in.
struct ResourceReloader::Job {
    std::uint64_t id = 0;
    std::vector<std::string> ids;
    std::string languageCode;
    ResourceManager::PackList packs;
    std::unique_ptr<ResourceManager> next;  // the resource view this job was prepared against

    struct PreparedTexture {
        std::string key;
        std::optional<Image> image;
        std::optional<Texture2D> staged;
    };
    struct PreparedSprite {
        std::string key;
        std::optional<SpriteData> data;
        std::optional<Texture2D> staged;
    };
    std::vector<PreparedTexture> textures;
    std::vector<PreparedSprite> sprites;
    std::optional<Language> language;
    std::optional<FontData> fontData;
    std::optional<SoundRegistry> sounds;
    std::optional<SplashManager> splashes;
    std::array<std::optional<Image>, 6> faces;
    bool hasFaces = false;

    // main-thread staging state
    std::size_t textureAt = 0;
    std::size_t spriteAt = 0;
    std::optional<Font> font;
    CubeMapTexture cube;
    bool cubeAllocated = false;
    unsigned faceAt = 0;

    int resourceCount = 0;
    double prepareMs = 0.0;
    double workMs = 0.0;
    double worstSliceMs = 0.0;
    Clock::time_point requestedAt;
};

ResourceReloader::ResourceReloader(ReloadTargets targets)
    : targets_(targets), latestLanguage_(targets.languageCode), appliedLanguage_(targets.languageCode) {
    worker_ = std::thread([this] { workerLoop(); });
}

ResourceReloader::~ResourceReloader() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<const ResourceSource> ResourceReloader::sourceFor(const std::string& id) {
    if (const auto it = sources_.find(id); it != sources_.end()) return it->second;
    auto source = targets_.repository.openSource(id);
    if (source) sources_.emplace(id, source);  // reused next time, so an unchanged pack stays "the same"
    return source;
}

void ResourceReloader::request(const std::vector<std::string>& packIds, const std::string& languageCode) {
    ResourceManager::PackList packs;
    std::vector<std::string> ids;
    for (const std::string& id : packIds) {
        if (auto source = sourceFor(id)) {
            packs.push_back(std::move(source));
            ids.push_back(id);
        }
    }

    std::vector<const void*> identity;
    for (const auto& pack : packs) identity.push_back(pack.get());
    std::vector<const void*> live;
    for (const auto& pack : targets_.resources.packs()) live.push_back(pack.get());
    const std::string& wantedLanguage = languageCode.empty() ? std::string("en_us") : languageCode;
    if (identity == (busy() ? latestPacks_ : live) && wantedLanguage == (busy() ? latestLanguage_ : appliedLanguage_)) {
        return;  // that is already on its way, or already live
    }
    latestPacks_ = identity;
    latestLanguage_ = wantedLanguage;

    // Drop the sources of packs that are no longer selected (their zips get unmapped).
    std::unordered_set<std::string> keep(ids.begin(), ids.end());
    for (auto it = sources_.begin(); it != sources_.end();) {
        it = keep.contains(it->first) ? std::next(it) : sources_.erase(it);
    }

    Request request;
    request.id = ++nextId_;
    request.language = wantedLanguage;
    request.previousLanguage = appliedLanguage_;
    request.ids = std::move(ids);
    request.packs = std::move(packs);
    request.textureKeys = targets_.textures.keys();
    request.spriteKeys = targets_.sprites.keys();
    request.requestedAt = Clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latestId_ = request.id;
        pending_ = std::move(request);
    }
    wake_.notify_one();
}

void ResourceReloader::workerLoop() {
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
            if (stop_) return;
            request = std::move(*pending_);
            pending_.reset();
        }
        auto job = prepare(request);
        if (!job) continue;  // superseded while loading
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = std::move(job);
    }
}

std::unique_ptr<ResourceReloader::Job> ResourceReloader::prepare(const Request& request) {
    auto job = std::make_unique<Job>();
    job->id = request.id;
    job->ids = request.ids;
    job->languageCode = request.language;
    job->packs = request.packs;
    job->requestedAt = request.requestedAt;
    job->next = std::make_unique<ResourceManager>(targets_.resources.withPacks(request.packs));

    const ResourceManager& live = targets_.resources;
    const ResourceManager& next = *job->next;
    const auto changed = [&](const ResourceLocation& loc) { return live.providers(loc) != next.providers(loc); };
    const auto superseded = [&] { return latestId_.load() != request.id; };

    // Work out what changes, preparing a slot for each; the heavy ones go first.
    std::vector<std::function<void()>> tasks;
    const auto guarded = [&](std::function<void()> body) {
        return [&superseded, body = std::move(body)] {
            if (superseded()) return;
            try {
                body();
            } catch (const std::exception& e) {
                falcon::log::line("reload: %s\n", e.what());
            }
        };
    };

    // Panorama: if any face changes, all six are needed to build the new cube map.
    for (unsigned face = 0; face < 6; ++face) {
        if (changed(PanoramaRenderer::faceLocation(face))) job->hasFaces = true;
    }
    if (job->hasFaces) {
        for (unsigned face = 0; face < 6; ++face) {
            tasks.push_back(guarded([&, face] { job->faces[face] = PanoramaRenderer::decodeFace(next, face); }));
        }
    }

    // The font is cheap to rebuild and a pack may change its definition or any of its pictures.
    tasks.push_back(guarded([&] { job->fontData = Font::prepare(next, kFontName); }));

    // English is always read, and the selected language on top of it: reload when either file
    // comes from somewhere else now, or when another language was picked.
    const auto langLoc = [](const std::string& code) { return ResourceLocation("minecraft", "lang/" + code + ".json"); };
    if (request.language != request.previousLanguage || changed(langLoc("en_us")) || changed(langLoc(request.language))) {
        tasks.push_back(guarded([&] { job->language = Language::load(next, request.language); }));
    }
    if (changed(ResourceLocation("minecraft", "sounds.json"))) {
        tasks.push_back(guarded([&] { job->sounds = SoundRegistry::load(next); }));
    }
    if (changed(ResourceLocation("minecraft", "texts/splashes.txt"))) {
        tasks.push_back(guarded([&] { job->splashes = SplashManager(next); }));
    }

    // Textures and sprites that were loaded so far (anything loaded later comes from the new
    // packs by itself once they are live).
    job->textures.reserve(request.textureKeys.size());
    for (const std::string& key : request.textureKeys) job->textures.push_back({key, std::nullopt, std::nullopt});
    for (auto& texture : job->textures) {
        const bool badge = targets_.cppEditionBadge && texture.key == kEditionTexturePath;
        if (!changed(TextureManager::locationOf(texture.key))) continue;
        tasks.push_back(guarded([&texture, &next, badge] {
            if (badge) texture.image = buildCppEditionBadge(next);
            if (!texture.image) texture.image = TextureManager::decode(next, texture.key);
        }));
    }
    job->sprites.reserve(request.spriteKeys.size());
    for (const std::string& key : request.spriteKeys) job->sprites.push_back({key, std::nullopt, std::nullopt});
    for (auto& sprite : job->sprites) {
        if (!changed(SpriteManager::pngOf(sprite.key)) && !changed(SpriteManager::mcmetaOf(sprite.key))) continue;
        tasks.push_back(guarded([&sprite, &next] { sprite.data = SpriteManager::decode(next, sprite.key); }));
    }

    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    runParallel(tasks, std::min(4u, std::max(2u, cores / 2)));
    if (superseded()) return nullptr;

    for (const auto& texture : job->textures) job->resourceCount += texture.image ? 1 : 0;
    for (const auto& sprite : job->sprites) job->resourceCount += sprite.data ? 1 : 0;
    for (const auto& face : job->faces) job->resourceCount += face ? 1 : 0;
    job->resourceCount += (job->language ? 1 : 0) + (job->sounds ? 1 : 0) + (job->splashes ? 1 : 0);
    job->prepareMs = millisSince(request.requestedAt);
    return job;
}

bool ResourceReloader::update() {
    {
        std::unique_ptr<Job> fresh;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fresh = std::move(ready_);
        }
        if (fresh && fresh->id == latestId_.load()) active_ = std::move(fresh);  // a stale one is dropped
    }
    if (active_ && active_->id != latestId_.load()) active_.reset();  // replaced while it was staging
    if (!active_) return false;

    const auto sliceStart = Clock::now();
    const bool staged = stage(*active_);
    const double sliceMs = millisSince(sliceStart);
    active_->workMs += sliceMs;
    active_->worstSliceMs = std::max(active_->worstSliceMs, sliceMs);
    if (!staged) return false;

    const auto commitStart = Clock::now();
    commit(*active_);
    const double commitMs = millisSince(commitStart);
    stats_.resources = active_->resourceCount;
    stats_.prepareMs = active_->prepareMs;
    stats_.uploadMs = active_->workMs + commitMs;
    stats_.worstFrameMs = std::max(active_->worstSliceMs, commitMs);
    settledId_ = active_->id;
    falcon::log::line("resource packs applied: %d file(s) reloaded; background %.1f ms, "
                "main thread %.1f ms in total (longest single frame %.1f ms)\n",
                stats_.resources, stats_.prepareMs, stats_.uploadMs, stats_.worstFrameMs);
    active_.reset();
    return true;
}

bool ResourceReloader::stage(Job& job) {
    const auto deadline = Clock::now() + kSliceBudget;
    const auto outOfTime = [&] { return Clock::now() >= deadline; };

    while (job.textureAt < job.textures.size()) {
        auto& texture = job.textures[job.textureAt++];
        if (!texture.image) continue;
        texture.staged.emplace(*texture.image, FilterMode::Nearest);
        texture.image.reset();
        if (outOfTime()) return false;
    }
    while (job.spriteAt < job.sprites.size()) {
        auto& sprite = job.sprites[job.spriteAt++];
        if (!sprite.data) continue;
        sprite.staged.emplace(sprite.data->image, FilterMode::Nearest);
        sprite.data->image = Image{};
        if (outOfTime()) return false;
    }
    if (job.fontData && !job.font) {
        job.font.emplace(Font::fromData(std::move(*job.fontData)));
        job.fontData.reset();
        if (outOfTime()) return false;
    }
    if (job.hasFaces) {
        if (!job.cubeAllocated) {
            job.cube.allocate(FilterMode::Linear);
            job.cubeAllocated = true;
        }
        while (job.faceAt < 6) {  // a 1024x1024 face is a few milliseconds: at most one per frame
            auto& face = job.faces[job.faceAt++];
            if (!face) continue;
            job.cube.setFace(job.faceAt - 1, *face);
            face.reset();
            if (outOfTime()) return false;
        }
    }
    return true;
}

void ResourceReloader::commit(Job& job) {
    ResourceManager& live = targets_.resources;
    const ResourceManager& next = *job.next;

    // Things that were first used after the request was made were read from the old packs and
    // were not prepared; bring the ones the new packs change up to date right now (there are few).
    std::unordered_set<std::string> prepared;
    for (const auto& texture : job.textures) prepared.insert(texture.key);
    for (const std::string& key : targets_.textures.keys()) {
        if (prepared.contains(key) || live.providers(TextureManager::locationOf(key)) ==
                                          next.providers(TextureManager::locationOf(key))) {
            continue;
        }
        const bool badge = targets_.cppEditionBadge && key == kEditionTexturePath;
        std::optional<Image> image = badge ? buildCppEditionBadge(next) : std::nullopt;
        if (!image) image = TextureManager::decode(next, key);
        if (!image) continue;
        Texture2D fresh(*image, FilterMode::Nearest);
        targets_.textures.replace(key, fresh);
    }
    prepared.clear();
    for (const auto& sprite : job.sprites) prepared.insert(sprite.key);
    for (const std::string& key : targets_.sprites.keys()) {
        if (prepared.contains(key) ||
            (live.providers(SpriteManager::pngOf(key)) == next.providers(SpriteManager::pngOf(key)) &&
             live.providers(SpriteManager::mcmetaOf(key)) == next.providers(SpriteManager::mcmetaOf(key)))) {
            continue;
        }
        auto data = SpriteManager::decode(next, key);
        if (!data) continue;
        Texture2D fresh(data->image, FilterMode::Nearest);
        targets_.sprites.replace(key, fresh, data->scaling);
    }

    // Sounds compare the old and new packs, so they go before the switch.
    targets_.sounds.reloadResources(std::move(job.sounds), next);

    // The switch: everything below happens between two frames.
    live.setPacks(job.packs);
    for (auto& texture : job.textures) {
        if (texture.staged) targets_.textures.replace(texture.key, *texture.staged);
    }
    for (auto& sprite : job.sprites) {
        if (sprite.staged && sprite.data) targets_.sprites.replace(sprite.key, *sprite.staged, sprite.data->scaling);
    }
    if (job.font) targets_.font = std::move(*job.font);
    if (job.language) {
        targets_.language = std::move(*job.language);
        appliedLanguage_ = job.languageCode;
    }
    if (job.splashes) targets_.splashes = std::move(*job.splashes);
    if (job.hasFaces && job.cubeAllocated) targets_.panorama.replaceCubeMap(job.cube);
}

}  // namespace falcon
