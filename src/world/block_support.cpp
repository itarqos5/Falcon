#include "apex/world/block_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace apex {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string stripNamespace(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

// The blocks of a block tag, following `#minecraft:other_tag` references.
void collectTag(const fs::path& tagDir, const BlockRegistry& registry, const std::string& tag, std::vector<std::uint8_t>& out, std::set<std::string>& seen) {
    if (!seen.insert(tag).second) return;
    std::ifstream in(tagDir / (tag + ".json"));
    if (!in) return;
    json doc = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("values") || !doc["values"].is_array()) return;
    for (const json& v : doc["values"]) {
        std::string id;
        if (v.is_string()) id = v.get<std::string>();
        else if (v.is_object() && v.contains("id") && v["id"].is_string()) id = v["id"].get<std::string>();
        if (id.empty()) continue;
        if (id[0] == '#') {
            collectTag(tagDir, registry, stripNamespace(id.substr(1)), out, seen);
        } else if (const auto block = registry.findBlock(id)) {
            out[static_cast<std::size_t>(*block)] = 1;
        }
    }
}

std::vector<std::uint8_t> tagBlocks(const fs::path& tagDir, const BlockRegistry& registry, const std::string& tag) {
    std::vector<std::uint8_t> out(registry.blocks().size(), 0);
    std::set<std::string> seen;
    collectTag(tagDir, registry, tag, out, seen);
    return out;
}

}  // namespace

std::shared_ptr<const BlockSupport> BlockSupport::loadFromGameData(const fs::path& dir, const BlockRegistry& registry, std::string& error) {
    std::ifstream in(dir / "block-behaviours.json", std::ios::binary);
    if (!in) {
        error = "block-behaviours.json is not in " + dir.string();
        return nullptr;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto out = std::shared_ptr<BlockSupport>(new BlockSupport());
    out->registry_ = &registry;
    const std::size_t n = registry.blocks().size();
    out->soil_.assign(n, Soil::None);
    out->halfProperty_.assign(n, 0);

    // The class chain of each block: {"name":"minecraft:tall_grass","class":["DoublePlantBlock","VegetationBlock","Block"], ...
    // (a plain scan: the file is 2 MB and this runs when a world opens).
    static const std::string kName = "{\"name\":\"minecraft:";
    std::size_t at = 0;
    while ((at = text.find(kName, at)) != std::string::npos) {
        at += kName.size();
        const std::size_t nameEnd = text.find('"', at);
        if (nameEnd == std::string::npos) break;
        const std::string name = text.substr(at, nameEnd - at);
        const std::size_t classAt = text.find("\"class\":[", nameEnd);
        const std::size_t classEnd = classAt == std::string::npos ? std::string::npos : text.find(']', classAt);
        if (classAt == std::string::npos || classEnd == std::string::npos) break;
        const std::string chain = text.substr(classAt + 9, classEnd - classAt - 9);
        at = classEnd;
        const auto block = registry.findBlock(name);
        if (!block) continue;
        const auto has = [&](const char* cls) { return chain.find(std::string("\"") + cls + "\"") != std::string::npos; };
        // The classes whose canSurvive is "a block of the tag below": the plain plants. (Mushrooms, nether roots, the wither rose,
        // lily pads, stems and the like have rules of their own and are left alone.)
        const bool wither = has("WitherRoseBlock");
        Soil soil = Soil::None;
        if (has("CropBlock")) soil = Soil::Crops;
        else if (has("DryVegetationBlock")) soil = Soil::Dry;
        else if (!wither && (has("DoublePlantBlock") || has("TallGrassBlock") || has("FlowerBlock") || has("SaplingBlock") || has("SweetBerryBushBlock"))) soil = Soil::Vegetation;
        out->soil_[static_cast<std::size_t>(*block)] = soil;
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (const BlockProperty& p : registry.blocks()[i].properties) {
            if (p.name == "half" && std::find(p.values.begin(), p.values.end(), "lower") != p.values.end()) out->halfProperty_[i] = 1;
        }
    }
    const fs::path tagDir = dir / "data" / "minecraft" / "tags" / "block";
    out->vegetationTag_ = tagBlocks(tagDir, registry, "supports_vegetation");
    out->cropsTag_ = tagBlocks(tagDir, registry, "supports_crops");
    out->dryTag_ = tagBlocks(tagDir, registry, "supports_dry_vegetation");
    return out;
}

bool BlockSupport::isDoubleHalf(Block block) const {
    const int b = registry_->state(block).block;
    return halfProperty_[static_cast<std::size_t>(b)] != 0;
}

bool BlockSupport::isLowerHalf(Block block) const { return isDoubleHalf(block) && registry_->get(block, "half") == "lower"; }

Block BlockSupport::upperHalf(Block lower) const { return registry_->with(lower, "half", "upper"); }

bool BlockSupport::needsSoil(Block block) const {
    return soil_[static_cast<std::size_t>(registry_->state(block).block)] != Soil::None;
}

bool BlockSupport::canSurvive(Block block, int x, int y, int z, const Reader& at) const {
    const std::size_t index = static_cast<std::size_t>(registry_->state(block).block);
    const Soil soil = soil_[index];
    // The upper half of a double block stands on its lower half; everything else on its soil.
    if (halfProperty_[index] != 0 && registry_->get(block, "half") == "upper") {
        const Block below = at(x, y - 1, z);
        return registry_->state(below).block == registry_->state(block).block && registry_->get(below, "half") == "lower";
    }
    if (soil == Soil::None) return true;
    const std::size_t under = static_cast<std::size_t>(registry_->state(at(x, y - 1, z)).block);
    switch (soil) {
        case Soil::Vegetation: return vegetationTag_[under] != 0;
        case Soil::Crops: return cropsTag_[under] != 0;
        case Soil::Dry: return dryTag_[under] != 0;
        case Soil::None: break;
    }
    return true;
}

Block BlockSupport::afterNeighborChange(Block block, int x, int y, int z, const Reader& at) const {
    if (registry_->isAir(block)) return block;
    const std::size_t index = static_cast<std::size_t>(registry_->state(block).block);
    if (halfProperty_[index] != 0) {
        // DoublePlantBlock.updateShape / DoorBlock.updateShape: each half needs the other one, of the same block.
        const bool lower = registry_->get(block, "half") == "lower";
        const Block other = at(x, y + (lower ? 1 : -1), z);
        if (registry_->state(other).block != registry_->state(block).block || registry_->get(other, "half") == registry_->get(block, "half")) {
            return commonBlocks().air;
        }
        if (lower && soil_[index] != Soil::None && !canSurvive(block, x, y, z, at)) return commonBlocks().air;  // the soil under the lower half
        return block;
    }
    if (soil_[index] != Soil::None && !canSurvive(block, x, y, z, at)) return commonBlocks().air;
    return block;
}

}  // namespace apex
