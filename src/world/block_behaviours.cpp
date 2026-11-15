#include "falcon/world/block_behaviours.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace falcon {

std::shared_ptr<const BlockBehaviours> BlockBehaviours::loadFromGameData(const std::filesystem::path& gameDataDir, const BlockRegistry& registry, std::string& error) {
    std::ifstream in(gameDataDir / "block-behaviours.json", std::ios::binary);
    if (!in) {
        error = "block-behaviours.json is missing";
        return nullptr;
    }
    // The file is mostly a list of every block state; it is dropped while reading (the parts used are small).
    const nlohmann::json doc = nlohmann::json::parse(
        in,
        [](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
            if (depth == 1 && event == nlohmann::json::parse_event_t::key) {
                const std::string& key = parsed.get_ref<const std::string&>();
                return key != "states" && key != "items" && key != "fluids";
            }
            return true;
        },
        /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("blocks") || !doc["blocks"].is_array()) {
        error = "block-behaviours.json is not what was expected";
        return nullptr;
    }
    auto out = std::make_shared<BlockBehaviours>();
    const std::size_t count = registry.blocks().size();
    out->flags_.assign(count, 0);
    out->ignite_.assign(count, 0);
    out->burn_.assign(count, 0);
    out->concrete_.assign(count, -1);
    const auto contains = [](const nlohmann::json& list, const char* name) {
        return list.is_array() && std::any_of(list.begin(), list.end(), [&](const nlohmann::json& v) { return v.is_string() && v.get<std::string>() == name; });
    };
    for (const nlohmann::json& b : doc["blocks"]) {
        if (!b.is_object() || !b.contains("name") || !b["name"].is_string()) continue;
        const auto index = registry.findBlock(b["name"].get<std::string>());
        if (!index) continue;
        const std::size_t i = static_cast<std::size_t>(*index);
        const nlohmann::json classes = b.value("class", nlohmann::json::array());
        const nlohmann::json interfaces = b.value("interfaces", nlohmann::json::array());
        std::uint8_t flags = 0;
        if (contains(interfaces, "Fallable")) flags |= kFallable;
        if (contains(classes, "BaseFireBlock")) flags |= kFire;
        if (contains(classes, "FireBlock")) flags |= kSpreadingFire;
        if (contains(classes, "TntBlock")) flags |= kTnt;
        if (contains(classes, "AnvilBlock")) flags |= kAnvil;
        if (contains(classes, "ConcretePowderBlock")) flags |= kPowder;
        out->flags_[i] = flags;
        const nlohmann::json fields = b.value("fields", nlohmann::json::object());
        if (fields.is_object()) {
            if (fields.contains("concrete") && fields["concrete"].is_string()) {
                if (const auto concrete = registry.findBlock(fields["concrete"].get<std::string>())) out->concrete_[i] = *concrete;
            }
            const auto readOdds = [&](const char* key, std::vector<std::uint16_t>& into) {
                const auto it = fields.find(key);
                if (it == fields.end() || !it->is_object()) return;
                for (const auto& [name, value] : it->items()) {
                    if (!value.is_number_integer()) continue;
                    if (const auto target = registry.findBlock(name)) into[static_cast<std::size_t>(*target)] = static_cast<std::uint16_t>(value.get<int>());
                }
            };
            readOdds("igniteOdds", out->ignite_);
            readOdds("burnOdds", out->burn_);
        }
    }
    return out;
}

}  // namespace falcon
