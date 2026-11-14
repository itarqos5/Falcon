#include "falcon/assets/zip_archive.hpp"

#include "falcon/platform/mapped_file.hpp"

#include <stb_image.h>  // declarations only; also provides the raw-deflate decoder we reuse

#include <algorithm>
#include <cstring>

namespace falcon {
namespace {

std::uint16_t u16(const unsigned char* d, std::size_t at) {
    return static_cast<std::uint16_t>(d[at] | (d[at + 1] << 8));
}

std::uint32_t u32(const unsigned char* d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8) |
           (static_cast<std::uint32_t>(d[at + 2]) << 16) | (static_cast<std::uint32_t>(d[at + 3]) << 24);
}

constexpr std::uint32_t kEndOfCentralDirectory = 0x06054b50;
constexpr std::uint32_t kCentralHeader = 0x02014b50;
constexpr std::uint32_t kLocalHeader = 0x04034b50;

}  // namespace

std::optional<ZipArchive> ZipArchive::open(const std::filesystem::path& file) {
    auto mapped = MappedFile::open(file);
    if (!mapped) return std::nullopt;
    const unsigned char* data = mapped->data();
    const std::size_t size = mapped->size();
    return parse(std::move(mapped), data, size);
}

std::optional<ZipArchive> ZipArchive::fromBytes(std::vector<unsigned char> bytes) {
    auto holder = std::make_shared<const std::vector<unsigned char>>(std::move(bytes));
    const unsigned char* data = holder->data();
    const std::size_t size = holder->size();
    return parse(std::move(holder), data, size);
}

std::optional<ZipArchive> ZipArchive::parse(std::shared_ptr<const void> owner, const unsigned char* data,
                                            std::size_t size) {
    if (size < 22) return std::nullopt;

    // The end-of-central-directory record is within the last 64 KiB (it may carry a comment).
    std::size_t eocd = std::string::npos;
    const std::size_t lowest = size > 65557 ? size - 65557 : 0;
    for (std::size_t i = size - 22 + 1; i-- > lowest;) {
        if (u32(data, i) == kEndOfCentralDirectory) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return std::nullopt;

    const std::size_t count = u16(data, eocd + 10);
    std::size_t pos = u32(data, eocd + 16);

    ZipArchive zip;
    zip.entries_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (pos + 46 > size || u32(data, pos) != kCentralHeader) return std::nullopt;
        Entry e;
        e.method = u16(data, pos + 10);
        e.compressedSize = u32(data, pos + 20);
        e.size = u32(data, pos + 24);
        const std::size_t nameLen = u16(data, pos + 28);
        const std::size_t extraLen = u16(data, pos + 30);
        const std::size_t commentLen = u16(data, pos + 32);
        e.localOffset = u32(data, pos + 42);
        if (pos + 46 + nameLen > size) return std::nullopt;
        std::string name(reinterpret_cast<const char*>(data + pos + 46), nameLen);
        std::replace(name.begin(), name.end(), '\\', '/');  // some Windows tools write backslashes
        zip.entries_.emplace(std::move(name), e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    zip.owner_ = std::move(owner);
    zip.data_ = data;
    zip.size_ = size;
    return zip;
}

std::optional<std::vector<unsigned char>> ZipArchive::read(const std::string& name) const {
    const auto it = entries_.find(name);
    if (it == entries_.end()) return std::nullopt;
    const Entry& e = it->second;

    const std::size_t local = e.localOffset;
    if (local + 30 > size_ || u32(data_, local) != kLocalHeader) return std::nullopt;
    const std::size_t start = local + 30 + u16(data_, local + 26) + u16(data_, local + 28);
    if (start + e.compressedSize > size_) return std::nullopt;
    const unsigned char* payload = data_ + start;

    if (e.method == 0) return std::vector<unsigned char>(payload, payload + e.compressedSize);
    if (e.method != 8) return std::nullopt;  // only stored and deflate

    int outLength = 0;
    char* inflated = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(payload),
                                                      static_cast<int>(e.compressedSize), &outLength);
    if (inflated == nullptr) return std::nullopt;
    std::vector<unsigned char> out(reinterpret_cast<unsigned char*>(inflated),
                                   reinterpret_cast<unsigned char*>(inflated) + outLength);
    stbi_image_free(inflated);
    if (out.size() != e.size) return std::nullopt;
    return out;
}

std::vector<std::string> ZipArchive::entryNames() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& entry : entries_) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}

std::optional<std::string> ZipArchive::readText(const std::string& name) const {
    const auto bytes = read(name);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

}  // namespace falcon
