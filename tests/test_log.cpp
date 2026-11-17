// The client's log (util/log.hpp): latest.log while it runs, the levels and categories of the old console lines, the run before kept under its start time (and marked when it did not end
// cleanly), the crash report, and the end of the log: latest.log takes the time the run was started as its name.
#include "falcon/util/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (false)

std::string slurp(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

int main() {
    namespace log = falcon::log;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "falcon_log_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
#ifdef _WIN32
    _putenv_s("FALCON_LOG_DIR", dir.string().c_str());
#else
    setenv("FALCON_LOG_DIR", dir.c_str(), 1);
#endif
    // The run before this one: a latest.log that stopped without its last line (it was killed), and a crash report from about then.
    {
        std::ofstream((dir / "latest.log").string(), std::ios::binary) << "2026-01-01 10:00:00.000 INFO  [client] it was running\n";
        std::ofstream((dir / "crash-2026-01-01_10-00-01.log").string(), std::ios::binary) << "Falcon crash report\n";
    }

    log::Settings settings;
    settings.version = "Falcon test";
    settings.echo = false;
    CHECK(log::init(settings));
    CHECK(log::directory() == dir);
    CHECK(log::currentFile() == dir / "latest.log");

    // The run before is kept under the time it started (its first line says: 10:00:00), marked, and the new log says so (and where its crash report is).
    CHECK(std::filesystem::exists(dir / "2026-01-01_10-00-00-unclean.log"));

    // The old console lines: a level and a category from what they say.
    log::line("audio: muted\n");
    log::line("could not save the options\n");
    log::line("FAILED to save a frame\n");
    log::line("spawn found: 1.0 2.0 3.0\n");
    log::info("render", "%d fps, %zu chunks", 60, static_cast<std::size_t>(12));
    log::debug("render", "a debug line (not written unless FALCON_LOG_LEVEL=debug)");
    log::addContext("world", [] { return std::string("the test world"); });

    const std::string live = slurp(log::currentFile());
    CHECK(has(live, "INFO  [audio] muted"));
    CHECK(has(live, "WARN  [save] could not save the options"));
    CHECK(has(live, "ERROR [capture] FAILED to save a frame"));
    CHECK(has(live, "INFO  [world] spawn found: 1.0 2.0 3.0"));
    CHECK(has(live, "INFO  [render] 60 fps, 12 chunks"));
    CHECK(!has(live, "a debug line"));
    CHECK(has(live, "the run before this one did not end cleanly"));
    CHECK(has(live, "crash-2026-01-01_10-00-01.log"));
    CHECK(!log::recentLines(3).empty());

    // A crash report: what, the stack, what the game was doing, the last lines.
    const std::filesystem::path report = log::writeCrashReport("fatal error: std::runtime_error", "the assets are gone");
    CHECK(!report.empty() && std::filesystem::exists(report));
    const std::string text = slurp(report);
    CHECK(has(text, "Falcon crash report"));
    CHECK(has(text, "What:     fatal error: std::runtime_error: the assets are gone"));
    CHECK(has(text, "Stack"));
    CHECK(has(text, "world: the test world"));
    CHECK(has(text, "[audio] muted"));

    log::shutdown(0);
    // The run is over: latest.log is gone and its text is in a file named by the time the run was started (2026-11-17_22-40-57.log).
    CHECK(!std::filesystem::exists(dir / "latest.log"));
    std::string finished;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        const bool dated = name.size() == 23 && name[4] == '-' && name[7] == '-' && name[10] == '_' && name.find("unclean") == std::string::npos;
        if (dated && name.rfind("crash-", 0) != 0) finished = slurp(entry.path());
    }
    CHECK(has(finished, "end of log (exit code 0"));
    CHECK(has(finished, "FATAL: fatal error: std::runtime_error"));

    std::filesystem::remove_all(dir);
    if (g_failures == 0) std::printf("log tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
