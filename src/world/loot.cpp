#include "falcon/world/loot.hpp"

#include "falcon/world/item_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace falcon {

using json = nlohmann::json;

namespace {

std::string withoutNamespace(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

std::shared_ptr<json> readJson(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return nullptr;
    auto doc = std::make_shared<json>(json::parse(in, nullptr, /*allow_exceptions=*/false));
    if (doc->is_discarded()) return nullptr;
    return doc;
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

}  // namespace

struct BlockLoot::Context {
    Block block;
    const ItemStack& tool;
    std::mt19937& random;
    [[nodiscard]] float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(random); }
    [[nodiscard]] int between(int lo, int hi) { return hi <= lo ? lo : std::uniform_int_distribution<int>(lo, hi)(random); }
};

BlockLoot::BlockLoot(std::filesystem::path dataRoot, const BlockRegistry& registry) : root_(std::move(dataRoot)), registry_(registry) {}
BlockLoot::~BlockLoot() = default;

const json* BlockLoot::table(const std::string& blockName) const {
    auto it = tables_.find(blockName);
    if (it == tables_.end()) it = tables_.emplace(blockName, readJson(root_ / "data" / "minecraft" / "loot_table" / "blocks" / (blockName + ".json"))).first;
    return it->second.get();
}

const json* BlockLoot::predicate(const std::string& id) const {
    const std::string path = withoutNamespace(id);
    auto it = predicates_.find(path);
    if (it == predicates_.end()) it = predicates_.emplace(path, readJson(root_ / "data" / "minecraft" / "predicate" / (path + ".json"))).first;
    return it->second.get();
}

// A number, or {"type": "minecraft:uniform" | "constant" | "binomial", ...}: the whole number it stands for this time.
int BlockLoot::sampleCount(const json& provider, Context& c) const {
    if (provider.is_number()) return static_cast<int>(provider.get<double>());
    if (!provider.is_object()) return 1;
    const std::string type = withoutNamespace(provider.value("type", std::string("constant")));
    const auto number = [&](const char* key, int fallback) {
        const auto it = provider.find(key);
        return it != provider.end() ? sampleCount(*it, c) : fallback;
    };
    if (type == "uniform") return c.between(number("min", 0), number("max", 0));
    if (type == "binomial") {
        const int n = number("n", 1);
        const double p = provider.value("p", 0.5);
        int hits = 0;
        for (int i = 0; i < n; ++i) hits += c.unit() < static_cast<float>(p) ? 1 : 0;
        return hits;
    }
    return number("value", 1);
}

bool BlockLoot::allMet(const json& holder, const char* key, Context& c) const {
    const auto it = holder.find(key);
    return it == holder.end() || met(*it, c);
}

bool BlockLoot::met(const json& condition, Context& c, int depth) const {
    if (depth > 12) return true;
    if (condition.is_string()) {  // the id of a predicate file, such as minecraft:tool/can_shear
        const json* named = predicate(condition.get<std::string>());
        return named == nullptr || met(*named, c, depth + 1);
    }
    if (condition.is_array()) {
        return std::all_of(condition.begin(), condition.end(), [&](const json& term) { return met(term, c, depth + 1); });
    }
    if (!condition.is_object()) return true;
    const std::string type = withoutNamespace(condition.value("type", std::string()));
    if (type == "all_of" || type == "any_of") {
        const auto terms = condition.find("terms");
        if (terms == condition.end() || !terms->is_array()) return true;
        if (type == "all_of") return std::all_of(terms->begin(), terms->end(), [&](const json& t) { return met(t, c, depth + 1); });
        return std::any_of(terms->begin(), terms->end(), [&](const json& t) { return met(t, c, depth + 1); });
    }
    if (type == "inverted") {
        const auto term = condition.find("term");
        return term != condition.end() && !met(*term, c, depth + 1);
    }
    if (type == "random_chance") {
        const auto chance = condition.find("chance");
        const float p = chance == condition.end() ? 1.0f : chance->is_number() ? chance->get<float>() : static_cast<float>(sampleCount(*chance, c));
        return c.unit() < p;
    }
    if (type == "table_bonus") {  // chances by enchantment level: level 0 here
        const auto chances = condition.find("chances");
        if (chances == condition.end() || !chances->is_array() || chances->empty()) return true;
        return c.unit() < (*chances)[0].get<float>();
    }
    if (type == "match_block") {
        const auto blocks = condition.find("blocks");
        if (blocks != condition.end()) {
            const std::string name = registry_.type(c.block).name;
            bool found = false;
            if (blocks->is_string()) found = withoutNamespace(blocks->get<std::string>()) == name;
            else if (blocks->is_array()) found = std::any_of(blocks->begin(), blocks->end(), [&](const json& b) { return b.is_string() && withoutNamespace(b.get<std::string>()) == name; });
            if (!found) return false;
        }
        const auto state = condition.find("state");
        if (state != condition.end() && state->is_object()) {
            for (const auto& [key, wanted] : state->items()) {
                const std::string_view have = registry_.get(c.block, key);
                if (wanted.is_string()) {
                    if (have != wanted.get<std::string>()) return false;
                } else if (wanted.is_boolean()) {
                    if (have != (wanted.get<bool>() ? "true" : "false")) return false;
                } else if (wanted.is_object()) {  // {"min": 2, "max": 7}
                    const int value = std::atoi(std::string(have).c_str());
                    if (wanted.contains("min") && value < wanted["min"].get<int>()) return false;
                    if (wanted.contains("max") && value > wanted["max"].get<int>()) return false;
                }
            }
        }
        return true;
    }
    if (type == "match_tool") {
        const auto pred = condition.find("predicate");
        if (pred == condition.end() || !pred->is_object()) return true;
        if (pred->contains("predicates")) return false;  // enchantments (silk touch) and other components: this player has none
        const auto items = pred->find("items");
        if (items == pred->end()) return !c.tool.isEmpty();
        if (c.tool.isEmpty()) return false;
        const std::string tool = c.tool.type().name;
        const auto matches = [&](const std::string& wanted) {
            if (wanted.rfind("#", 0) == 0) {  // the tool tags that block loot asks about
                const std::string tag = withoutNamespace(wanted.substr(1));
                return (tag == "pickaxes" && endsWith(tool, "_pickaxe")) || (tag == "axes" && endsWith(tool, "_axe")) || (tag == "shovels" && endsWith(tool, "_shovel")) ||
                       (tag == "hoes" && endsWith(tool, "_hoe")) || (tag == "swords" && endsWith(tool, "_sword"));
            }
            return withoutNamespace(wanted) == tool;
        };
        if (items->is_string()) return matches(items->get<std::string>());
        if (items->is_array()) return std::any_of(items->begin(), items->end(), [&](const json& i) { return i.is_string() && matches(i.get<std::string>()); });
        return true;
    }
    if (type == "enchantment_active_check") return false;
    return true;  // survives_explosion, location_check, entity_properties ... and what is not known
}

void BlockLoot::applyModifiers(const json& modifiers, Context& c, ItemStack& stack) const {
    if (!modifiers.is_array()) return;
    for (const json& m : modifiers) {
        if (!m.is_object() || !allMet(m, "condition", c)) continue;
        const std::string type = withoutNamespace(m.value("type", std::string()));
        if (type == "set_count") {
            const auto count = m.find("count");
            if (count == m.end()) continue;
            const int n = sampleCount(*count, c);
            stack.count = m.value("add", false) ? stack.count + n : n;
        } else if (type == "limit_count") {
            if (const auto limit = m.find("limit"); limit != m.end() && limit->is_object()) {
                if (limit->contains("min")) stack.count = std::max(stack.count, sampleCount((*limit)["min"], c));
                if (limit->contains("max")) stack.count = std::min(stack.count, sampleCount((*limit)["max"], c));
            }
        } else if (type == "apply_bonus") {  // the fortune formulas at level 0
            const std::string formula = withoutNamespace(m.value("formula", std::string()));
            const json parameters = m.contains("parameters") && m["parameters"].is_object() ? m["parameters"] : json::object();
            if (formula == "binomial_with_bonus_count") {
                const int extra = parameters.value("extra", 0);
                const float probability = parameters.value("probability", 0.5f);
                for (int i = 0; i < extra; ++i) stack.count += c.unit() < probability ? 1 : 0;
            }  // uniform_bonus_count adds nextInt(multiplier * 0 + 1) = 0, ore_drops multiplies by 1
        }
        // explosion_decay, copy_state, copy_components, set_components ...: nothing to do here
    }
}

void BlockLoot::runEntry(const json& entry, Context& c, std::vector<ItemStack>& out, bool conditionsChecked) const {
    if (!entry.is_object() || (!conditionsChecked && !allMet(entry, "condition", c))) return;  // (a condition with a chance in it is rolled once, by whoever checks it first)
    const std::string type = withoutNamespace(entry.value("type", std::string()));
    const auto children = entry.find("children");
    if (type == "alternatives") {  // the first child whose conditions hold
        if (children == entry.end() || !children->is_array()) return;
        for (const json& child : *children) {
            if (child.is_object() && allMet(child, "condition", c)) {
                runEntry(child, c, out, true);
                return;
            }
        }
    } else if (type == "group") {
        if (children != entry.end() && children->is_array()) {
            for (const json& child : *children) runEntry(child, c, out);
        }
    } else if (type == "sequence") {
        if (children != entry.end() && children->is_array()) {
            for (const json& child : *children) {
                if (!child.is_object() || !allMet(child, "condition", c)) return;
                runEntry(child, c, out, true);
            }
        }
    } else if (type == "item") {
        const auto item = itemRegistry().find(entry.value("name", std::string()));
        if (!item || *item == Item::Air) return;
        ItemStack stack(*item, 1);
        if (const auto modifiers = entry.find("modifier"); modifiers != entry.end()) applyModifiers(*modifiers, c, stack);
        if (stack.count > 0) out.push_back(std::move(stack));
    }
    // empty, tag, loot_table, dynamic: nothing a bare block gives
}

std::vector<ItemStack> BlockLoot::drops(Block block, const ItemStack& tool, std::mt19937& random) const {
    std::vector<ItemStack> out;
    const json* doc = table(registry_.type(block).name);
    if (doc == nullptr && registry_.type(block).name.find(':') != std::string::npos) {  // a mod's block has no table here: it drops its own item (a tool, when it needs one)
        const Item own = itemRegistry().itemOfBlock(registry_.state(block).block);
        if (own != Item::Air && (!registry_.state(block).has(kStateRequiresTool) || !tool.isEmpty())) out.push_back(ItemStack(own, 1));
        return out;
    }
    if (doc == nullptr || !doc->is_object()) return out;
    const auto pools = doc->find("pools");
    if (pools == doc->end() || !pools->is_array()) return out;
    Context c{block, tool, random};
    for (const json& pool : *pools) {
        if (!pool.is_object() || !allMet(pool, "condition", c)) continue;
        const auto entries = pool.find("entries");
        if (entries == pool.end() || !entries->is_array()) continue;
        const auto rolls = pool.find("rolls");
        const int count = rolls == pool.end() ? 1 : sampleCount(*rolls, c);
        for (int roll = 0; roll < count; ++roll) {
            if (entries->size() == 1) {
                runEntry((*entries)[0], c, out);
                continue;
            }
            // Several entries: each roll picks one by weight (among those whose conditions hold).
            std::vector<const json*> candidates;
            int total = 0;
            for (const json& entry : *entries) {
                if (!entry.is_object() || !allMet(entry, "condition", c)) continue;
                candidates.push_back(&entry);
                total += std::max(1, entry.value("weight", 1));
            }
            if (candidates.empty()) continue;
            int pick = c.between(0, total - 1);
            for (const json* entry : candidates) {
                pick -= std::max(1, entry->value("weight", 1));
                if (pick < 0) {
                    runEntry(*entry, c, out, true);
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace falcon
