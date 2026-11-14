#include "falcon/assets/resource_source.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace falcon {
namespace fs = std::filesystem;
namespace {

class DirectoryPack final : public ResourceSource {
public:
    explicit DirectoryPack(fs::path root) : root_(std::move(root)) {}

    bool contains(const ResourceLocation& loc) const override {
        std::error_code ec;
        return fs::is_regular_file(path(loc), ec);
    }

    std::optional<std::vector<unsigned char>> read(const ResourceLocation& loc) const override {
        std::ifstream in(path(loc), std::ios::binary);
        if (!in) return std::nullopt;
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>());
    }

private:
    [[nodiscard]] fs::path path(const ResourceLocation& loc) const {
        return root_ / "assets" / loc.ns / fs::path(loc.path);
    }

    fs::path root_;
};

class ZipPack final : public ResourceSource {
public:
    explicit ZipPack(ZipArchive zip) : zip_(std::move(zip)) {}

    bool contains(const ResourceLocation& loc) const override { return zip_.contains(name(loc)); }

    std::optional<std::vector<unsigned char>> read(const ResourceLocation& loc) const override {
        return zip_.read(name(loc));
    }

private:
    [[nodiscard]] static std::string name(const ResourceLocation& loc) {
        return "assets/" + loc.ns + "/" + loc.path;
    }

    ZipArchive zip_;
};

}  // namespace

std::shared_ptr<const ResourceSource> makeDirectoryPack(fs::path packRoot) {
    return std::make_shared<DirectoryPack>(std::move(packRoot));
}

std::shared_ptr<const ResourceSource> makeZipPack(ZipArchive zip) {
    return std::make_shared<ZipPack>(std::move(zip));
}

}  // namespace falcon
