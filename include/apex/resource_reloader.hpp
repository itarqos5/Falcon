#pragma once

#include "apex/assets/resource_manager.hpp"
#include "apex/audio/sound_system.hpp"
#include "apex/gfx/panorama_renderer.hpp"
#include "apex/gfx/texture_manager.hpp"
#include "apex/packs/pack_repository.hpp"
#include "apex/text/font.hpp"
#include "apex/text/language.hpp"
#include "apex/game/splash_manager.hpp"
#include "apex/ui/sprite_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace apex {

// Everything that holds loaded resources, i.e. everything a resource pack can change.
struct ReloadTargets {
    ResourceManager& resources;
    const PackRepository& repository;
    Language& language;
    Font& font;
    TextureManager& textures;
    SpriteManager& sprites;
    PanoramaRenderer& panorama;
    SplashManager& splashes;
    SoundSystem& sounds;
    bool cppEditionBadge = true;  // keep regenerating the "C++ EDITION" title badge
    std::string languageCode = "en_us";  // the language that is loaded now
};

// Applies a new selection of resource packs to the running client without ever stalling it.
//
//  1. request() opens the packs (zips are memory-mapped, so that is instant) and hands the work to
//     a background thread.
//  2. The background thread works out which files the new pack stack actually changes (a file that
//     comes from the same place as before is left alone) and reads, decodes and parses only those,
//     spread over a few threads. Nothing the game is drawing with is touched.
//  3. update(), called once per frame on the thread that owns the GL context, uploads the decoded
//     pictures in slices of about two milliseconds, then swaps the new resources in all at once,
//     between two frames. Until that moment the old resources keep working, and afterwards every
//     existing reference to a texture or sprite shows the new picture.
//
// A request made while another is still loading replaces it.
class ResourceReloader {
public:
    struct Stats {
        int resources = 0;          // files that were reloaded (textures, sprites, faces, ...)
        double prepareMs = 0.0;     // background thread, from request to ready
        double uploadMs = 0.0;      // main thread: all staging slices and the swap together
        double worstFrameMs = 0.0;  // longest single stretch of main-thread work
    };

    explicit ResourceReloader(ReloadTargets targets);
    ~ResourceReloader();
    ResourceReloader(const ResourceReloader&) = delete;
    ResourceReloader& operator=(const ResourceReloader&) = delete;

    // Main thread. `packIds` are as GameOptions::resourcePacks stores them, lowest priority first;
    // "vanilla" and packs that cannot be opened are skipped. `languageCode` is the selected
    // language ("de_de"). Does nothing if that is already live.
    void request(const std::vector<std::string>& packIds, const std::string& languageCode);

    // Main thread, once per frame before drawing. True in the frame in which the new resources
    // went live, so the caller can rebuild screens and release the memory the old ones used.
    [[nodiscard]] bool update();

    // A request is still being loaded or swapped in.
    [[nodiscard]] bool busy() const { return latestId_.load() != settledId_; }
    [[nodiscard]] const Stats& lastStats() const { return stats_; }

private:
    struct Job;
    struct Request {
        std::uint64_t id = 0;
        std::vector<std::string> ids;
        std::string language;          // the language to have loaded afterwards
        std::string previousLanguage;  // the one loaded when this was asked for
        ResourceManager::PackList packs;
        std::vector<std::string> textureKeys;
        std::vector<std::string> spriteKeys;
        std::chrono::steady_clock::time_point requestedAt;
    };

    [[nodiscard]] std::shared_ptr<const ResourceSource> sourceFor(const std::string& id);
    void workerLoop();
    [[nodiscard]] std::unique_ptr<Job> prepare(const Request& request);
    [[nodiscard]] bool stage(Job& job);   // budgeted GL uploads; true when everything is staged
    void commit(Job& job);

    ReloadTargets targets_;

    std::unordered_map<std::string, std::shared_ptr<const ResourceSource>> sources_;  // main thread
    std::vector<const void*> latestPacks_;  // pack identities of the newest request (main thread)
    std::string latestLanguage_;            // its language
    std::string appliedLanguage_;           // the language that is live

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> pending_;  // newest request the worker has not started
    std::unique_ptr<Job> ready_;      // finished by the worker, waiting for the main thread
    bool stop_ = false;
    std::thread worker_;

    std::atomic<std::uint64_t> latestId_{0};
    std::uint64_t nextId_ = 0;
    std::uint64_t settledId_ = 0;  // main thread
    std::unique_ptr<Job> active_;  // being staged on the main thread
    Stats stats_;
};

}  // namespace apex
