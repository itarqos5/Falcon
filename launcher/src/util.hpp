#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace launcher {

// UTF-8 <-> UTF-16 (what the Windows API and the web view use).
[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

// Where the launcher keeps everything: %LOCALAPPDATA%\Falcon (FALCON_LAUNCHER_HOME overrides it: tests and development use that).
//   launcher.json   settings and what setup has done          account.bin   the sign-in (DPAPI-protected)
//   client\<ver>\   the bundled game client, extracted        game\<ver>\   the game's files, downloaded from Mojang
//   webview\        the web view's own profile                logs\         launcher.log
[[nodiscard]] std::filesystem::path dataRoot();

// A line in logs\launcher.log (also to stderr when FALCON_LAUNCHER_LOG_STDERR is set). Safe from any thread.
void logLine(std::string_view text);

// Milliseconds since the process started (for the start-up timing the log reports).
[[nodiscard]] double millisSinceStart();

[[nodiscard]] std::string toHex(const unsigned char* bytes, std::size_t count);
[[nodiscard]] std::string randomHex(std::size_t byteCount);  // cryptographically random, as hex
[[nodiscard]] std::string lowerCase(std::string text);

// Writes a whole file through a temporary name, so a crash never leaves half of it.
bool writeFileAtomic(const std::filesystem::path& path, std::string_view contents);
[[nodiscard]] bool readFile(const std::filesystem::path& path, std::string& out);

// "https://..." only, no spaces or control characters: what may be handed to the system browser.
[[nodiscard]] bool isSafeWebUrl(std::string_view url);
// Opens a web page in the user's default browser.
void openInBrowser(const std::string& url);
// Shows a folder in the file manager.
void openFolder(const std::filesystem::path& folder);

}  // namespace launcher
