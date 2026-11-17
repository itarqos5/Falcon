#pragma once

// The advancements of the game, read from its data (data/minecraft/advancement/{story,adventure,husbandry,nether,end}/*.json): what each is called and looks like, which one it
// follows, and what it asks for. The screen (progress_screens.hpp) draws them as trees, one tab for each root; GameSession checks them against what the player has.
//
// What Falcon can check: the criteria that look at the inventory (minecraft:inventory_changed: items and tags of items), the world the player is in (changed_dimension) and the ones
// that are met at once (tick). The rest of the game's triggers (kills, trades, potions, structures ...) need things Falcon does not have yet: those advancements are shown, and
// stay locked.

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace falcon {

struct AdvancementDef {
    std::string id;           // "story/mine_stone"
    std::string parent;       // "" for the root of a tab
    std::string tab;          // the folder: "story"
    std::string icon;         // "cobblestone" (no "minecraft:")
    std::string frame = "task";  // task, goal or challenge
    std::string background;   // roots: the picture of the tab, "gui/advancements/backgrounds/stone"
    bool hidden = false;      // not shown until it is done
    // One criterion: its trigger and what it asks for. `itemSets`: for inventory_changed, a set of item names for each thing that must be in the inventory.
    struct Criterion {
        std::string trigger;
        std::vector<std::set<std::string>> itemSets;
        std::string dimension;  // changed_dimension: the world gone to ("the_nether")
    };
    std::map<std::string, Criterion> criteria;
    std::vector<std::vector<std::string>> requirements;  // all of these groups, in each one of its criteria
    float x = 0.0f, y = 0.0f;  // where it stands in its tab (x: how far from the root, y: the row)
    std::vector<int> children;

    [[nodiscard]] std::string titleKey() const;        // advancements.story.mine_stone.title
    [[nodiscard]] std::string descriptionKey() const;
};

// What the player has and where, for the criteria.
struct AdvancementContext {
    std::set<std::string> items;  // the names of the items in the inventory ("cobblestone")
    std::string dimension = "overworld";
};

class AdvancementTree {
public:
    // Reads the advancements under `dataDir` (the folder that holds minecraft/). Missing data is an empty tree.
    [[nodiscard]] static std::shared_ptr<const AdvancementTree> load(const std::filesystem::path& dataDir);

    [[nodiscard]] const std::vector<AdvancementDef>& all() const { return defs_; }
    [[nodiscard]] const std::vector<int>& roots() const { return roots_; }  // one for each tab, story first
    [[nodiscard]] int indexOf(const std::string& id) const;               // -1 when there is none
    // The advancements of a root's tab, the root first.
    [[nodiscard]] std::vector<int> tabOf(int root) const;

    // Checks every advancement that is not done against `context`. `progress` holds the ids of what is done ("story/mine_stone") and of the criteria that are met
    // ("story/mine_stone#cobblestone"); what becomes done is added to it and told to `onDone`.
    void evaluate(std::set<std::string>& progress, const AdvancementContext& context, const std::function<void(const AdvancementDef&)>& onDone) const;
    // Whether the advancement shows in its tab: a root does, a done one does, and so do the ones next to what is done (vanilla shows two steps ahead).
    [[nodiscard]] bool visible(int index, const std::set<std::string>& progress) const;
    // How many of the tab's advancements are done, and how many there are.
    [[nodiscard]] std::pair<int, int> count(int root, const std::set<std::string>& progress) const;

private:
    std::vector<AdvancementDef> defs_;
    std::vector<int> roots_;
};

}  // namespace falcon
