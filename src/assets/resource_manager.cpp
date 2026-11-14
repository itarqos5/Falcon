#include "apex/assets/resource_manager.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace apex {
namespace fs = std::filesystem;

ResourceManager::ResourceManager(fs::path assetsRoot, PackList packs)
    : root_(std::move(assetsRoot)), packs_(std::make_shared<const PackList>(std::move(packs))) {}

ResourceManager::ResourceManager(const ResourceManager& other)
    : root_(other.root_), packs_(other.snapshot()) {}

ResourceManager::Snapshot ResourceManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packs_;
}

ResourceManager::PackList ResourceManager::packs() const { return *snapshot(); }

void ResourceManager::setPacks(PackList packs) {
    auto fresh = std::make_shared<const PackList>(std::move(packs));
    std::lock_guard<std::mutex> lock(mutex_);
    packs_ = std::move(fresh);
}

fs::path ResourceManager::resolve(const ResourceLocation& loc) const {
    return root_ / loc.ns / fs::path(loc.path);
}

namespace {

std::optional<std::vector<unsigned char>> readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

bool ResourceManager::exists(const ResourceLocation& loc) const {
    for (const auto& pack : *snapshot()) {
        if (pack->contains(loc)) return true;
    }
    std::error_code ec;
    return fs::is_regular_file(resolve(loc), ec);
}

std::optional<std::vector<unsigned char>> ResourceManager::readBytes(const ResourceLocation& loc) const {
    const Snapshot packs = snapshot();
    for (auto it = packs->rbegin(); it != packs->rend(); ++it) {  // highest priority first
        if (auto bytes = (*it)->read(loc)) return bytes;
    }
    return readFile(resolve(loc));
}

std::optional<std::string> ResourceManager::readText(const ResourceLocation& loc) const {
    const auto bytes = readBytes(loc);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

std::vector<std::string> ResourceManager::readAllText(const ResourceLocation& loc) const {
    std::vector<std::string> texts;
    if (const auto base = readFile(resolve(loc))) texts.emplace_back(base->begin(), base->end());
    for (const auto& pack : *snapshot()) {
        if (const auto bytes = pack->read(loc)) texts.emplace_back(bytes->begin(), bytes->end());
    }
    return texts;
}

std::vector<const void*> ResourceManager::providers(const ResourceLocation& loc) const {
    std::vector<const void*> who;
    std::error_code ec;
    if (fs::is_regular_file(resolve(loc), ec)) who.push_back(nullptr);
    for (const auto& pack : *snapshot()) {
        if (pack->contains(loc)) who.push_back(pack.get());
    }
    return who;
}

}  // namespace apex
