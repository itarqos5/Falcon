#include "falcon/client.hpp"
#include "falcon/platform/dialog.hpp"
#include "falcon/util/log.hpp"
#include "falcon/version.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
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

// What the program was started with, for the log's header: the values that name the account are left out.
std::string describeArguments(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!out.empty()) out += ' ';
        out += argv[i];
        if ((std::strcmp(argv[i], "--username") == 0 || std::strcmp(argv[i], "--uuid") == 0) && i + 1 < argc) {
            out += " <hidden>";
            ++i;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    // The exe is built for the GUI subsystem (no console window); this reconnects the log output
    // when it was launched from a terminal.
    falcon::attachParentConsole();
    // The log is read while the game runs (the launcher's log, a script waiting for "spawn area loaded"): a line goes out when it is written, also into a file or a pipe.
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);

    // The client's own log (logs/client.log in Falcon's folder) and the crash report. A scripted run (a capture, a background run) does not fill the person's folder unless it is
    // told where to write (FALCON_LOG_DIR).
    const bool automated = isAutomatedRun(argc, argv);
    {
        falcon::log::Settings settings;
        settings.version = "Falcon " FALCON_VERSION_STRING " (Minecraft Java 26.3)";
        settings.commandLine = describeArguments(argc, argv);
        bool help = false;  // (a --help run must not push the log of the last real run out of the way)
        for (int i = 1; i < argc; ++i) help = help || std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0;
        settings.toFile = !automated && !help;
        falcon::log::init(settings);
        falcon::log::installCrashHandler([automated](const std::filesystem::path& report) {
            if (!automated) falcon::showErrorDialog("Falcon has crashed", "Falcon stopped because of an error. What happened and where is written to:\n\n" + report.string());
        });
    }
    struct LogEnd {  // (the last line of the log says the run ended cleanly)
        int code = 0;
        ~LogEnd() { falcon::log::shutdown(code); }
    } logEnd;

    try {
        falcon::ClientOptions options = falcon::parseOptions(argc, argv);
        if (options.showHelp) {
            std::fputs(falcon::usageText().c_str(), stdout);
            return 0;
        }
        {  // The launcher hands over the account's access token in the environment: it is kept (for a server in online mode, which asks Mojang who joins) and wiped from there,
           // so that nothing this process starts inherits it.
            char* token = std::getenv("FALCON_MC_TOKEN");
            options.accessToken = token != nullptr ? token : "";
#ifdef _WIN32
            _putenv_s("FALCON_MC_TOKEN", "");
#else
            if (token != nullptr) std::memset(token, 0, std::strlen(token));  // (/proc/<pid>/environ shows this very memory, which unsetenv alone leaves as it is)
            unsetenv("FALCON_MC_TOKEN");
#endif
        }
#ifdef FALCON_REQUIRE_OWNERSHIP
        // The client that is bundled in the launcher plays only for an account that owns Minecraft: Java Edition. The launcher hands it the account's Minecraft
        // access token and Mojang's servers are asked about it (see platform/ownership.hpp). The Falcon.exe of a development build has no such check.
        {
            const falcon::OwnershipCheck check = falcon::verifyMinecraftOwnership(options.accessToken, options.uuid.value_or(""), options.username.value_or(""));
            if (!check.ok) throw std::runtime_error(check.message);
        }
#endif
        falcon::Client client(std::move(options));
        logEnd.code = client.run();
        return logEnd.code;
    } catch (const std::exception& e) {
        // A fatal error the program caught itself (no assets, no OpenGL ...): a report with the stack and what the game was doing, then the person is told.
        falcon::log::writeCrashReport(std::string("fatal error: ") + falcon::log::typeName(typeid(e)), e.what());
        if (automated) {
            std::fprintf(stderr, "[falcon] fatal: %s\n", e.what());
            std::fflush(stderr);
        } else {
            falcon::showErrorDialog("Falcon", e.what());
        }
        logEnd.code = 1;
        return 1;
    }
}
