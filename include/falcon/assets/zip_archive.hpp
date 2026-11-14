#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {

// Minimal read-only .zip reader (stored and deflate entries), enough for resource packs.
//
// A zip opened from a file is memory-mapped: opening reads only the table of contents, and an
// entry is read (and inflated) when it is asked for. Reading is thread-safe.
class ZipArchive {
public:
    // Nullopt if the file is missing or not a zip.
    [[nodiscard]] static std::optional<ZipArchive> open(const std::filesystem::path& file);
    // Same, from bytes already in memory (used by tests).
    [[nodiscard]] static std::optional<ZipArchive> fromBytes(std::vector<unsigned char> bytes);

    // Entry names use '/' (a backslash written by some Windows tools is treated as '/').
    [[nodiscard]] bool contains(const std::string& name) const { return entries_.contains(name); }
    [[nodiscard]] std::optional<std::vector<unsigned char>> read(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> readText(const std::string& name) const;
    [[nodiscard]] std::size_t entryCount() const { return entries_.size(); }
    [[nodiscard]] std::vector<std::string> entryNames() const;

private:
    struct Entry {
        std::uint16_t method = 0;
        std::uint32_t compressedSize = 0;
        std::uint32_t size = 0;
        std::uint32_t localOffset = 0;
    };

    [[nodiscard]] static std::optional<ZipArchive> parse(std::shared_ptr<const void> owner,
                                                         const unsigned char* data, std::size_t size);

    std::shared_ptr<const void> owner_;  // keeps the mapped file / byte buffer alive
    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace falcon
