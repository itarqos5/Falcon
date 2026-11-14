#pragma once

#include <string>

namespace falcon {

// Shows a modal error box. The app runs without a console window, so this is how startup
// failures (missing assets, no OpenGL 3.3, ...) reach the user. Also prints to stderr.
void showErrorDialog(const std::string& title, const std::string& message);

// Opens a folder in the system file manager (Explorer on Windows).
void openFolderInFileManager(const std::string& path);

// If the process was started from a terminal, reconnects stdout/stderr to it (a GUI-subsystem
// exe has none by default). Redirected streams and double-click launches are left alone.
void attachParentConsole();

}  // namespace falcon
