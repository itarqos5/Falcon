#include "falcon/net/skin_fetcher.hpp"

#include "falcon/net/session_auth.hpp"
#include "falcon/platform/http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace falcon::net {

namespace {

bool isHex(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string withoutDashes(std::string id) {
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    return id;
}

using Pixel = std::uint8_t;

Pixel* pixelAt(Image& image, int x, int y) { return &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4]; }

// NativeImage.copyRect: a rectangle at (x, y) of width x height copied to (x + dx, y + dy), mirrored left to right.
void copyRectMirrored(Image& image, int x, int y, int dx, int dy, int width, int height) {
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            const Pixel* from = pixelAt(image, x + j, y + i);
            Pixel* to = pixelAt(image, x + dx + (width - 1 - j), y + dy + i);
            std::copy(from, from + 4, to);
        }
    }
}

void setNoAlpha(Image& image, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) pixelAt(image, x, y)[3] = 255;
    }
}

// HttpTexture.doNotchTransparencyHack: an old skin whose hat area has no transparency at all would show a solid box, so the area is made see-through.
void notchTransparencyHack(Image& image, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (pixelAt(image, x, y)[3] < 128) return;  // it has transparency: it was made for this
        }
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) pixelAt(image, x, y)[3] = 0;
    }
}

}  // namespace

std::optional<SkinInfo> parseTexturesProperty(const std::string& base64Value) {
    const Bytes decoded = base64Decode(base64Value);
    if (decoded.empty()) return std::nullopt;
    const nlohmann::json j = nlohmann::json::parse(std::string(decoded.begin(), decoded.end()), nullptr, false);
    if (!j.is_object() || !j.contains("textures") || !j["textures"].is_object()) return std::nullopt;
    const nlohmann::json& textures = j["textures"];
    if (!textures.contains("SKIN") || !textures["SKIN"].is_object()) return std::nullopt;
    const nlohmann::json& skin = textures["SKIN"];
    if (!skin.contains("url") || !skin["url"].is_string()) return std::nullopt;
    const std::string url = skin["url"].get<std::string>();
    for (const char* prefix : {"http://textures.minecraft.net/texture/", "https://textures.minecraft.net/texture/"}) {
        const std::string p = prefix;
        if (url.compare(0, p.size(), p) != 0) continue;
        SkinInfo info;
        info.hash = url.substr(p.size());
        if (!isHex(info.hash) || info.hash.size() > 128) return std::nullopt;
        if (skin.contains("metadata") && skin["metadata"].is_object() && skin["metadata"].contains("model") && skin["metadata"]["model"].is_string()) {
            info.slim = skin["metadata"]["model"].get<std::string>() == "slim";
        }
        return info;
    }
    return std::nullopt;
}

Image prepareSkin(Image decoded) {
    if (decoded.width != 64 || (decoded.height != 32 && decoded.height != 64)) return {};
    const bool legacy = decoded.height == 32;
    Image image = std::move(decoded);
    if (legacy) {  // the lower half is made from the upper one: the right limbs seen in a mirror are the left ones
        image.height = 64;
        image.rgba.resize(64u * 64u * 4u, 0);
        copyRectMirrored(image, 4, 16, 16, 32, 4, 4);
        copyRectMirrored(image, 8, 16, 16, 32, 4, 4);
        copyRectMirrored(image, 0, 20, 24, 32, 4, 12);
        copyRectMirrored(image, 4, 20, 16, 32, 4, 12);
        copyRectMirrored(image, 8, 20, 8, 32, 4, 12);
        copyRectMirrored(image, 12, 20, 16, 32, 4, 12);
        copyRectMirrored(image, 44, 16, -8, 32, 4, 4);
        copyRectMirrored(image, 48, 16, -8, 32, 4, 4);
        copyRectMirrored(image, 40, 20, 0, 32, 4, 12);
        copyRectMirrored(image, 44, 20, -8, 32, 4, 12);
        copyRectMirrored(image, 48, 20, -16, 32, 4, 12);
        copyRectMirrored(image, 52, 20, -8, 32, 4, 12);
    }
    setNoAlpha(image, 0, 0, 32, 16);
    if (legacy) notchTransparencyHack(image, 32, 0, 64, 32);
    setNoAlpha(image, 0, 16, 64, 32);
    setNoAlpha(image, 16, 48, 48, 64);
    return image;
}

struct SkinFetcher::State {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    std::set<std::string> requested;       // hashes asked for
    std::set<std::string> profilesAsked;   // account ids asked for
    std::vector<Done> finished;
    std::map<std::string, SkinInfo> profiles;
    bool stop = false;
    bool running = false;

    void post(std::function<void()> job) {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push_back(std::move(job));
        wake.notify_one();
    }
};

SkinFetcher::SkinFetcher() : state_(std::make_shared<State>()) {
    // One thread takes the jobs one after the other; it is let go of (not waited for) when the fetcher goes, so closing the game never waits for a download.
    std::thread([state = state_] {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait(lock, [&] { return state->stop || !state->jobs.empty(); });
                if (state->stop) return;
                job = std::move(state->jobs.front());
                state->jobs.pop_front();
            }
            job();
        }
    }).detach();
}

SkinFetcher::~SkinFetcher() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->stop = true;
    state_->wake.notify_all();
}

void SkinFetcher::request(const SkinInfo& info) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->requested.insert(info.hash).second || state_->requested.size() > 512) return;
    }
    state_->post([state = state_, info] { fetchPicture(state, info); });
}

void SkinFetcher::fetchPicture(const std::shared_ptr<State>& state, const SkinInfo& info) {
    const HttpAnswer answer = httpsRequest("GET", "textures.minecraft.net", "/texture/" + info.hash);
    if (answer.status != 200) return;
    const std::vector<unsigned char> bytes(answer.body.begin(), answer.body.end());
    auto decoded = decodePng(bytes);
    if (!decoded) return;
    Image skin = prepareSkin(std::move(*decoded));
    if (skin.empty()) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->finished.push_back({info.hash, info.slim, std::move(skin)});
}

void SkinFetcher::requestProfile(const std::string& id) {
    const std::string key = withoutDashes(id);
    if (!isHex(key)) return;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->profilesAsked.insert(key).second) return;
    }
    state_->post([state = state_, key] {
        const HttpAnswer answer = httpsRequest("GET", "sessionserver.mojang.com", "/session/minecraft/profile/" + key);
        if (answer.status != 200) return;
        const nlohmann::json j = nlohmann::json::parse(answer.body, nullptr, false);
        if (!j.is_object() || !j.contains("properties") || !j["properties"].is_array()) return;
        for (const nlohmann::json& p : j["properties"]) {
            if (!p.is_object() || !p.contains("name") || !p["name"].is_string() || p["name"].get<std::string>() != "textures" || !p.contains("value") || !p["value"].is_string()) continue;
            if (const auto info = parseTexturesProperty(p["value"].get<std::string>())) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->profiles[key] = *info;
                    if (!state->requested.insert(info->hash).second) return;
                }
                fetchPicture(state, *info);
                return;
            }
        }
    });
}

std::vector<SkinFetcher::Done> SkinFetcher::takeFinished() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<Done> out = std::move(state_->finished);
    state_->finished.clear();
    return out;
}

std::optional<SkinInfo> SkinFetcher::profileSkin(const std::string& id) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto it = state_->profiles.find(withoutDashes(id));
    if (it == state_->profiles.end()) return std::nullopt;
    return it->second;
}

}  // namespace falcon::net
