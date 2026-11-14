#include "falcon/client.hpp"
#include "falcon/platform/dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#ifdef FALCON_REQUIRE_OWNERSHIP
#include "falcon/platform/ownership.hpp"
#endif

namespace {

// Captures and --background runs are scripted: they must never open a dialog over whatever the person
// is doing (a modal box would also hang the run). Their errors go to stderr and the exit code.
bool isAutomatedRun(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--capture") == 0 || std::strcmp(argv[i], "--background") == 0) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    // The exe is built for the GUI subsystem (no console window); this reconnects the log output
    // when it was launched from a terminal.
    falcon::attachParentConsole();

    try {
        falcon::ClientOptions options = falcon::parseOptions(argc, argv);
        if (options.showHelp) {
            std::fputs(falcon::usageText().c_str(), stdout);
            return 0;
        }
#ifdef FALCON_REQUIRE_OWNERSHIP
        // The client that is bundled in the launcher plays only for an account that owns Minecraft: Java Edition. The launcher hands it the account's Minecraft
        // access token and Mojang's servers are asked about it (see platform/ownership.hpp). The Falcon.exe of a development build has no such check.
        {
            const char* token = std::getenv("FALCON_MC_TOKEN");
            const falcon::OwnershipCheck check = falcon::verifyMinecraftOwnership(token != nullptr ? token : "", options.uuid.value_or(""), options.username.value_or(""));
            _putenv_s("FALCON_MC_TOKEN", "");  // (it is not needed again, and nothing this process starts should inherit it)
            if (!check.ok) throw std::runtime_error(check.message);
        }
#endif
        falcon::Client client(std::move(options));
        return client.run();
    } catch (const std::exception& e) {
        if (isAutomatedRun(argc, argv)) {
            std::fprintf(stderr, "[falcon] fatal: %s\n", e.what());
            std::fflush(stderr);
        } else {
            falcon::showErrorDialog("Falcon", e.what());
        }
        return 1;
    }
}
