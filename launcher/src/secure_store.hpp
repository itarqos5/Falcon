#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace launcher {

// A secret kept on disk, encrypted with the Windows account's key (DPAPI): only this user on this PC can read it back. That is what the
// Microsoft refresh token lives in, so signing in is needed once, not at every start.
bool protectToFile(const std::filesystem::path& file, const std::string& secret);
[[nodiscard]] std::optional<std::string> unprotectFromFile(const std::filesystem::path& file);

}  // namespace launcher
