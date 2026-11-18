#pragma once

#include "falcon/assets/resource_source.hpp"
#include "falcon/jvm/class_file.hpp"
#include "falcon/jvm/mod_content.hpp"
#include "falcon/jvm/vm.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon::jvm {

// A Mixin class of the mod, read and not applied: which game classes it patches, and what its injectors say.
struct MixinInfo {
    std::string mixinClass;
    std::vector<std::string> targets;
    std::vector<MixinHook> hooks;
};

// What running a mod's start-up code through the VM found out.
struct ModReport {
    std::string modId, name, loader;
    bool supported = false;           // a Fabric or Quilt mod, whose entry points were run
    std::string notRun;               // why not, when it was not
    std::vector<std::string> entrypoints;  // the ones that were run, "com.example.Mod (main)"
    std::vector<Registration> registrations;
    std::vector<std::string> events, keys, calls, log;
    std::vector<std::string> unsupported;  // library classes and methods it used that the VM does not have
    std::vector<MixinInfo> mixins;
    std::string error;                // an exception that ended an entry point, or the budget
    std::uint64_t instructions = 0;
    double milliseconds = 0.0;
};

struct RunOptions {
    std::uint64_t budget = 200'000'000;  // instructions for the start-up
    bool runClient = true;               // run the client entry points
};

// A mod's VM, kept after its start-up so that what it registered for can be run: Nothing is loaded into the client. The report says what the mod did and wanted; the client carries it out
// itself (and, tick by tick, runs the handlers the mod registered and does what they ask for).
class ModInstance {
public:
    explicit ModInstance(const std::filesystem::path& jar, const RunOptions& options = {});
    ~ModInstance();
    ModInstance(const ModInstance&) = delete;
    ModInstance& operator=(const ModInstance&) = delete;

    [[nodiscard]] const ModReport& report() const { return report_; }
    // One client tick: the mod's tick handlers run, each with a budget of its own (one that fails or runs out of it is switched off); what the mod said to the player is returned.
    [[nodiscard]] std::vector<std::string> tick();

    // What the mod added (items, blocks, keys), as facts.
    [[nodiscard]] const ModContent& content() const { return content_; }
    // What the client lends the mod: the player's place, the window's size, the keys. Set before each call that runs the mod's code.
    [[nodiscard]] HostState& host() { return vm_->host; }
    [[nodiscard]] bool running() const { return vm_ != nullptr; }
    // The mod's assets (the jar as a resource pack: assets/<namespace>/...) and the namespaces it has some for.
    [[nodiscard]] std::shared_ptr<const ResourceSource> resourcePack() const;
    [[nodiscard]] const std::vector<std::string>& namespaces() const { return namespaces_; }

    // The mod's HUD (HudRenderCallback, HudElementRegistry): its handlers run once, and what they drew is returned for the client to draw. A handler that fails or overruns is switched off.
    [[nodiscard]] std::vector<DrawCmd> renderHud();

    // A button a mod put on a screen (ScreenEvents.AFTER_INIT and Screens.getButtons(screen).add(Button.builder(...).bounds(...).build())).
    struct ScreenWidget {
        std::string label;
        int x = 0, y = 0, width = 150, height = 20;
        int id = 0;  // for pressWidget
    };
    // A screen was built for this window size: the mod's handlers for it run, and the buttons they added are returned. `screenClass` is the game's class of the screen
    // ("net/minecraft/client/gui/screens/TitleScreen").
    [[nodiscard]] std::vector<ScreenWidget> screenOpened(const std::string& screenClass, int width, int height);
    // The player pressed one of those buttons: the handler the mod gave it runs (what it says to the player is returned by the next tick()).
    void pressWidget(int id);

private:
    void collect();
    void switchOff(std::size_t handler, const std::string& why);

    std::filesystem::path jar_;
    std::vector<std::string> namespaces_;
    ModReport report_;
    ModContent content_;
    std::unique_ptr<Vm> vm_;
    std::vector<bool> off_;  // by handler: switched off
    Object* graphics_ = nullptr;
    Object* delta_ = nullptr;
    struct Pressable {
        Object* button = nullptr;
        Object* onPress = nullptr;
    };
    std::map<int, Pressable> widgets_;
    int nextWidget_ = 1;
    std::size_t gcAt_ = 50'000;
};

// Runs the entry points of a Fabric or Quilt mod (its `main`, then its `client`) in a VM of its own, and reads its Mixin classes.
[[nodiscard]] ModReport runMod(const std::filesystem::path& jar, const RunOptions& options = {});

// Every Fabric and Quilt mod in a folder, started (the optimization mods are left out: Falcon does not support them), and ticked together.
class ModHost {
public:
    explicit ModHost(const std::filesystem::path& modsDir);
    ~ModHost();
    struct Said {
        std::string mod, text;
    };
    [[nodiscard]] std::vector<Said> tick();
    [[nodiscard]] const std::vector<std::unique_ptr<ModInstance>>& mods() const { return mods_; }
    // The mods' HUDs, drawn in the order the mods were started.
    [[nodiscard]] std::vector<DrawCmd> renderHud();
    // What every mod ships to be looked up as resources, lowest priority first, and the namespaces they bring (for their language files).
    [[nodiscard]] std::vector<std::shared_ptr<const ResourceSource>> resourcePacks() const;
    [[nodiscard]] std::vector<std::string> namespaces() const;
    struct HostWidget {
        ModInstance* mod = nullptr;
        ModInstance::ScreenWidget widget;
    };
    [[nodiscard]] std::vector<HostWidget> screenOpened(const std::string& screenClass, int width, int height);
    // Lends every mod the same view of the client (position, window, text width, language), before they run.
    void setHost(const HostState& state);
    // A key a mod asked for (by its name, "key.examplemod.open") was pressed or let go: the mods' KeyMapping.consumeClick and isDown answer from this.
    void keyEvent(const std::string& keyName, bool pressed);

private:
    std::vector<std::unique_ptr<ModInstance>> mods_;
    std::map<std::string, int> clicks_;
    std::set<std::string> down_;
};

// The report in short lines for a screen or a terminal: what the mod adds, hooks, wants to patch, wrote, and where it failed.
[[nodiscard]] std::vector<std::string> summarize(const ModReport& report);

}  // namespace falcon::jvm
