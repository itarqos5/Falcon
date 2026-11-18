#include "game_session_internal.hpp"

#include "falcon/util/log.hpp"

#include <cstdlib>

// The mods of the mods folder, running in the VM (jvm/mod_runtime.hpp): the Fabric and Quilt ones are started when the world is ready (their entry points run once, in the VM, with nothing of
// them loaded into the client), and every game tick the handlers they registered for the client tick are run again; what a handler says to the player goes into the chat. What the mods
// registered is read from their report (the Mods screen shows it); carrying out more of it (items, blocks, keys) is for the client's own code to do.

namespace falcon {

void GameSession::tickMods() {
    static const bool disabled = std::getenv("FALCON_NO_MODS") != nullptr;  // (to play without them)
    if (disabled || remote_) return;
    if (!modsStarted_) {
        modsStarted_ = true;
        try {
            modHost_ = std::make_unique<jvm::ModHost>(services_.modsDir);
        } catch (...) {
            falcon::log::info("mods", "the mods could not be started");
            return;
        }
        for (const auto& mod : modHost_->mods()) {
            const jvm::ModReport& r = mod->report();
            falcon::log::info("mods", "%s: %zu entry points in %.1f ms, %zu registrations, %zu event handlers%s%s", r.modId.c_str(), r.entrypoints.size(), r.milliseconds, r.registrations.size(), r.events.size(),
                              r.error.empty() ? "" : ", stopped: ", r.error.c_str());
        }
    }
    if (!modHost_) return;
    for (const jvm::ModHost::Said& said : modHost_->tick()) addChatMessage("§7[" + said.mod + "]§r " + said.text);
}

}  // namespace falcon
