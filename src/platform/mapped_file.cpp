#include "falcon/platform/mapped_file.hpp"

#include <fstream>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace falcon {

std::shared_ptr<const MappedFile> MappedFile::open(const std::filesystem::path& file) {
    std::shared_ptr<MappedFile> result(new MappedFile());

#ifdef _WIN32
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(handle, &size) == 0 || size.QuadPart <= 0) {
        CloseHandle(handle);
        return nullptr;
    }
    const HANDLE mapping = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(handle);  // the mapping keeps the file open
    if (mapping != nullptr) {
        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);  // the view keeps the mapping alive
        if (view != nullptr) {
            result->data_ = static_cast<const unsigned char*>(view);
            result->size_ = static_cast<std::size_t>(size.QuadPart);
            result->mapped_ = true;
            return result;
        }
    }
#endif

    std::ifstream in(file, std::ios::binary);
    if (!in) return nullptr;
    result->fallback_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (result->fallback_.empty()) return nullptr;
    result->data_ = result->fallback_.data();
    result->size_ = result->fallback_.size();
    return result;
}

MappedFile::~MappedFile() {
#ifdef _WIN32
    if (mapped_) UnmapViewOfFile(data_);
#endif
}

}  // namespace falcon
