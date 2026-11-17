#include "falcon/game/advancements.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace falcon {
namespace {

std::string withoutNamespace(const std::string& id) { return id.compare(0, 10, "minecraft:") == 0 ? id.substr(10) : id; }

std::optional<nlohmann::json> readJson(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    nlohmann::json json = nlohmann::json::parse(text.str(), nullptr, false);
    if (json.is_discarded()) return std::nullopt;
    return json;
}

// The items a tag (or an item) names: "minecraft:stone" is itself, "#minecraft:logs" is every item of that tag (tags may name other tags).
void collectItems(const std::filesystem::path& dataDir, const std::string& entry, std::set<std::string>& out, int depth = 0) {
    if (entry.empty() || depth > 6) return;
    if (entry[0] != '#') {
        out.insert(withoutNamespace(entry));
        return;
    }
    const std::string name = withoutNamespace(entry.substr(1));
    for (const char* folder : {"item", "items"}) {
        const auto json = readJson(dataDir / "minecraft" / "tags" / folder / (name + ".json"));
        if (!json || !json->contains("values") || !(*json)["values"].is_array()) continue;
        for (const auto& value : (*json)["values"]) {
            if (value.is_string()) collectItems(dataDir, value.get<std::string>(), out, depth + 1);
            else if (value.is_object() && value.contains("id") && value["id"].is_string()) collectItems(dataDir, value["id"].get<std::string>(), out, depth + 1);
        }
        return;
    }
}

void collectItemsOf(const std::filesystem::path& dataDir, const nlohmann::json& value, std::set<std::string>& out) {
    if (value.is_string()) collectItems(dataDir, value.get<std::string>(), out);
    else if (value.is_array()) {
        for (const auto& v : value) {
            if (v.is_string()) collectItems(dataDir, v.get<std::string>(), out);
        }
    }
}

bool met(const AdvancementDef::Criterion& c, const AdvancementContext& context) {
    if (c.trigger == "minecraft:tick") return true;
    if (c.trigger == "minecraft:changed_dimension") return !c.dimension.empty() && c.dimension == context.dimension;
    if (c.trigger == "minecraft:inventory_changed") {
        if (c.itemSets.empty()) return false;
        for (const std::set<std::string>& wanted : c.itemSets) {
            bool found = false;
            for (const std::string& item : context.items) {
                if (wanted.count(item) != 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
    return false;  // a trigger Falcon has no events for
}

// The tidy layout of a tree: a leaf takes the next row, a parent stands in the middle of its first and last child, and every step down is one column across.
void layout(std::vector<AdvancementDef>& defs, int node, int depth, float& nextRow) {
    AdvancementDef& d = defs[static_cast<std::size_t>(node)];
    d.x = static_cast<float>(depth);
    if (d.children.empty()) {
        d.y = nextRow;
        nextRow += 1.0f;
        return;
    }
    for (const int child : d.children) layout(defs, child, depth + 1, nextRow);
    d.y = (defs[static_cast<std::size_t>(d.children.front())].y + defs[static_cast<std::size_t>(d.children.back())].y) / 2.0f;
}

}  // namespace

std::string AdvancementDef::titleKey() const {
    std::string key = "advancements.";
    for (const char c : id) key += c == '/' ? '.' : c;
    return key + ".title";
}

std::string AdvancementDef::descriptionKey() const {
    std::string key = "advancements.";
    for (const char c : id) key += c == '/' ? '.' : c;
    return key + ".description";
}

std::shared_ptr<const AdvancementTree> AdvancementTree::load(const std::filesystem::path& dataDir) {
    auto tree = std::make_shared<AdvancementTree>();
    std::error_code ec;
    // (The data folder is the one that holds minecraft/, or the one that holds data/.)
    const std::filesystem::path base = std::filesystem::is_directory(dataDir / "minecraft" / "advancement", ec) ? dataDir : dataDir / "data";
    const std::filesystem::path root = base / "minecraft" / "advancement";
    for (const char* tab : {"story", "nether", "end", "adventure", "husbandry"}) {
        const std::filesystem::path folder = root / tab;
        if (!std::filesystem::is_directory(folder, ec)) continue;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const std::filesystem::path& file : files) {
            const auto json = readJson(file);
            if (!json || !json->contains("display") || !(*json)["display"].is_object()) continue;  // (the recipes have none)
            AdvancementDef d;
            d.id = std::filesystem::relative(file, root, ec).generic_string();
            d.id = d.id.substr(0, d.id.size() - 5);
            d.tab = tab;
            if (json->contains("parent") && (*json)["parent"].is_string()) d.parent = withoutNamespace((*json)["parent"].get<std::string>());
            const nlohmann::json& display = (*json)["display"];
            if (display.contains("icon") && display["icon"].is_object() && display["icon"].contains("id")) d.icon = withoutNamespace(display["icon"]["id"].get<std::string>());
            if (display.contains("frame") && display["frame"].is_string()) d.frame = display["frame"].get<std::string>();
            if (display.contains("background") && display["background"].is_string()) d.background = withoutNamespace(display["background"].get<std::string>());
            d.hidden = display.value("hidden", false);
            if (json->contains("criteria") && (*json)["criteria"].is_object()) {
                for (const auto& [name, criterion] : (*json)["criteria"].items()) {
                    AdvancementDef::Criterion c;
                    c.trigger = criterion.value("trigger", std::string());
                    if (criterion.contains("conditions") && criterion["conditions"].is_object()) {
                        const nlohmann::json& conditions = criterion["conditions"];
                        if (conditions.contains("items") && conditions["items"].is_array()) {
                            for (const auto& predicate : conditions["items"]) {
                                if (!predicate.is_object() || !predicate.contains("items")) continue;
                                std::set<std::string> set;
                                collectItemsOf(base, predicate["items"], set);
                                if (!set.empty()) c.itemSets.push_back(std::move(set));
                            }
                        }
                        if (conditions.contains("to") && conditions["to"].is_string()) c.dimension = withoutNamespace(conditions["to"].get<std::string>());
                    }
                    d.criteria[name] = std::move(c);
                }
            }
            if (json->contains("requirements") && (*json)["requirements"].is_array()) {
                for (const auto& group : (*json)["requirements"]) {
                    std::vector<std::string> names;
                    if (group.is_array()) {
                        for (const auto& n : group) {
                            if (n.is_string()) names.push_back(n.get<std::string>());
                        }
                    }
                    d.requirements.push_back(std::move(names));
                }
            } else {
                for (const auto& [name, c] : d.criteria) d.requirements.push_back({name});  // (no list: every criterion is needed)
            }
            tree->defs_.push_back(std::move(d));
        }
    }
    // The links: children in file order, and a tab for every root.
    std::map<std::string, int> index;
    for (std::size_t i = 0; i < tree->defs_.size(); ++i) index[tree->defs_[i].id] = static_cast<int>(i);
    for (std::size_t i = 0; i < tree->defs_.size(); ++i) {
        const AdvancementDef& d = tree->defs_[i];
        const auto parent = index.find(d.parent);
        if (d.parent.empty() || parent == index.end()) tree->roots_.push_back(static_cast<int>(i));
        else tree->defs_[static_cast<std::size_t>(parent->second)].children.push_back(static_cast<int>(i));
    }
    for (const int r : tree->roots_) {
        float row = 0.0f;
        layout(tree->defs_, r, 0, row);
    }
    return tree;
}

int AdvancementTree::indexOf(const std::string& id) const {
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (defs_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> AdvancementTree::tabOf(int root) const {
    std::vector<int> out;
    std::vector<int> stack{root};
    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        out.push_back(n);
        const auto& children = defs_[static_cast<std::size_t>(n)].children;
        for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

void AdvancementTree::evaluate(std::set<std::string>& progress, const AdvancementContext& context, const std::function<void(const AdvancementDef&)>& onDone) const {
    for (const AdvancementDef& d : defs_) {
        if (progress.count(d.id) != 0) continue;
        for (const auto& [name, criterion] : d.criteria) {
            const std::string key = d.id + "#" + name;
            if (progress.count(key) == 0 && met(criterion, context)) progress.insert(key);
        }
        bool done = !d.requirements.empty();
        for (const auto& group : d.requirements) {
            bool any = false;
            for (const std::string& name : group) any = any || progress.count(d.id + "#" + name) != 0;
            done = done && any;
        }
        if (done) {
            progress.insert(d.id);
            if (onDone) onDone(d);
        }
    }
}

bool AdvancementTree::visible(int index, const std::set<std::string>& progress) const {
    const AdvancementDef& d = defs_[static_cast<std::size_t>(index)];
    if (progress.count(d.id) != 0) return true;
    if (d.hidden) return false;
    if (d.parent.empty()) return true;
    if (progress.count(d.parent) != 0) return true;
    const int p = indexOf(d.parent);
    if (p < 0) return true;
    const AdvancementDef& parent = defs_[static_cast<std::size_t>(p)];
    return parent.parent.empty() || progress.count(parent.parent) != 0;  // (a root's children show at once; the others two steps from what is done)
}

std::pair<int, int> AdvancementTree::count(int root, const std::set<std::string>& progress) const {
    int done = 0, total = 0;
    for (const int n : tabOf(root)) {
        ++total;
        done += progress.count(defs_[static_cast<std::size_t>(n)].id) != 0 ? 1 : 0;
    }
    return {done, total};
}

}  // namespace falcon
