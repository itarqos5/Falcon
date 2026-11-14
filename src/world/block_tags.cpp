#include "falcon/world/block_tags.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

namespace falcon {

namespace {

std::string withoutNamespace(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

void collect(const std::filesystem::path& dir, const BlockRegistry& registry, const std::string& tag, std::vector<std::uint8_t>& out, std::set<std::string>& seen) {
    if (!seen.insert(tag).second) return;
    std::ifstream in(dir / (tag + ".json"));
    if (!in) return;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("values") || !doc["values"].is_array()) return;
    for (const nlohmann::json& v : doc["values"]) {
        std::string id;
        if (v.is_string()) id = v.get<std::string>();
        else if (v.is_object() && v.contains("id") && v["id"].is_string()) id = v["id"].get<std::string>();
        if (id.empty()) continue;
        if (id[0] == '#') {
            collect(dir, registry, withoutNamespace(id.substr(1)), out, seen);
        } else if (const auto block = registry.findBlock(id)) {
            out[static_cast<std::size_t>(*block)] = 1;
        }
    }
}

}  // namespace

const std::vector<std::uint8_t>& BlockTags::members(const std::string& tag) const {
    auto it = cache_.find(tag);
    if (it != cache_.end()) return it->second;
    std::vector<std::uint8_t> out(registry_.blocks().size(), 0);
    std::set<std::string> seen;
    collect(dir_, registry_, tag, out, seen);
    return cache_.emplace(tag, std::move(out)).first->second;
}

bool BlockTags::contains(std::string_view tag, std::size_t blockIndex) const {
    std::string name(tag);
    if (!name.empty() && name[0] == '#') name.erase(0, 1);
    name = withoutNamespace(std::move(name));
    const std::vector<std::uint8_t>& set = members(name);
    return blockIndex < set.size() && set[blockIndex] != 0;
}

}  // namespace falcon
