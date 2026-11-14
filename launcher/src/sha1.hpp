#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace launcher {

// SHA-1 as lower-case hex (Windows CNG). Mojang names and checks its files with SHA-1; it is used here only to check that a download
// is the file that was meant, never for anything secret.
[[nodiscard]] std::string sha1Hex(std::string_view data);
// Empty if the file cannot be read.
[[nodiscard]] std::string sha1FileHex(const std::filesystem::path& file);

}  // namespace launcher
