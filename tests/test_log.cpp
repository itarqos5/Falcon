// The client's log (util/log.hpp): the file it writes, the levels and categories of the old console lines, the rotation of the run before (and its being marked when it did not end
// cleanly), the crash report, and the end-of-log line.
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
    // The run before this one: a log that stopped without its last line (a crash), and the crash report it left.
    {
        std::ofstream((dir / "client.log").string(), std::ios::binary) <<"2026-01-01 10:00:00.000 INFO  [client] it was running\n";
        std::ofstream((dir / "crash-20260101-100001.log").string(), std::ios::binary) << "Falcon crash report\n";
    }

    log::Settings settings;
    settings.version = "Falcon test";
    settings.echo = false;
    CHECK(log::init(settings));
    CHECK(log::directory() == dir);
    CHECK(log::currentFile() == dir / "client.log");

    // The run before is kept, marked, and the new log says so (and where its crash report is).
    bool markedUnclean = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("client-", 0) == 0 && name.find("-unclean.log") != std::string::npos) markedUnclean = true;
    }
    CHECK(markedUnclean);

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
    CHECK(has(live, "crash-20260101-100001.log"));
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
    const std::string finished = slurp(dir / "client.log");
    CHECK(has(finished, "end of log (exit code 0"));
    CHECK(has(finished, "FATAL: fatal error: std::runtime_error"));

    std::filesystem::remove_all(dir);
    if (g_failures == 0) std::printf("log tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
