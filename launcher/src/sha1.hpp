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
// SHA-256 as lower-case hex, for the files that are Falcon's own (the data tables): the file a release names is the file that is used.
[[nodiscard]] std::string sha256Hex(std::string_view data);
[[nodiscard]] std::string sha256FileHex(const std::filesystem::path& file);

}  // namespace launcher
