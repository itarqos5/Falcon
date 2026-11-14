#include "apex/game/creative_tabs.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>

namespace apex {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* translationKeyOf(const std::string& id) {
    static const struct {
        const char* id;
        const char* key;
    } kKeys[] = {{"minecraft:building_blocks", "itemGroup.buildingBlocks"}, {"minecraft:colored_blocks", "itemGroup.coloredBlocks"},
                 {"minecraft:natural_blocks", "itemGroup.natural"},         {"minecraft:functional_blocks", "itemGroup.functional"},
                 {"minecraft:redstone_blocks", "itemGroup.redstone"},       {"minecraft:hotbar", "itemGroup.hotbar"},
                 {"minecraft:search", "itemGroup.search"},                  {"minecraft:tools_and_utilities", "itemGroup.tools"},
                 {"minecraft:combat", "itemGroup.combat"},                  {"minecraft:food_and_drinks", "itemGroup.foodAndDrink"},
                 {"minecraft:ingredients", "itemGroup.ingredients"},        {"minecraft:spawn_eggs", "itemGroup.spawnEggs"},
                 {"minecraft:op_blocks", "itemGroup.op"},                   {"minecraft:inventory", "itemGroup.inventory"}};
    for (const auto& k : kKeys) {
        if (id == k.id) return k.key;
    }
    return "";
}

// The value text of the top-level key `key` of {key:value,...} written in the game's text form; "" if there is none.
std::string topLevelValue(const std::string& text, const std::string& key) {
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= n || text[i] != '{') return {};
    ++i;
    while (i < n) {
        while (i < n && (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])))) ++i;
        if (i >= n || text[i] == '}') break;
        std::string name;
        if (text[i] == '"') {
            const std::size_t close = text.find('"', i + 1);
            if (close == std::string::npos) return {};
            name = text.substr(i + 1, close - i - 1);
            i = close + 1;
        } else {
            const std::size_t colon = text.find(':', i);
            if (colon == std::string::npos) return {};
            name = text.substr(i, colon - i);
            i = colon;
        }
        if (i >= n || text[i] != ':') return {};
        ++i;
        const std::size_t start = i;
        int depth = 0;
        bool inString = false;
        for (; i < n; ++i) {
            const char c = text[i];
            if (inString) {
                if (c == '\\') ++i;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (depth == 0) break;
                --depth;
            } else if (c == ',' && depth == 0) break;
        }
        if (name == key) return text.substr(start, i - start);
    }
    return {};
}

std::string unquoted(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

std::optional<ItemStack> parseStackText(const std::string& text, const ItemRegistry& items) {
    const std::string id = unquoted(topLevelValue(text, "id"));
    if (id.empty()) return std::nullopt;
    const auto item = items.find(id);
    if (!item) return std::nullopt;
    int count = 1;
    const std::string c = topLevelValue(text, "count");
    if (!c.empty()) count = std::atoi(c.c_str());
    ItemStack stack(*item, count);
    stack.components = topLevelValue(text, "components");
    return stack;
}

bool CreativeTab::contains(const ItemStack& stack) const {
    for (const CreativeEntry& e : entries) {
        if (e.stack.item == stack.item && e.stack.components == stack.components) return true;
    }
    return false;
}

const CreativeTab* CreativeTabs::find(const std::string& id) const {
    for (const CreativeTab& t : tabs_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::vector<const CreativeEntry*> CreativeTabs::search(const std::string& rawText) const {
    std::vector<const CreativeEntry*> out;
    const CreativeTab* tab = find("minecraft:search");
    if (tab == nullptr) return out;
    const std::string text = lower(rawText);
    const bool byTag = !text.empty() && text[0] == '#';
    std::vector<std::string> words;
    {
        std::string word;
        for (char c : byTag ? text.substr(1) : text) {
            if (c == ' ') {
                if (!word.empty()) words.push_back(std::move(word));
                word.clear();
            } else {
                word += c;
            }
        }
        if (!word.empty()) words.push_back(std::move(word));
    }
    for (const CreativeEntry& e : tab->entries) {
        bool ok = true;
        for (const std::string& w : words) {
            bool found = false;
            if (byTag) {
                for (const std::string& t : e.tags) {
                    if (lower(t).find(w) != std::string::npos) {
                        found = true;
                        break;
                    }
                }
            } else {
                found = e.searchText.find(w) != std::string::npos;
            }
            if (!found) {
                ok = false;
                break;
            }
        }
        if (ok) out.push_back(&e);
    }
    return out;
}

std::vector<const CreativeTab*> CreativeTabs::tabsContaining(const ItemStack& stack) const {
    std::vector<const CreativeTab*> out;
    for (const CreativeTab& t : tabs_) {
        if (t.type == CreativeTabType::Search || t.type == CreativeTabType::Inventory) continue;
        if (t.contains(stack)) out.push_back(&t);
    }
    return out;
}

std::string CreativeTabs::variantNameOf(Item item, const std::string& components) const {
    const CreativeTab* tab = find("minecraft:search");
    if (tab == nullptr || components.empty()) return {};
    for (const CreativeEntry& e : tab->entries) {
        if (e.stack.item == item && e.stack.components == components) return e.stack.variantName;
    }
    return {};
}

std::shared_ptr<const CreativeTabs> CreativeTabs::loadFromGameData(const fs::path& dir, const ItemRegistry& items, std::string& error) {
    std::ifstream in(dir / "creative-inventory.json");
    if (!in) {
        error = "creative-inventory.json is not in " + dir.string();
        return nullptr;
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.contains("tabs") || !doc["tabs"].is_array()) {
        error = "creative-inventory.json is not readable";
        return nullptr;
    }
    auto out = std::make_shared<CreativeTabs>();
    for (const json& t : doc["tabs"]) {
        CreativeTab tab;
        tab.id = t.value("id", std::string());
        const std::string type = t.value("type", std::string("CATEGORY"));
        tab.type = type == "HOTBAR" ? CreativeTabType::Hotbar : type == "SEARCH" ? CreativeTabType::Search : type == "INVENTORY" ? CreativeTabType::Inventory : CreativeTabType::Category;
        tab.top = t.value("row", std::string("TOP")) == "TOP";
        tab.column = t.value("column", 0);
        tab.title = t.value("title", std::string());
        tab.translationKey = translationKeyOf(tab.id);
        tab.visible = !t.contains("visible") || !t["visible"].is_boolean() || t["visible"].get<bool>();
        tab.permissionsOnly = t.contains("permissions_only") && t["permissions_only"].is_boolean() && t["permissions_only"].get<bool>();
        if (const auto icon = parseStackText(t.value("icon", std::string()), items)) tab.icon = *icon;
        const auto list = t.find("items");
        const auto lines = t.find("lines");
        const auto tags = t.find("tags");
        if (list != t.end() && list->is_array()) {
            for (std::size_t i = 0; i < list->size(); ++i) {
                const auto stack = parseStackText((*list)[i].get<std::string>(), items);
                if (!stack) continue;
                CreativeEntry e;
                e.stack = *stack;
                if (tab.type == CreativeTabType::Search) {
                    std::string text;
                    if (lines != t.end() && lines->is_array() && i < lines->size() && (*lines)[i].is_array()) {
                        for (const json& line : (*lines)[i]) {
                            if (line.is_string()) {
                                if (text.empty()) e.stack.variantName = e.stack.components.empty() ? std::string() : line.get<std::string>();
                                text += lower(line.get<std::string>()) + "\n";
                            }
                        }
                    }
                    text += "minecraft:" + e.stack.type().name;
                    e.searchText = std::move(text);
                    if (tags != t.end() && tags->is_array() && i < tags->size() && (*tags)[i].is_array()) {
                        for (const json& tag : (*tags)[i]) {
                            if (tag.is_string()) e.tags.push_back(tag.get<std::string>());
                        }
                    }
                }
                tab.entries.push_back(std::move(e));
            }
        }
        out->tabs_.push_back(std::move(tab));
    }
    if (out->tabs_.empty()) {
        error = "creative-inventory.json lists no tabs";
        return nullptr;
    }
    // The names of stacks with components come from the search tab's tooltip lines; every other tab gets them too.
    for (CreativeTab& tab : out->tabs_) {
        if (tab.type == CreativeTabType::Search) continue;
        for (CreativeEntry& e : tab.entries) {
            if (!e.stack.components.empty()) e.stack.variantName = out->variantNameOf(e.stack.item, e.stack.components);
        }
    }
    return out;
}

std::shared_ptr<const CreativeTabs> CreativeTabs::makeMinimal(const ItemRegistry& items) {
    auto out = std::make_shared<CreativeTabs>();
    const auto stack = [&](const char* name) { return ItemStack(items.find(name).value_or(Item::Air), 1); };
    CreativeTab building;
    building.id = "minecraft:building_blocks";
    building.title = "Building Blocks";
    building.translationKey = "itemGroup.buildingBlocks";
    building.icon = stack("bricks");
    for (const char* n : {"stone", "dirt", "cobblestone", "oak_planks", "oak_log", "bricks", "stone_bricks", "sandstone"}) building.entries.push_back({stack(n), {}, {}});
    CreativeTab search;
    search.id = "minecraft:search";
    search.type = CreativeTabType::Search;
    search.column = 6;
    search.title = "Search Items";
    search.translationKey = "itemGroup.search";
    search.icon = stack("stone");
    for (const char* n : {"stone", "dirt", "cobblestone", "oak_planks", "oak_log", "bricks", "stone_bricks", "sandstone", "stick", "apple", "diamond_pickaxe"}) {
        CreativeEntry e{stack(n), {}, {}};
        e.searchText = lower(e.stack.type().name) + "\nminecraft:" + e.stack.type().name;
        search.entries.push_back(std::move(e));
    }
    CreativeTab inventory;
    inventory.id = "minecraft:inventory";
    inventory.type = CreativeTabType::Inventory;
    inventory.top = false;
    inventory.column = 6;
    inventory.title = "Survival Inventory";
    inventory.translationKey = "itemGroup.inventory";
    inventory.icon = stack("crafting_table");
    out->tabs_ = {building, search, inventory};
    return out;
}

namespace {
std::mutex g_tabsMutex;
std::shared_ptr<const CreativeTabs> g_tabs;
}  // namespace

std::shared_ptr<const CreativeTabs> currentCreativeTabs() {
    std::lock_guard<std::mutex> lock(g_tabsMutex);
    return g_tabs;
}

void installCreativeTabs(std::shared_ptr<const CreativeTabs> tabs) {
    std::lock_guard<std::mutex> lock(g_tabsMutex);
    g_tabs = std::move(tabs);
}

}  // namespace apex
