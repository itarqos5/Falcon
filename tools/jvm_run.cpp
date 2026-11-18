// Runs the start-up code of Fabric and Quilt mods through the VM (jvm/vm.hpp) and says what they did: falcon_jvm_run <mod.jar> [more.jar ...] [--verbose]
#include "falcon/jvm/mod_runtime.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    bool verbose = false;
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            continue;
        }
        const falcon::jvm::ModReport r = falcon::jvm::runMod(argv[i]);
        std::printf("== %s (%s %s): %.1f ms, %llu instructions\n", r.name.empty() ? argv[i] : r.name.c_str(), r.loader.c_str(), r.modId.c_str(), r.milliseconds, static_cast<unsigned long long>(r.instructions));
        for (const std::string& line : falcon::jvm::summarize(r)) std::printf("   %s\n", line.c_str());
        if (verbose) {
            for (const auto& reg : r.registrations) std::printf("   register %s %s: %s\n", reg.registry.c_str(), reg.id.c_str(), reg.text.c_str());
            for (const std::string& e : r.events) std::printf("   event %s\n", e.c_str());
            for (const std::string& k : r.keys) std::printf("   key %s\n", k.c_str());
            for (const std::string& c : r.calls) std::printf("   call %s\n", c.c_str());
            for (const std::string& u : r.unsupported) std::printf("   unsupported %s\n", u.c_str());
            for (const auto& m : r.mixins) {
                std::printf("   mixin %s ->", m.mixinClass.c_str());
                for (const std::string& t : m.targets) std::printf(" %s", t.c_str());
                std::printf("\n");
                for (const auto& h : m.hooks) {
                    std::printf("      %s %s:", h.kind.c_str(), h.handler.c_str());
                    for (const std::string& t : h.targets) std::printf(" %s", t.c_str());
                    std::printf("\n");
                }
            }
        }
        if (!r.error.empty()) status = 1;
    }
    return status;
}
