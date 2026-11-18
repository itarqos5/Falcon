#pragma once

#include <string>

namespace falcon {

// Shows a modal error box. The app runs without a console window, so this is how startup
// failures (missing assets, no OpenGL 3.3, ...) reach the user. Also prints to stderr.
void showErrorDialog(const std::string& title, const std::string& message);

// Opens a folder in the system file manager (Explorer on Windows).
void openFolderInFileManager(const std::string& path);

// Opens a web address in the browser. Only http and https addresses are opened (nothing that a mod's own metadata could use to start another program), without spaces, quotes or
// control characters; false when the address is not one of those.
bool openWebAddress(const std::string& url);

// If the process was started from a terminal, reconnects stdout/stderr to it (a GUI-subsystem
// exe has none by default). Redirected streams and double-click launches are left alone.
void attachParentConsole();

}  // namespace falcon
