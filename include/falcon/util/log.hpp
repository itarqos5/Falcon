#pragma once

// The client's log: what the game does and why it went wrong, in a file of its own.
//
// Every run writes logs/client.log in Falcon's folder (%LOCALAPPDATA%\Falcon on Windows, ~/.local/share/Falcon elsewhere), a line to a line, flushed as it is written, so that
// whatever happened before a crash is there: "2026-11-17 22:31:05.123 INFO  [render] 40 fps ...". The run before it is kept as client-<date>.log (the last 15; one that did not end
// cleanly is marked -unclean), and a crash or a fatal error writes logs/crash-<date>.log: what happened and where, the stack, what the game was doing (screen, world, position,
// render distance ...) and the last lines of the log. This is the client's alone: the launcher keeps its own log.
//
// The old console lines ("[falcon] ...") go through line(), which prints them as before and logs them with a level and a category worked out from what they say.

#include <filesystem>
#include <functional>
#include <string>
#include <typeinfo>
#include <vector>

namespace falcon::log {

enum class Level { Debug, Info, Warn, Error };

struct Settings {
    std::string version;           // "Falcon 1.3.0 (Minecraft 26.3)"
    std::string commandLine;       // what the program was started with (for the header)
    bool echo = true;              // also to the console (stdout), as the game always did: the launcher and the test scripts read it
    int keep = 15;                 // client logs and crash reports kept
    bool toFile = true;            // false: the console only (scripted runs that must not fill the person's folder)
};

// Opens the log file (the one of the run before is renamed) and writes the header: the program, the PC and when it started. False if no file could be made (the console still gets
// the lines). FALCON_LOG_DIR puts the files somewhere else, FALCON_LOG_LEVEL=debug logs the debug lines too.
bool init(const Settings& settings);
// The last line, which says the run ended cleanly (a log without it is a run that crashed or was killed).
void shutdown(int exitCode);

[[nodiscard]] std::filesystem::path directory();    // empty when there is no file
[[nodiscard]] std::filesystem::path currentFile();

#if defined(__GNUC__) && !defined(__clang__)
#define FALCON_LOG_FORMAT(a, b) __attribute__((format(gnu_printf, a, b)))  // (gnu_printf: %zu and the like, which MinGW prints as the C standard says)
#else
#define FALCON_LOG_FORMAT(a, b)
#endif

void write(Level level, const std::string& category, const std::string& message);
void writef(Level level, const char* category, const char* format, ...) FALCON_LOG_FORMAT(3, 4);
// "[falcon] <message>\n" as the game printed it (the "\n" is optional): the console gets the line as it is, the log a level (error, warning or info, by what it says) and a category (the
// word before a colon: "audio: muted" is [audio]).
void line(const char* format, ...) FALCON_LOG_FORMAT(1, 2);

template <class... Args>
void debug(const char* category, const char* format, Args... args) {
    writef(Level::Debug, category, format, args...);
}
template <class... Args>
void info(const char* category, const char* format, Args... args) {
    writef(Level::Info, category, format, args...);
}
template <class... Args>
void warn(const char* category, const char* format, Args... args) {
    writef(Level::Warn, category, format, args...);
}
template <class... Args>
void error(const char* category, const char* format, Args... args) {
    writef(Level::Error, category, format, args...);
}

// What the game is doing, for the crash report: each provider says it in a line ("screen: PauseScreen", "world: New World, overworld, 96 136 -31"); they are asked when the program
// crashes, so they must not wait for anything. A provider with the same name is replaced.
void addContext(const std::string& name, std::function<std::string()> provider);
void removeContext(const std::string& name);

// An unhandled exception, a signal (a crash, abort()) and std::terminate write a crash report before the process ends. `notify` is told where it is (a window can say so).
void installCrashHandler(std::function<void(const std::filesystem::path& report)> notify = nullptr);
// The same report for a fatal error the program caught itself. Returns the file ("" if it could not be written).
std::filesystem::path writeCrashReport(const std::string& title, const std::string& detail);

// The last `count` lines written (the crash report holds them).
[[nodiscard]] std::vector<std::string> recentLines(std::size_t count);
// "falcon::PauseScreen" for a type_info.
[[nodiscard]] std::string typeName(const std::type_info& type);

}  // namespace falcon::log
