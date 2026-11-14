#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace falcon {

// A read-only file whose bytes are mapped straight into memory: opening costs nothing however
// large the file is, only the parts that are read get loaded, and those pages are the OS's cache
// rather than memory this process holds. (Where mapping is unavailable the file is read whole.)
class MappedFile {
public:
    // Null if the file is missing, empty or cannot be mapped.
    [[nodiscard]] static std::shared_ptr<const MappedFile> open(const std::filesystem::path& file);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] const unsigned char* data() const { return data_; }
    [[nodiscard]] std::size_t size() const { return size_; }

private:
    MappedFile() = default;

    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    bool mapped_ = false;
    std::vector<unsigned char> fallback_;  // used when the file could not be mapped
};

}  // namespace falcon
