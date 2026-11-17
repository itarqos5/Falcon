// The advancements (game/advancements.hpp): the tree is read from the game's data files, laid out, and checked against what the player has. The data is made here, in a folder of
// its own, in the shape of the game's: a root, a child that wants an item of a tag, and a grandchild that wants the Nether.
#include "falcon/game/advancements.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (false)

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

int main() {
    using falcon::AdvancementContext;
    using falcon::AdvancementDef;
    using falcon::AdvancementTree;
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "falcon_advancement_test";
    std::filesystem::remove_all(data);
    const auto adv = data / "minecraft" / "advancement" / "story";
    write(adv / "root.json", R"({"criteria":{"crafting_table":{"conditions":{"items":[{"items":"minecraft:crafting_table"}]},"trigger":"minecraft:inventory_changed"}},
        "display":{"background":"minecraft:gui/advancements/backgrounds/stone","icon":{"id":"minecraft:grass_block"},"title":{"translate":"t"},"description":{"translate":"d"}},
        "requirements":[["crafting_table"]]})");
    write(adv / "mine_stone.json", R"({"parent":"minecraft:story/root","criteria":{"get_stone":{"conditions":{"items":[{"items":"#minecraft:stone_tool_materials"}]},"trigger":"minecraft:inventory_changed"}},
        "display":{"icon":{"id":"minecraft:wooden_pickaxe"},"title":{"translate":"t"},"description":{"translate":"d"}},"requirements":[["get_stone"]]})");
    write(adv / "enter_nether.json", R"({"parent":"minecraft:story/mine_stone","criteria":{"entered":{"conditions":{"to":"minecraft:the_nether"},"trigger":"minecraft:changed_dimension"}},
        "display":{"frame":"task","icon":{"id":"minecraft:flint_and_steel"},"title":{"translate":"t"},"description":{"translate":"d"}},"requirements":[["entered"]]})");
    write(adv / "recipe_only.json", R"({"criteria":{"x":{"trigger":"minecraft:tick"}}})");  // (no display: not an advancement of the screen)
    write(data / "minecraft" / "tags" / "item" / "stone_tool_materials.json", R"({"values":["minecraft:cobblestone","minecraft:blackstone"]})");

    const auto tree = AdvancementTree::load(data);
    CHECK(tree->all().size() == 3);
    CHECK(tree->roots().size() == 1);
    const int root = tree->indexOf("story/root"), stone = tree->indexOf("story/mine_stone"), nether = tree->indexOf("story/enter_nether");
    CHECK(root >= 0 && stone >= 0 && nether >= 0);
    CHECK(tree->indexOf("story/recipe_only") == -1);
    if (root < 0 || stone < 0 || nether < 0) return 1;
    // A chain: one step across for each step down, all in one row.
    CHECK(tree->all()[static_cast<std::size_t>(stone)].x == 1.0f && tree->all()[static_cast<std::size_t>(nether)].x == 2.0f);
    CHECK(tree->all()[static_cast<std::size_t>(root)].y == 0.0f && tree->all()[static_cast<std::size_t>(nether)].y == 0.0f);
    CHECK(tree->all()[static_cast<std::size_t>(root)].background == "gui/advancements/backgrounds/stone");
    CHECK(tree->all()[static_cast<std::size_t>(stone)].titleKey() == "advancements.story.mine_stone.title");
    CHECK(tree->tabOf(tree->roots()[0]).size() == 3);

    std::set<std::string> progress;
    std::set<std::string> told;
    const auto onDone = [&](const AdvancementDef& d) { told.insert(d.id); };
    // Nothing held, nowhere: nothing is done.
    tree->evaluate(progress, AdvancementContext{}, onDone);
    CHECK(progress.empty() && told.empty());
    CHECK(tree->visible(root, progress) && tree->visible(stone, progress) && !tree->visible(nether, progress));
    // An item of the tag does the child (whatever its parent): the tag was read from the data.
    AdvancementContext holding;
    holding.items = {"blackstone"};
    tree->evaluate(progress, holding, onDone);
    CHECK(progress.count("story/mine_stone") == 1 && progress.count("story/root") == 0 && told.size() == 1);
    CHECK(tree->visible(nether, progress));  // (next to what is done)
    // The crafting table does the root, once: a second look says nothing new.
    holding.items.insert("crafting_table");
    tree->evaluate(progress, holding, onDone);
    tree->evaluate(progress, holding, onDone);
    CHECK(progress.count("story/root") == 1 && told.size() == 2);
    // The Nether does the last one.
    holding.dimension = "the_nether";
    tree->evaluate(progress, holding, onDone);
    CHECK(progress.count("story/enter_nether") == 1 && tree->count(tree->roots()[0], progress) == std::make_pair(3, 3));

    std::filesystem::remove_all(data);
    if (g_failures == 0) std::printf("advancement tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
