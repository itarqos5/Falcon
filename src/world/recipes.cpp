#include "apex/world/recipes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>

namespace apex {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string_view stripNamespace(std::string_view name) {
    if (name.substr(0, 10) == "minecraft:") name.remove_prefix(10);
    return name;
}

std::optional<json> readJson(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) return std::nullopt;
    return doc;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Tags

void ItemTags::define(const std::string& name, std::vector<std::uint16_t> items) {
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    tags_[std::string(stripNamespace(name))] = std::move(items);
}

const std::vector<std::uint16_t>& ItemTags::items(std::string_view name) const {
    static const std::vector<std::uint16_t> kNone;
    if (!name.empty() && name.front() == '#') name.remove_prefix(1);
    const auto it = tags_.find(stripNamespace(name));
    return it == tags_.end() ? kNone : it->second;
}

bool ItemTags::contains(std::string_view name) const {
    if (!name.empty() && name.front() == '#') name.remove_prefix(1);
    return tags_.find(stripNamespace(name)) != tags_.end();
}

ItemTags ItemTags::load(const fs::path& gameDataDir, const ItemRegistry& items, std::string& error) {
    ItemTags out;
    const fs::path root = gameDataDir / "data" / "minecraft" / "tags" / "item";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        error = "no item tags in " + root.string();
        return out;
    }
    // Raw tag files: name -> the entries ("minecraft:oak_planks", "#minecraft:logs").
    std::map<std::string, std::vector<std::string>> raw;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".json") continue;
        const auto doc = readJson(it->path());
        if (!doc || !doc->is_object()) continue;
        std::string name = fs::relative(it->path(), root, ec).generic_string();
        name.erase(name.size() - 5);  // ".json"
        std::vector<std::string> values;
        if (const auto v = doc->find("values"); v != doc->end() && v->is_array()) {
            for (const json& e : *v) {
                if (e.is_string()) {
                    values.push_back(e.get<std::string>());
                } else if (e.is_object() && e.contains("id") && e["id"].is_string()) {
                    values.push_back(e["id"].get<std::string>());
                }
            }
        }
        raw[name] = std::move(values);
    }
    std::set<std::string> resolving;
    std::function<const std::vector<std::uint16_t>&(const std::string&)> resolve = [&](const std::string& name) -> const std::vector<std::uint16_t>& {
        static const std::vector<std::uint16_t> kNone;
        if (const auto done = out.tags_.find(name); done != out.tags_.end()) return done->second;
        const auto it = raw.find(name);
        if (it == raw.end() || !resolving.insert(name).second) return kNone;
        std::vector<std::uint16_t> result;
        for (const std::string& entry : it->second) {
            if (!entry.empty() && entry[0] == '#') {
                const std::vector<std::uint16_t>& nested = resolve(std::string(stripNamespace(std::string_view(entry).substr(1))));
                result.insert(result.end(), nested.begin(), nested.end());
            } else if (const auto item = items.find(entry)) {
                result.push_back(itemId(*item));
            }
        }
        resolving.erase(name);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return out.tags_.emplace(name, std::move(result)).first->second;
    };
    for (const auto& [name, values] : raw) {
        (void)values;
        resolve(name);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Matching

CraftingInput CraftingInput::fromGrid(int gridWidth, int gridHeight, const std::vector<ItemStack>& grid) {
    int minX = gridWidth, minY = gridHeight, maxX = -1, maxY = -1, count = 0;
    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            if (grid[static_cast<std::size_t>(y * gridWidth + x)].isEmpty()) continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            ++count;
        }
    }
    CraftingInput in;
    if (count == 0) return in;
    in.width = maxX - minX + 1;
    in.height = maxY - minY + 1;
    in.left = minX;
    in.top = minY;
    in.ingredientCount = count;
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) in.items.push_back(grid[static_cast<std::size_t>((y + minY) * gridWidth + x + minX)]);
    }
    return in;
}

namespace {
bool cellMatches(const Ingredient& ingredient, const ItemStack& stack) {
    if (ingredient.empty()) return stack.isEmpty();
    return !stack.isEmpty() && ingredient.test(stack.item);
}
}  // namespace

bool ShapedRecipe::matches(const CraftingInput& input) const {
    if (input.ingredientCount != ingredientCount || input.width != width || input.height != height) return false;
    const auto oriented = [&](bool mirrored) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const Ingredient& ingredient = cells[static_cast<std::size_t>(mirrored ? width - x - 1 + y * width : x + y * width)];
                if (!cellMatches(ingredient, input.at(x, y))) return false;
            }
        }
        return true;
    };
    if (!symmetrical && oriented(true)) return true;
    return oriented(false);
}

bool ShapelessRecipe::matches(const CraftingInput& input) const {
    if (input.ingredientCount != static_cast<int>(ingredients.size())) return false;
    // Every item must be given to a different ingredient that accepts it: a small bipartite matching.
    std::vector<const ItemStack*> stacks;
    for (const ItemStack& s : input.items) {
        if (!s.isEmpty()) stacks.push_back(&s);
    }
    std::vector<int> owner(ingredients.size(), -1);  // ingredient -> the stack index using it
    std::function<bool(int, std::vector<char>&)> assign = [&](int stack, std::vector<char>& seen) {
        for (std::size_t i = 0; i < ingredients.size(); ++i) {
            if (seen[i] || !ingredients[i].test(stacks[static_cast<std::size_t>(stack)]->item)) continue;
            seen[i] = 1;
            if (owner[i] == -1 || assign(owner[i], seen)) {
                owner[i] = stack;
                return true;
            }
        }
        return false;
    };
    for (std::size_t s = 0; s < stacks.size(); ++s) {
        std::vector<char> seen(ingredients.size(), 0);
        if (!assign(static_cast<int>(s), seen)) return false;
    }
    return true;
}

bool TransmuteRecipe::matches(const CraftingInput& grid) const {
    if (grid.ingredientCount != 2) return false;
    std::vector<const ItemStack*> stacks;
    for (const ItemStack& s : grid.items) {
        if (!s.isEmpty()) stacks.push_back(&s);
    }
    if (stacks.size() != 2) return false;
    return (input.test(stacks[0]->item) && material.test(stacks[1]->item)) || (input.test(stacks[1]->item) && material.test(stacks[0]->item));
}

std::optional<CraftingMatch> RecipeManager::find(const CraftingInput& input) const {
    if (input.ingredientCount == 0) return std::nullopt;
    int index = 0;
    for (const ShapedRecipe& r : shaped_) {
        if (r.matches(input)) return CraftingMatch{r.result, index};
        ++index;
    }
    for (const ShapelessRecipe& r : shapeless_) {
        if (r.matches(input)) return CraftingMatch{r.result, index};
        ++index;
    }
    for (const TransmuteRecipe& r : transmute_) {
        if (!r.matches(input)) continue;
        // The result item takes over the components (and name) of the item that was transmuted.
        ItemStack out(r.result, 1);
        for (const ItemStack& s : input.items) {
            if (!s.isEmpty() && r.input.test(s.item)) {
                out.customName = s.customName;
                out.components = s.components;
                break;
            }
        }
        return CraftingMatch{out, index};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------
// Loading

namespace {
constexpr char kRecipeCacheMagic[4] = {'F', 'R', 'C', '1'};
}  // namespace

class RecipeBuilder {
public:
    RecipeBuilder(RecipeManager& manager, const ItemRegistry& items) : m_(manager), items_(items) {}

    // ---- the binary cache: one small file instead of ~2000 small ones (which take seconds to open) ----
    struct Fingerprint {
        std::uint64_t registrySize = 0;
        std::int64_t registryTime = 0;
        std::int64_t recipeDirTime = 0;
        std::int64_t tagDirTime = 0;
        std::uint32_t itemCount = 0;
        bool operator==(const Fingerprint&) const = default;
    };
    static std::optional<Fingerprint> fingerprintOf(const fs::path& dir, const ItemRegistry& items) {
        Fingerprint f;
        std::error_code ec;
        f.registrySize = fs::file_size(dir / "registries.json", ec);
        if (ec) return std::nullopt;
        f.registryTime = static_cast<std::int64_t>(fs::last_write_time(dir / "registries.json", ec).time_since_epoch().count());
        f.recipeDirTime = static_cast<std::int64_t>(fs::last_write_time(dir / "data" / "minecraft" / "recipe", ec).time_since_epoch().count());
        f.tagDirTime = static_cast<std::int64_t>(fs::last_write_time(dir / "data" / "minecraft" / "tags" / "item", ec).time_since_epoch().count());
        f.itemCount = static_cast<std::uint32_t>(items.size());
        return f;
    }
    template <typename T>
    static void put(std::ostream& out, const T& v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T>
    static bool get(std::istream& in, T& v) {
        in.read(reinterpret_cast<char*>(&v), sizeof(T));
        return static_cast<bool>(in);
    }
    static void putString(std::ostream& out, const std::string& s) {
        put(out, static_cast<std::uint16_t>(s.size()));
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    static bool getString(std::istream& in, std::string& s) {
        std::uint16_t n = 0;
        if (!get(in, n)) return false;
        s.resize(n);
        in.read(s.data(), n);
        return static_cast<bool>(in);
    }
    static void putResult(std::ostream& out, const ItemStack& s) {
        put(out, itemId(s.item));
        put(out, static_cast<std::int32_t>(s.count));
    }
    static bool getResult(std::istream& in, ItemStack& s) {
        std::uint16_t id = 0;
        std::int32_t count = 0;
        if (!get(in, id) || !get(in, count)) return false;
        s = ItemStack(static_cast<Item>(id), count);
        return true;
    }

    static void writeCache(const fs::path& path, const Fingerprint& fp, const RecipeManager& m) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path tmp = path.string() + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        // Every distinct ingredient once; recipes point at them (0 is the empty ingredient).
        std::map<std::vector<std::uint16_t>, std::uint32_t> table;
        std::vector<const std::vector<std::uint16_t>*> ordered;
        const auto indexOf = [&](const Ingredient& ing) -> std::uint32_t {
            if (ing.empty()) return 0;
            const auto it = table.emplace(ing.items, static_cast<std::uint32_t>(table.size() + 1));
            if (it.second) ordered.push_back(&it.first->first);
            return it.first->second;
        };
        for (const ShapedRecipe& r : m.shaped_) {
            for (const Ingredient& i : r.cells) (void)indexOf(i);
        }
        for (const ShapelessRecipe& r : m.shapeless_) {
            for (const Ingredient& i : r.ingredients) (void)indexOf(i);
        }
        for (const TransmuteRecipe& r : m.transmute_) {
            (void)indexOf(r.input);
            (void)indexOf(r.material);
        }
        out.write(kRecipeCacheMagic, 4);
        put(out, fp);
        put(out, static_cast<std::uint32_t>(ordered.size()));
        for (const auto* items : ordered) {
            put(out, static_cast<std::uint16_t>(items->size()));
            out.write(reinterpret_cast<const char*>(items->data()), static_cast<std::streamsize>(items->size() * sizeof(std::uint16_t)));
        }
        put(out, static_cast<std::uint32_t>(m.shaped_.size()));
        for (const ShapedRecipe& r : m.shaped_) {
            putString(out, r.id);
            put(out, static_cast<std::uint8_t>(r.width));
            put(out, static_cast<std::uint8_t>(r.height));
            put(out, static_cast<std::uint8_t>(r.symmetrical));
            put(out, static_cast<std::uint8_t>(r.ingredientCount));
            for (const Ingredient& i : r.cells) put(out, indexOf(i));
            putResult(out, r.result);
        }
        put(out, static_cast<std::uint32_t>(m.shapeless_.size()));
        for (const ShapelessRecipe& r : m.shapeless_) {
            putString(out, r.id);
            put(out, static_cast<std::uint8_t>(r.ingredients.size()));
            for (const Ingredient& i : r.ingredients) put(out, indexOf(i));
            putResult(out, r.result);
        }
        put(out, static_cast<std::uint32_t>(m.transmute_.size()));
        for (const TransmuteRecipe& r : m.transmute_) {
            putString(out, r.id);
            put(out, indexOf(r.input));
            put(out, indexOf(r.material));
            put(out, itemId(r.result));
        }
        out.close();
        if (out) {
            fs::rename(tmp, path, ec);
            if (ec) fs::remove(tmp, ec);
        }
    }

    static bool readCache(const fs::path& path, const Fingerprint& fp, RecipeManager& m) {
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {};
        Fingerprint stored;
        std::uint32_t tableSize = 0;
        if (!in.read(magic, 4) || std::memcmp(magic, kRecipeCacheMagic, 4) != 0 || !get(in, stored) || !(stored == fp) || !get(in, tableSize) || tableSize > 100000) return false;
        std::vector<Ingredient> table(tableSize + 1);
        for (std::uint32_t i = 1; i <= tableSize; ++i) {
            std::uint16_t n = 0;
            if (!get(in, n)) return false;
            table[i].items.resize(n);
            in.read(reinterpret_cast<char*>(table[i].items.data()), static_cast<std::streamsize>(n * sizeof(std::uint16_t)));
            if (!in) return false;
        }
        const auto ingredientAt = [&](std::uint32_t index, Ingredient& out) {
            if (index > tableSize) return false;
            out = table[index];
            return true;
        };
        std::uint32_t count = 0;
        if (!get(in, count) || count > 100000) return false;
        for (std::uint32_t k = 0; k < count; ++k) {
            ShapedRecipe r;
            std::uint8_t w = 0, h = 0, sym = 0, ing = 0;
            if (!getString(in, r.id) || !get(in, w) || !get(in, h) || !get(in, sym) || !get(in, ing)) return false;
            r.width = w;
            r.height = h;
            r.symmetrical = sym != 0;
            r.ingredientCount = ing;
            r.cells.resize(static_cast<std::size_t>(w * h));
            for (Ingredient& cell : r.cells) {
                std::uint32_t index = 0;
                if (!get(in, index) || !ingredientAt(index, cell)) return false;
            }
            if (!getResult(in, r.result)) return false;
            m.shaped_.push_back(std::move(r));
        }
        if (!get(in, count) || count > 100000) return false;
        for (std::uint32_t k = 0; k < count; ++k) {
            ShapelessRecipe r;
            std::uint8_t n = 0;
            if (!getString(in, r.id) || !get(in, n)) return false;
            r.ingredients.resize(n);
            for (Ingredient& cell : r.ingredients) {
                std::uint32_t index = 0;
                if (!get(in, index) || !ingredientAt(index, cell)) return false;
            }
            if (!getResult(in, r.result)) return false;
            m.shapeless_.push_back(std::move(r));
        }
        if (!get(in, count) || count > 100000) return false;
        for (std::uint32_t k = 0; k < count; ++k) {
            TransmuteRecipe r;
            std::uint32_t a = 0, b = 0;
            std::uint16_t result = 0;
            if (!getString(in, r.id) || !get(in, a) || !get(in, b) || !get(in, result) || !ingredientAt(a, r.input) || !ingredientAt(b, r.material)) return false;
            r.result = static_cast<Item>(result);
            m.transmute_.push_back(std::move(r));
        }
        return true;
    }

    // An ingredient: an item id, "#tag", or a list of them. False if it names nothing that exists.
    bool ingredient(const json& node, Ingredient& out) const {
        std::vector<std::uint16_t> items;
        const std::function<void(const json&)> add = [&](const json& n) {
            if (n.is_string()) {
                const std::string s = n.get<std::string>();
                if (!s.empty() && s[0] == '#') {
                    const auto& tagItems = m_.tags_.items(s);
                    items.insert(items.end(), tagItems.begin(), tagItems.end());
                } else if (const auto item = items_.find(s)) {
                    items.push_back(itemId(*item));
                }
            } else if (n.is_array()) {
                for (const json& e : n) add(e);
            }
        };
        add(node);
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        out.items = std::move(items);
        return !out.items.empty();
    }

    bool result(const json& node, ItemStack& out) const {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string()) return false;
        const auto item = items_.find(node["id"].get<std::string>());
        if (!item) return false;
        out = ItemStack(*item, node.value("count", 1));
        return true;
    }

    void shaped(const std::string& id, const json& doc) const {
        if (!doc.contains("pattern") || !doc["pattern"].is_array() || !doc.contains("key") || !doc["key"].is_object()) return;
        ShapedRecipe r;
        r.id = id;
        r.group = doc.value("group", std::string());
        std::vector<std::string> rows;
        for (const json& row : doc["pattern"]) {
            if (!row.is_string()) return;
            rows.push_back(row.get<std::string>());
        }
        if (rows.empty()) return;
        r.height = static_cast<int>(rows.size());
        for (const std::string& row : rows) r.width = std::max(r.width, static_cast<int>(row.size()));
        std::map<char, Ingredient> key;
        for (auto it = doc["key"].begin(); it != doc["key"].end(); ++it) {
            Ingredient ing;
            if (it.key().size() != 1 || !ingredient(it.value(), ing)) return;
            key[it.key()[0]] = std::move(ing);
        }
        r.cells.resize(static_cast<std::size_t>(r.width * r.height));
        for (int y = 0; y < r.height; ++y) {
            for (int x = 0; x < r.width; ++x) {
                const char c = x < static_cast<int>(rows[static_cast<std::size_t>(y)].size()) ? rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] : ' ';
                if (c == ' ') continue;
                const auto k = key.find(c);
                if (k == key.end()) return;
                r.cells[static_cast<std::size_t>(y * r.width + x)] = k->second;
                ++r.ingredientCount;
            }
        }
        r.symmetrical = true;
        for (int y = 0; y < r.height && r.symmetrical; ++y) {
            for (int x = 0; x < r.width; ++x) {
                if (r.cells[static_cast<std::size_t>(y * r.width + x)].items != r.cells[static_cast<std::size_t>(y * r.width + r.width - x - 1)].items) {
                    r.symmetrical = false;
                    break;
                }
            }
        }
        if (!result(doc.value("result", json()), r.result)) return;
        m_.shaped_.push_back(std::move(r));
    }

    void shapeless(const std::string& id, const json& doc) const {
        if (!doc.contains("ingredients") || !doc["ingredients"].is_array()) return;
        ShapelessRecipe r;
        r.id = id;
        r.group = doc.value("group", std::string());
        for (const json& n : doc["ingredients"]) {
            Ingredient ing;
            if (!ingredient(n, ing)) return;
            r.ingredients.push_back(std::move(ing));
        }
        if (r.ingredients.empty() || r.ingredients.size() > 9) return;
        if (!result(doc.value("result", json()), r.result)) return;
        m_.shapeless_.push_back(std::move(r));
    }

    void transmute(const std::string& id, const json& doc) const {
        TransmuteRecipe r;
        r.id = id;
        if (!doc.contains("input") || !doc.contains("material") || !ingredient(doc["input"], r.input) || !ingredient(doc["material"], r.material)) return;
        ItemStack out;
        if (!result(doc.value("result", json()), out)) return;
        r.result = out.item;
        m_.transmute_.push_back(std::move(r));
    }

    static void addShaped(RecipeManager& m, ShapedRecipe r) { m.shaped_.push_back(std::move(r)); }
    static void addShapeless(RecipeManager& m, ShapelessRecipe r) { m.shapeless_.push_back(std::move(r)); }
    static ItemTags& tags(RecipeManager& m) { return m.tags_; }

private:
    RecipeManager& m_;
    const ItemRegistry& items_;
};

std::shared_ptr<const RecipeManager> RecipeManager::loadFromGameData(const fs::path& dir, const fs::path& cacheDir, const ItemRegistry& items, std::string& error) {
    auto manager = std::make_shared<RecipeManager>();
    const auto fingerprint = RecipeBuilder::fingerprintOf(dir, items);
    const fs::path cachePath = cacheDir / "recipes.cache";
    if (fingerprint && RecipeBuilder::readCache(cachePath, *fingerprint, *manager)) {
        manager->fromGameData_ = true;
        manager->fromCache_ = true;
        return manager;
    }
    manager = std::make_shared<RecipeManager>();
    std::string tagError;
    manager->tags_ = ItemTags::load(dir, items, tagError);
    const fs::path root = dir / "data" / "minecraft" / "recipe";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        error = "no recipes in " + root.string();
        return nullptr;
    }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    RecipeBuilder builder(*manager, items);
    for (const fs::path& file : files) {
        const auto doc = readJson(file);
        if (!doc || !doc->is_object()) continue;
        const std::string type = std::string(stripNamespace(doc->value("type", std::string())));
        const std::string id = file.stem().string();
        if (type == "crafting_shaped") {
            builder.shaped(id, *doc);
        } else if (type == "crafting_shapeless") {
            builder.shapeless(id, *doc);
        } else if (type == "crafting_transmute") {
            builder.transmute(id, *doc);
        }
    }
    if (manager->recipeCount() == 0) {
        error = "no crafting recipe could be read from " + root.string();
        return nullptr;
    }
    manager->fromGameData_ = true;
    if (fingerprint) RecipeBuilder::writeCache(cachePath, *fingerprint, *manager);
    return manager;
}

std::shared_ptr<const RecipeManager> RecipeManager::makeMinimal(const ItemRegistry& items) {
    auto manager = std::make_shared<RecipeManager>();
    const auto id = [&](const char* name) { return itemId(items.find(name).value_or(Item::Air)); };
    ItemTags& tags = RecipeBuilder::tags(*manager);
    tags.define("planks", {id("oak_planks")});
    tags.define("oak_logs", {id("oak_log")});
    Ingredient planks;
    planks.items = tags.items("planks");
    Ingredient logs;
    logs.items = tags.items("oak_logs");
    const auto stack = [&](const char* name, int count) { return ItemStack(items.find(name).value_or(Item::Air), count); };

    ShapelessRecipe oakPlanks;
    oakPlanks.id = "oak_planks";
    oakPlanks.ingredients = {logs};
    oakPlanks.result = stack("oak_planks", 4);
    RecipeBuilder::addShapeless(*manager, oakPlanks);

    ShapedRecipe sticks;
    sticks.id = "stick";
    sticks.width = 1;
    sticks.height = 2;
    sticks.cells = {planks, planks};
    sticks.symmetrical = true;
    sticks.ingredientCount = 2;
    sticks.result = stack("stick", 4);
    RecipeBuilder::addShaped(*manager, sticks);

    ShapedRecipe table;
    table.id = "crafting_table";
    table.width = 2;
    table.height = 2;
    table.cells = {planks, planks, planks, planks};
    table.symmetrical = true;
    table.ingredientCount = 4;
    table.result = stack("crafting_table", 1);
    RecipeBuilder::addShaped(*manager, table);

    const auto single = [&](const char* name) {
        Ingredient i;
        i.items = {id(name)};
        return i;
    };
    ShapelessRecipe whiteDye;
    whiteDye.id = "white_dye";
    whiteDye.ingredients = {single("bone_meal")};
    whiteDye.result = stack("white_dye", 1);
    RecipeBuilder::addShapeless(*manager, whiteDye);

    ShapelessRecipe magenta;
    magenta.id = "magenta_dye_from_blue_red_pink";
    magenta.ingredients = {single("blue_dye"), single("red_dye"), single("pink_dye")};
    magenta.result = stack("magenta_dye", 3);
    RecipeBuilder::addShapeless(*manager, magenta);
    return manager;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
// The session installs the recipes from a loading thread while the main thread already plays.
std::mutex g_recipesMutex;
std::shared_ptr<const RecipeManager> g_recipes;
}  // namespace

void installRecipeManager(std::shared_ptr<const RecipeManager> manager) {
    std::lock_guard<std::mutex> lock(g_recipesMutex);
    g_recipes = std::move(manager);
}
const RecipeManager* recipeManager() {
    std::lock_guard<std::mutex> lock(g_recipesMutex);
    return g_recipes.get();
}
std::shared_ptr<const RecipeManager> currentRecipeManager() {
    std::lock_guard<std::mutex> lock(g_recipesMutex);
    return g_recipes;
}

}  // namespace apex
