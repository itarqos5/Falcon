#pragma once

#include "falcon/jvm/class_file.hpp"
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

private:
    ModReport report_;
    std::unique_ptr<Vm> vm_;
    Object* client_ = nullptr;
    std::vector<bool> off_;  // by handler: switched off
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

private:
    std::vector<std::unique_ptr<ModInstance>> mods_;
};

// The report in short lines for a screen or a terminal: what the mod adds, hooks, wants to patch, wrote, and where it failed.
[[nodiscard]] std::vector<std::string> summarize(const ModReport& report);

}  // namespace falcon::jvm
