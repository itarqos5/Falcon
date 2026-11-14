#pragma once

#include "http.hpp"
#include "result.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace launcher {

// Something compiled into the exe (an RCDATA resource): the UI page, the game client, the WebView2 loader.
struct Blob {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    [[nodiscard]] bool empty() const { return data == nullptr || size == 0; }
};
[[nodiscard]] Blob embeddedResource(const wchar_t* name);  // empty in a program built without the resource (the tests)

// The game client is always bundled in the launcher, so the two always fit each other. Setup puts it at
// <dataRoot>\client\<id>\Falcon.exe, where <id> is the start of the client's SHA-1: a launcher update with a new client gets a new
// folder, and the old ones are removed. Returns the path of the exe. Checks an existing copy (it may have been deleted or damaged).
[[nodiscard]] Result<std::filesystem::path> installBundledClient(const std::filesystem::path& dataRoot, const Blob& client);
// True if `<dataRoot>\client\<id>\Falcon.exe` is in place and is the bundled client.
[[nodiscard]] bool bundledClientInstalled(const std::filesystem::path& dataRoot, const Blob& client, std::filesystem::path* exe = nullptr);

// A "Falcon Launcher" shortcut in the Start menu (to the exe that is running).
bool createStartMenuShortcut();

// The Microsoft Edge WebView2 runtime: what shows the UI. Windows 11 and recent Windows 10 have it; for a PC without it, setup
// downloads Microsoft's small installer and runs it.
[[nodiscard]] bool webView2RuntimeInstalled();
[[nodiscard]] Result<bool> installWebView2Runtime(HttpClient& http);

}  // namespace launcher
