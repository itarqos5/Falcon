#include "apex/world/item_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace apex {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string_view stripNamespace(std::string_view name) {
    if (name.substr(0, 10) == "minecraft:") name.remove_prefix(10);
    return name;
}

Rarity rarityOf(const std::string& s) {
    if (s == "uncommon") return Rarity::Uncommon;
    if (s == "rare") return Rarity::Rare;
    if (s == "epic") return Rarity::Epic;
    return Rarity::Common;
}

EquipSlot slotOf(const std::string& s) {
    if (s == "head") return EquipSlot::Head;
    if (s == "chest") return EquipSlot::Chest;
    if (s == "legs") return EquipSlot::Legs;
    if (s == "feet") return EquipSlot::Feet;
    if (s == "body") return EquipSlot::Body;
    if (s == "offhand") return EquipSlot::OffHand;
    if (s == "mainhand") return EquipSlot::MainHand;
    if (s == "saddle") return EquipSlot::Saddle;
    return EquipSlot::None;
}

// The numbers behind "minecraft:cooking/time_coal" and friends (data/minecraft/context_int_provider/...): a division of
// a constant by a reduction factor that is 1 unless the fuel is burnt in a "fast cooking" block, which an item never is.
class IntProviders {
public:
    explicit IntProviders(fs::path dir) : dir_(std::move(dir)) {}

    int resolve(const std::string& reference, int depth = 0) {
        if (depth > 8) return 0;
        const std::string name(stripNamespace(reference));
        if (const auto it = cache_.find(name); it != cache_.end()) return it->second;
        int value = 0;
        std::ifstream in(dir_ / (name + ".json"));
        if (in) {
            const json doc = json::parse(in, nullptr, false);
            if (!doc.is_discarded()) value = eval(doc, depth + 1);
        }
        cache_[name] = value;
        return value;
    }

private:
    int eval(const json& node, int depth) {
        if (node.is_number()) return node.get<int>();
        if (node.is_string()) return resolve(node.get<std::string>(), depth);
        if (!node.is_object()) return 0;
        const std::string type = node.value("type", std::string());
        if (type == "minecraft:div") {
            const int right = eval(node.value("right", json(1)), depth + 1);
            return right == 0 ? 0 : eval(node.value("left", json(0)), depth + 1) / right;
        }
        if (type == "minecraft:mul") return eval(node.value("left", json(0)), depth + 1) * eval(node.value("right", json(1)), depth + 1);
        if (type == "minecraft:add") return eval(node.value("left", json(0)), depth + 1) + eval(node.value("right", json(0)), depth + 1);
        if (type == "minecraft:conditional") return eval(node.value("on_false", json(0)), depth + 1);  // no condition holds for an item
        if (type == "minecraft:constant") return eval(node.value("value", json(0)), depth + 1);
        return 0;
    }

    fs::path dir_;
    std::unordered_map<std::string, int> cache_;
};


// ---------------------------------------------------------------------------------------------------------------
// The binary cache: the parsed items (without block links, which come from the block registry) in one small file.

constexpr char kCacheMagic[4] = {'F', 'I', 'R', '1'};

struct Fingerprint {
    std::uint64_t size[2] = {};
    std::int64_t time[2] = {};
    bool operator==(const Fingerprint&) const = default;
};

std::optional<Fingerprint> fingerprintOf(const fs::path& dir) {
    Fingerprint f;
    const char* names[2] = {"items.json", "registries.json"};
    for (int i = 0; i < 2; ++i) {
        std::error_code ec;
        const fs::path p = dir / names[i];
        f.size[i] = fs::file_size(p, ec);
        if (ec) return std::nullopt;
        f.time[i] = static_cast<std::int64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
        if (ec) return std::nullopt;
    }
    return f;
}

template <typename T>
void put(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
bool get(std::istream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}
void putString(std::ostream& out, const std::string& s) {
    put(out, static_cast<std::uint16_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
bool getString(std::istream& in, std::string& s) {
    std::uint16_t n = 0;
    if (!get(in, n)) return false;
    s.resize(n);
    in.read(s.data(), n);
    return static_cast<bool>(in);
}

}  // namespace

class ItemRegistryBuilder {
public:
    static std::vector<ItemType>& items(ItemRegistry& r) { return r.items_; }
    static void add(ItemRegistry& r, ItemType t) {
        const auto index = static_cast<std::uint16_t>(r.items_.size());
        t.protocolId = index;
        r.byName_[t.name] = index;
        r.items_.push_back(std::move(t));
    }

    static void writeCache(const fs::path& path, const Fingerprint& fp, const ItemRegistry& r) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path tmp = path.string() + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(kCacheMagic, 4);
        put(out, fp);
        put(out, static_cast<std::uint32_t>(r.items_.size()));
        for (const ItemType& t : r.items_) {
            putString(out, t.name);
            putString(out, t.translationKey);
            putString(out, t.itemModel);
            putString(out, t.useRemainder);
            put(out, t.maxStack);
            put(out, t.maxDamage);
            put(out, static_cast<std::uint8_t>(t.rarity));
            put(out, static_cast<std::uint8_t>(t.glint));
            put(out, static_cast<std::uint8_t>(t.food.has_value()));
            put(out, t.food.value_or(FoodInfo{}));
            put(out, static_cast<std::uint8_t>(t.isTool));
            put(out, t.toolDamagePerBlock);
            put(out, static_cast<std::uint16_t>(t.toolRules.size()));
            for (const ToolRule& rule : t.toolRules) {
                putString(out, rule.blocks);
                put(out, static_cast<std::uint8_t>(rule.hasSpeed));
                put(out, rule.speed);
                put(out, rule.correctForDrops);
            }
            put(out, t.attackDamageBonus);
            put(out, t.attackSpeedBonus);
            put(out, static_cast<std::uint8_t>(t.hasAttackModifiers));
            put(out, t.armor);
            put(out, t.armorToughness);
            put(out, static_cast<std::uint8_t>(t.armorSlot));
            put(out, static_cast<std::uint8_t>(t.equipSlot));
            put(out, t.fuelBurnTicks);
        }
        out.close();
        if (out) {
            fs::rename(tmp, path, ec);
            if (ec) fs::remove(tmp, ec);
        }
    }

    static bool readCache(const fs::path& path, const Fingerprint& fp, ItemRegistry& r) {
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {};
        Fingerprint stored;
        std::uint32_t count = 0;
        if (!in.read(magic, 4) || std::memcmp(magic, kCacheMagic, 4) != 0 || !get(in, stored) || !(stored == fp) || !get(in, count) || count > 65535) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            ItemType t;
            std::uint8_t rarity = 0, glint = 0, hasFood = 0, isTool = 0, hasSpeedFlag = 0, armorSlot = 0, equipSlot = 0, hasAttack = 0;
            std::uint16_t rules = 0;
            FoodInfo food;
            bool ok = getString(in, t.name) && getString(in, t.translationKey) && getString(in, t.itemModel) && getString(in, t.useRemainder) &&
                      get(in, t.maxStack) && get(in, t.maxDamage) && get(in, rarity) && get(in, glint) && get(in, hasFood) && get(in, food) &&
                      get(in, isTool) && get(in, t.toolDamagePerBlock) && get(in, rules);
            if (!ok) return false;
            for (std::uint16_t k = 0; k < rules; ++k) {
                ToolRule rule;
                ok = getString(in, rule.blocks) && get(in, hasSpeedFlag) && get(in, rule.speed) && get(in, rule.correctForDrops);
                if (!ok) return false;
                rule.hasSpeed = hasSpeedFlag != 0;
                t.toolRules.push_back(std::move(rule));
            }
            ok = get(in, t.attackDamageBonus) && get(in, t.attackSpeedBonus) && get(in, hasAttack) && get(in, t.armor) && get(in, t.armorToughness) &&
                 get(in, armorSlot) && get(in, equipSlot) && get(in, t.fuelBurnTicks);
            if (!ok) return false;
            t.rarity = static_cast<Rarity>(rarity);
            t.glint = glint != 0;
            if (hasFood != 0) t.food = food;
            t.isTool = isTool != 0;
            t.hasAttackModifiers = hasAttack != 0;
            t.armorSlot = static_cast<EquipSlot>(armorSlot);
            t.equipSlot = static_cast<EquipSlot>(equipSlot);
            add(r, std::move(t));
        }
        return true;
    }
    // Links every block item to the block it places: a block named like the item first, else the first block
    // that lists the item as its own.
    static void linkBlocks(ItemRegistry& r, const BlockRegistry& blocks) {
        std::unordered_map<std::string, int> byItem;
        for (std::size_t i = 0; i < blocks.blocks().size(); ++i) {
            const BlockType& b = blocks.blocks()[i];
            if (b.item.empty() || b.item == "air") continue;
            const auto [it, inserted] = byItem.emplace(b.item, static_cast<int>(i));
            if (!inserted && b.name == b.item) it->second = static_cast<int>(i);  // the block that carries the item's own name wins
        }
        r.itemOfBlock_.clear();
        for (ItemType& t : r.items_) {
            t.blockIndex = -1;
            if (t.name == "air") continue;
            const auto it = byItem.find(t.name);
            if (it == byItem.end()) continue;
            t.blockIndex = it->second;
        }
        // The reverse: the item of a block is the item it lists (wall_torch stands for the torch item).
        for (std::size_t i = 0; i < blocks.blocks().size(); ++i) {
            const BlockType& b = blocks.blocks()[i];
            if (b.item.empty() || b.item == "air") continue;
            const auto id = r.byName_.find(b.item);
            if (id == r.byName_.end()) continue;
            r.itemOfBlock_[static_cast<int>(i)] = static_cast<Item>(id->second);
        }
    }
};

std::optional<Item> ItemRegistry::find(std::string_view name) const {
    const auto it = byName_.find(std::string(stripNamespace(name)));
    if (it == byName_.end()) return std::nullopt;
    return static_cast<Item>(it->second);
}

std::shared_ptr<const ItemRegistry> ItemRegistry::loadFromGameData(const fs::path& dir, const fs::path& cacheDir, const BlockRegistry* blocks, std::string& error) {
    auto reg = std::make_shared<ItemRegistry>();
    const auto fingerprint = fingerprintOf(dir);
    const fs::path cachePath = cacheDir / "items.cache";
    if (fingerprint && ItemRegistryBuilder::readCache(cachePath, *fingerprint, *reg)) {
        if (blocks != nullptr) ItemRegistryBuilder::linkBlocks(*reg, *blocks);
        reg->fromGameData_ = true;
        return reg;
    }
    reg = std::make_shared<ItemRegistry>();
    try {
        // Protocol ids: the game numbers items by this table, and the order of items.json is alphabetical.
        std::vector<std::pair<int, std::string>> order;
        {
            std::ifstream in(dir / "registries.json");
            if (!in) throw std::runtime_error("registries.json is not in " + dir.string());
            const json registries = json::parse(in);
            const json& entries = registries.at("minecraft:item").at("entries");
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                order.emplace_back(it.value().at("protocol_id").get<int>(), std::string(stripNamespace(it.key())));
            }
            std::sort(order.begin(), order.end());
        }
        std::ifstream in(dir / "items.json");
        if (!in) throw std::runtime_error("items.json is not in " + dir.string());
        const json doc = json::parse(in);
        IntProviders providers(dir / "data" / "minecraft" / "context_int_provider");

        for (std::size_t index = 0; index < order.size(); ++index) {
            if (order[index].first != static_cast<int>(index)) throw std::runtime_error("the item protocol ids are not consecutive");
            const std::string& name = order[index].second;
            ItemType t;
            t.name = name;
            const auto entry = doc.find("minecraft:" + name);
            if (entry != doc.end() && entry->is_object()) {
                const json& c = *entry;
                t.maxStack = c.value("minecraft:max_stack_size", 64);
                t.maxDamage = c.value("minecraft:max_damage", 0);
                if (const auto n = c.find("minecraft:item_name"); n != c.end() && n->is_object()) t.translationKey = n->value("translate", std::string());
                t.itemModel = std::string(stripNamespace(c.value("minecraft:item_model", std::string("minecraft:") + name)));
                t.rarity = rarityOf(c.value("minecraft:rarity", std::string("common")));
                if (const auto g = c.find("minecraft:enchantment_glint_override"); g != c.end() && g->is_boolean()) t.glint = g->get<bool>();
                if (const auto f = c.find("minecraft:food"); f != c.end() && f->is_object()) {
                    FoodInfo food;
                    food.nutrition = f->value("nutrition", 0);
                    food.saturation = f->value("saturation", 0.0f);
                    food.canAlwaysEat = f->value("can_always_eat", false);
                    t.food = food;
                }
                if (const auto tool = c.find("minecraft:tool"); tool != c.end() && tool->is_object()) {
                    t.isTool = true;
                    t.toolDamagePerBlock = tool->value("damage_per_block", 1);
                    if (const auto rules = tool->find("rules"); rules != tool->end() && rules->is_array()) {
                        for (const json& r : *rules) {
                            ToolRule rule;
                            if (r.contains("blocks") && r["blocks"].is_string()) rule.blocks = r["blocks"].get<std::string>();
                            if (r.contains("speed") && r["speed"].is_number()) {
                                rule.hasSpeed = true;
                                rule.speed = r["speed"].get<float>();
                            }
                            if (r.contains("correct_for_drops") && r["correct_for_drops"].is_boolean()) rule.correctForDrops = r["correct_for_drops"].get<bool>() ? 1 : 0;
                            t.toolRules.push_back(std::move(rule));
                        }
                    }
                }
                if (const auto mods = c.find("minecraft:attribute_modifiers"); mods != c.end() && mods->is_array()) {
                    for (const json& m : *mods) {
                        const std::string type = std::string(stripNamespace(m.value("type", std::string())));
                        const std::string slot = m.value("slot", std::string());
                        const float amount = m.value("amount", 0.0f);
                        if (type == "attack_damage") {
                            t.attackDamageBonus += amount;
                            t.hasAttackModifiers = true;
                        } else if (type == "attack_speed") {
                            t.attackSpeedBonus += amount;
                            t.hasAttackModifiers = true;
                        } else if (type == "armor") {
                            t.armor += amount;
                            t.armorSlot = slotOf(slot);
                        } else if (type == "armor_toughness") {
                            t.armorToughness += amount;
                        }
                    }
                }
                if (const auto e = c.find("minecraft:equippable"); e != c.end() && e->is_object()) t.equipSlot = slotOf(e->value("slot", std::string()));
                if (const auto fuel = c.find("minecraft:cooking_fuel"); fuel != c.end() && fuel->is_object()) {
                    if (fuel->contains("burn_time")) {
                        const json& bt = (*fuel)["burn_time"];
                        t.fuelBurnTicks = bt.is_number() ? bt.get<int>() : bt.is_string() ? providers.resolve(bt.get<std::string>()) : 0;
                    }
                }
                if (const auto r = c.find("minecraft:use_remainder"); r != c.end() && r->is_object()) {
                    t.useRemainder = std::string(stripNamespace(r->value("id", std::string())));
                }
            } else {
                t.translationKey = "item.minecraft." + name;
                t.itemModel = name;
            }
            ItemRegistryBuilder::add(*reg, std::move(t));
        }
        if (fingerprint) ItemRegistryBuilder::writeCache(cachePath, *fingerprint, *reg);
        if (blocks != nullptr) ItemRegistryBuilder::linkBlocks(*reg, *blocks);
        reg->fromGameData_ = true;
    } catch (const std::exception& e) {
        error = std::string("reading the item data failed: ") + e.what();
        return nullptr;
    }
    return reg;
}

std::shared_ptr<const ItemRegistry> ItemRegistry::makeMinimal(const BlockRegistry* blocks) {
    auto reg = std::make_shared<ItemRegistry>();
    const auto add = [&](const char* name, const char* key, int maxStack = 64, int maxDamage = 0) {
        ItemType t;
        t.name = name;
        t.translationKey = key;
        t.itemModel = name;
        t.maxStack = maxStack;
        t.maxDamage = maxDamage;
        ItemRegistryBuilder::add(*reg, std::move(t));
        return static_cast<int>(reg->items_.size()) - 1;
    };
    add("air", "block.minecraft.air");
    for (const char* block : {"stone", "dirt", "grass_block", "bedrock", "sand", "gravel", "cobblestone", "deepslate", "oak_planks", "oak_log", "bricks", "stone_bricks", "sandstone"}) {
        add(block, (std::string("block.minecraft.") + block).c_str());
    }
    add("stick", "item.minecraft.stick");
    add("coal", "item.minecraft.coal");
    reg->items_.back().fuelBurnTicks = 1600;
    add("crafting_table", "block.minecraft.crafting_table");
    const int apple = add("apple", "item.minecraft.apple");
    reg->items_[static_cast<std::size_t>(apple)].food = FoodInfo{4, 2.4f, false};
    const int pick = add("diamond_pickaxe", "item.minecraft.diamond_pickaxe", 1, 1561);
    ItemType& p = reg->items_[static_cast<std::size_t>(pick)];
    p.isTool = true;
    p.hasAttackModifiers = true;
    p.attackDamageBonus = 4.0f;
    p.attackSpeedBonus = -2.8f;
    const int chest = add("iron_chestplate", "item.minecraft.iron_chestplate", 1, 240);
    ItemType& c = reg->items_[static_cast<std::size_t>(chest)];
    c.armor = 6.0f;
    c.armorSlot = EquipSlot::Chest;
    c.equipSlot = EquipSlot::Chest;
    const int helmet = add("iron_helmet", "item.minecraft.iron_helmet", 1, 165);
    reg->items_[static_cast<std::size_t>(helmet)].armor = 2.0f;
    reg->items_[static_cast<std::size_t>(helmet)].armorSlot = EquipSlot::Head;
    reg->items_[static_cast<std::size_t>(helmet)].equipSlot = EquipSlot::Head;
    const int shield = add("shield", "item.minecraft.shield", 1, 336);
    reg->items_[static_cast<std::size_t>(shield)].equipSlot = EquipSlot::OffHand;
    add("water_bucket", "item.minecraft.water_bucket", 1);
    add("white_dye", "item.minecraft.white_dye");
    add("red_dye", "item.minecraft.red_dye");
    add("blue_dye", "item.minecraft.blue_dye");
    add("pink_dye", "item.minecraft.pink_dye");
    add("magenta_dye", "item.minecraft.magenta_dye");
    add("bone_meal", "item.minecraft.bone_meal");
    add("ender_pearl", "item.minecraft.ender_pearl", 16);
    const int golden = add("enchanted_golden_apple", "item.minecraft.enchanted_golden_apple");
    reg->items_[static_cast<std::size_t>(golden)].rarity = Rarity::Epic;
    reg->items_[static_cast<std::size_t>(golden)].glint = true;
    if (blocks != nullptr) ItemRegistryBuilder::linkBlocks(*reg, *blocks);
    return reg;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
std::shared_ptr<const ItemRegistry> g_items;
std::once_flag g_itemsFallbackOnce;
}  // namespace

void installItemRegistry(std::shared_ptr<const ItemRegistry> registry) { g_items = std::move(registry); }

bool hasItemRegistry() { return g_items != nullptr; }

std::shared_ptr<const ItemRegistry> currentItemRegistry() { return g_items; }

const ItemRegistry& itemRegistry() {
    if (!g_items) std::call_once(g_itemsFallbackOnce, [] { installItemRegistry(ItemRegistry::makeMinimal(&blockRegistry())); });
    return *g_items;
}

}  // namespace apex
