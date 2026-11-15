// D:/Projects/Falcon/tests/items tests: recipes real.
#include "test_items_support.hpp"

std::string craft(const RecipeManager& recipes, int w, int h, const std::vector<ItemStack>& grid) {
    const auto match = recipes.find(CraftingInput::fromGrid(w, h, grid));
    return match ? describe(match->result) : "-";
}

void test_recipes_minimal() {
    const auto recipes = RecipeManager::makeMinimal(itemRegistry());
    std::vector<ItemStack> g(4);
    setOne(g, 0, "oak_log");
    CHECK(craft(*recipes, 2, 2, g) == "oak_planksx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 1, "blue_dye");
    setOne(g, 2, "red_dye");
    setOne(g, 3, "pink_dye");
    CHECK(craft(*recipes, 2, 2, g) == "magenta_dyex3");
    g[1] = ItemStack();  // missing one dye: no match
    CHECK(craft(*recipes, 2, 2, g) == "-");
    g = std::vector<ItemStack>(4);
    setOne(g, 3, "bone_meal");
    CHECK(craft(*recipes, 2, 2, g) == "white_dyex1");
    // the grid cut to its box
    const CraftingInput in = CraftingInput::fromGrid(2, 2, std::vector<ItemStack>{ItemStack(), S("stone"), ItemStack(), S("stone")});
    CHECK(in.width == 1 && in.height == 2 && in.left == 1 && in.top == 0 && in.ingredientCount == 2);
    CHECK(CraftingInput::fromGrid(2, 2, std::vector<ItemStack>(4)).ingredientCount == 0);
}

std::uint64_t countOccurrences(const std::filesystem::path& dir, const std::string& needle) {
    std::uint64_t n = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::ifstream in(it->path());
        std::stringstream ss;
        ss << in.rdbuf();
        if (ss.str().find(needle) != std::string::npos) ++n;
    }
    return n;
}

void test_creative_tabs_real(const std::filesystem::path& dataDir, const ItemRegistry& items);
void test_player_preview_real(const std::filesystem::path& assetsRoot);
void test_item_names_real(const std::filesystem::path& assetsRoot);
void test_item_icons_real(const std::filesystem::path& assetsRoot, const std::shared_ptr<const BlockRegistry>& blocks);

// Plants and two-block blocks: what keeps them standing (BlockSupport), from the game's own class and tag files.
void test_block_support_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks) {
    std::string error;
    const auto support = BlockSupport::loadFromGameData(dataDir, blocks, error);
    CHECK(support != nullptr);
    if (!support) {
        std::printf("  block support failed: %s\n", error.c_str());
        return;
    }
    const auto b = [&](const char* text) {
        const auto s = blocks.parse(text);
        CHECK(s.has_value());
        return s.value_or(Block::Air);
    };
    std::map<std::array<int, 3>, Block> world;
    const BlockSupport::Reader at = [&](int x, int y, int z) {
        const auto it = world.find({x, y, z});
        return it == world.end() ? Block::Air : it->second;
    };
    const Block air = Block::Air;
    const Block lower = b("tall_grass[half=lower]"), upper = b("tall_grass[half=upper]");
    CHECK(support->isDoubleHalf(lower) && support->isLowerHalf(lower) && !support->isLowerHalf(upper) && support->upperHalf(lower) == upper);
    CHECK(!support->isDoubleHalf(b("oak_stairs[half=top]")) && !support->isDoubleHalf(b("stone")) && support->isDoubleHalf(b("oak_door[half=upper]")));
    CHECK(support->isDoubleHalf(b("sunflower[half=lower]")) && support->isDoubleHalf(b("rose_bush[half=upper]")));

    // Soil: grass, dirt and farmland carry flowers; stone and sand do not; crops want farmland; dead bushes like sand.
    world[{0, 0, 0}] = b("grass_block");
    world[{5, 0, 0}] = b("stone");
    world[{6, 0, 0}] = b("sand");
    world[{7, 0, 0}] = b("farmland");
    CHECK(support->canSurvive(b("poppy"), 0, 1, 0, at) && !support->canSurvive(b("poppy"), 5, 1, 0, at) && !support->canSurvive(b("poppy"), 6, 1, 0, at));
    CHECK(support->canSurvive(b("short_grass"), 0, 1, 0, at) && support->canSurvive(b("oak_sapling"), 0, 1, 0, at) && support->canSurvive(lower, 0, 1, 0, at));
    CHECK(!support->canSurvive(lower, 5, 1, 0, at) && !support->canSurvive(b("wheat"), 0, 1, 0, at) && support->canSurvive(b("wheat"), 7, 1, 0, at));
    CHECK(support->canSurvive(b("dead_bush"), 6, 1, 0, at) && !support->canSurvive(b("dead_bush"), 5, 1, 0, at));
    CHECK(support->canSurvive(b("stone"), 5, 1, 0, at) && support->canSurvive(b("torch"), 5, 1, 0, at));  // not a plant: left alone

    // A tall plant on the grass: both halves stay while both are there.
    world[{0, 1, 0}] = lower;
    world[{0, 2, 0}] = upper;
    CHECK(support->afterNeighborChange(lower, 0, 1, 0, at) == lower && support->afterNeighborChange(upper, 0, 2, 0, at) == upper);
    // The bottom half is broken: the top half goes (this was the bug: it stayed floating in the air).
    world[{0, 1, 0}] = air;
    CHECK(support->afterNeighborChange(upper, 0, 2, 0, at) == air);
    // The top half is broken: the bottom half goes.
    world[{0, 1, 0}] = lower;
    world[{0, 2, 0}] = air;
    CHECK(support->afterNeighborChange(lower, 0, 1, 0, at) == air);
    // The ground under a tall plant is broken: its lower half goes, then (on that change) its upper half.
    world[{0, 2, 0}] = upper;
    world[{0, 0, 0}] = air;
    CHECK(support->afterNeighborChange(lower, 0, 1, 0, at) == air);
    world[{0, 1, 0}] = air;
    CHECK(support->afterNeighborChange(upper, 0, 2, 0, at) == air);
    // A flower whose soil goes pops off; a stone block never does; air stays air.
    world[{0, 1, 0}] = b("poppy");
    CHECK(support->afterNeighborChange(b("poppy"), 0, 1, 0, at) == air);
    CHECK(support->afterNeighborChange(b("stone"), 0, 1, 0, at) == b("stone") && support->afterNeighborChange(air, 0, 1, 0, at) == air);
    // A door is two blocks as well, and does not care about soil.
    const Block doorLower = b("oak_door[half=lower]"), doorUpper = b("oak_door[half=upper]");
    world.clear();
    world[{0, 1, 0}] = doorLower;
    CHECK(support->afterNeighborChange(doorLower, 0, 1, 0, at) == air);  // its upper half is missing
    world[{0, 2, 0}] = doorUpper;
    CHECK(support->afterNeighborChange(doorLower, 0, 1, 0, at) == doorLower && support->afterNeighborChange(doorUpper, 0, 2, 0, at) == doorUpper);
}

void test_item_registry_and_recipes_real(const std::filesystem::path& dataDir) {
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const auto blocks = BlockRegistry::loadFromGameData(dataDir, std::filesystem::temp_directory_path() / "falcon_items_test_cache", error);
    if (!blocks) {
        std::printf("  (item tests with the game data skipped: %s)\n", error.c_str());
        return;
    }
    const std::filesystem::path itemCache = std::filesystem::temp_directory_path() / "falcon_items_test_cache";
    std::error_code ignored;
    std::filesystem::remove(itemCache / "items.cache", ignored);
    std::filesystem::remove(itemCache / "recipes.cache", ignored);
    const auto itemsStart = std::chrono::steady_clock::now();
    const auto items = ItemRegistry::loadFromGameData(dataDir, itemCache, blocks.get(), error);
    std::printf("  item registry from items.json: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - itemsStart).count());
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(items != nullptr);
    if (!items) {
        std::printf("  item registry failed: %s\n", error.c_str());
        return;
    }
    std::printf("  item registry + block registry (cached) loaded in %.0f ms\n", loadMs);
    const auto itemStart = std::chrono::steady_clock::now();
    std::string error2;
    const auto again = ItemRegistry::loadFromGameData(dataDir, itemCache, blocks.get(), error2);
    std::printf("  item registry from items.cache: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - itemStart).count());
    CHECK(again != nullptr && again->size() == items->size());
    if (again) {
        bool same = true;
        for (std::size_t i = 0; i < items->size(); ++i) {
            const ItemType& a = items->items()[i];
            const ItemType& b = again->items()[i];
            if (a.name != b.name || a.maxStack != b.maxStack || a.maxDamage != b.maxDamage || a.translationKey != b.translationKey || a.itemModel != b.itemModel ||
                a.rarity != b.rarity || a.food.has_value() != b.food.has_value() || a.toolRules.size() != b.toolRules.size() || a.armor != b.armor ||
                a.equipSlot != b.equipSlot || a.fuelBurnTicks != b.fuelBurnTicks || a.blockIndex != b.blockIndex || a.attackDamageBonus != b.attackDamageBonus) {
                same = false;
            }
        }
        CHECK(same);
    }

    CHECK(items->isFromGameData() && items->size() == 1658);
    const auto get = [&](const char* name) -> const ItemType& { return items->item(items->find(name).value()); };
    CHECK(get("air").protocolId == 0 && get("stone").protocolId == 1 && get("diamond_pickaxe").protocolId == 1052 && get("apple").protocolId == 1007);
    CHECK(get("stone").maxStack == 64 && get("stone").maxDamage == 0 && get("stone").translationKey == "block.minecraft.stone");
    CHECK(get("diamond_pickaxe").maxDamage == 1561 && get("diamond_pickaxe").maxStack == 1 && get("diamond_pickaxe").isTool);
    CHECK(std::abs(get("diamond_pickaxe").attackDamage() - 5.0f) < 1e-5f && std::abs(get("diamond_pickaxe").attackSpeed() - 1.2f) < 1e-3f);
    CHECK(get("diamond_pickaxe").toolRules.size() == 2 && get("diamond_pickaxe").toolRules[1].blocks == "#minecraft:mineable/pickaxe" &&
          get("diamond_pickaxe").toolRules[1].hasSpeed && std::abs(get("diamond_pickaxe").toolRules[1].speed - 8.0f) < 1e-5f);
    CHECK(get("diamond_sword").toolDamagePerBlock == 2 && get("diamond_sword").maxDamage == 1561);
    CHECK(get("apple").food && get("apple").food->nutrition == 4 && std::abs(get("apple").food->saturation - 2.4f) < 1e-5f && !get("apple").food->canAlwaysEat);
    CHECK(get("golden_apple").food && get("golden_apple").food->canAlwaysEat && get("stone").food == std::nullopt);
    CHECK(std::abs(get("iron_chestplate").armor - 6.0f) < 1e-5f && get("iron_chestplate").armorSlot == EquipSlot::Chest &&
          get("iron_chestplate").equipSlot == EquipSlot::Chest && get("iron_chestplate").maxDamage == 240);
    CHECK(get("iron_helmet").equipSlot == EquipSlot::Head && get("iron_boots").equipSlot == EquipSlot::Feet && get("iron_leggings").equipSlot == EquipSlot::Legs);
    CHECK(get("shield").equipSlot == EquipSlot::OffHand && get("shield").maxDamage == 336);
    CHECK(get("ender_pearl").maxStack == 16 && get("bucket").maxStack == 16 && get("water_bucket").maxStack == 1 && get("oak_sign").maxStack == 16);
    CHECK(get("enchanted_golden_apple").rarity == Rarity::Rare && get("elytra").rarity == Rarity::Epic && get("stone").rarity == Rarity::Common);
    CHECK(get("coal").fuelBurnTicks == 1600 && get("oak_planks").fuelBurnTicks == 300 && get("stick").fuelBurnTicks == 100 &&
          get("lava_bucket").fuelBurnTicks == 20000 && get("oak_boat").fuelBurnTicks == 1200 && get("stone").fuelBurnTicks == 0);
    CHECK(get("stick").translationKey == "item.minecraft.stick" && get("stick").itemModel == "stick");
    // Block items know their block, and the other way round.
    CHECK(get("stone").blockIndex == blocks->findBlock("stone").value() && get("oak_stairs").blockIndex == blocks->findBlock("oak_stairs").value());
    CHECK(get("diamond_pickaxe").blockIndex == -1 && get("torch").blockIndex == blocks->findBlock("torch").value());
    CHECK(items->itemOfBlock(blocks->findBlock("wall_torch").value()) == items->find("torch").value());
    CHECK(items->itemOfBlock(blocks->findBlock("stone").value()) == items->find("stone").value() && items->itemOfBlock(blocks->findBlock("air").value()) == Item::Air);
    CHECK(items->blockOf(items->find("grass_block").value()) == blocks->findBlock("grass_block"));
    CHECK(!items->find("not_an_item") && items->find("minecraft:apple") == items->find("apple"));

    installBlockRegistry(blocks);
    installItemRegistry(items);

    // Recipes from the game's own files.
    const auto recipeStart = std::chrono::steady_clock::now();
    std::string recipeError;
    const auto recipes = RecipeManager::loadFromGameData(dataDir, itemCache, *items, recipeError);
    const double recipeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recipeStart).count();
    CHECK(recipes != nullptr);
    if (!recipes) {
        std::printf("  recipes failed: %s\n", recipeError.c_str());
        return;
    }
    std::printf("  %zu crafting recipes + %zu item tags loaded in %.0f ms\n", recipes->recipeCount(), recipes->tags().size(), recipeMs);
    const std::filesystem::path recipeDir = dataDir / "data" / "minecraft" / "recipe";
    CHECK(recipes->shaped().size() == countOccurrences(recipeDir, "\"minecraft:crafting_shaped\""));
    CHECK(recipes->shapeless().size() == countOccurrences(recipeDir, "\"minecraft:crafting_shapeless\""));
    CHECK(recipes->transmute().size() + 1 == countOccurrences(recipeDir, "\"minecraft:crafting_transmute\""));  // (map_cloning has no fixed result)
    CHECK(recipes->tags().contains("planks") && recipes->tags().items("minecraft:planks").size() >= 12);
    CHECK(recipes->tags().items("logs").size() > 40);  // nested tags are resolved

    const auto c2 = [&](std::vector<ItemStack> g) { return craft(*recipes, 2, 2, g); };
    std::vector<ItemStack> g(4);
    setOne(g, 0, "oak_log");
    CHECK(c2(g) == "oak_planksx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 3, "spruce_log");
    CHECK(c2(g) == "spruce_planksx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 2, "stripped_birch_wood");
    CHECK(c2(g) == "birch_planksx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "oak_planks");
    setOne(g, 2, "oak_planks");
    CHECK(c2(g) == "stickx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 1, "birch_planks");
    setOne(g, 3, "spruce_planks");  // any planks make sticks
    CHECK(c2(g) == "stickx4");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "oak_planks");
    setOne(g, 1, "birch_planks");
    setOne(g, 2, "spruce_planks");
    setOne(g, 3, "oak_planks");
    CHECK(c2(g) == "crafting_tablex1");
    g[3] = ItemStack();
    CHECK(c2(g) == "-");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "oak_planks");
    setOne(g, 3, "oak_planks");  // a diagonal pair: nothing
    CHECK(c2(g) == "-");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "oak_planks");
    setOne(g, 1, "oak_planks");  // a row of two: a pressure plate
    CHECK(c2(g) == "oak_pressure_platex1");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "blue_dye");
    setOne(g, 1, "red_dye");
    setOne(g, 3, "pink_dye");
    CHECK(c2(g) == "magenta_dyex3");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "bone_meal");
    CHECK(c2(g) == "white_dyex1");
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "red_dye");
    setOne(g, 1, "red_dye");
    CHECK(c2(g) == "-");  // two red dyes: no recipe uses them
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "gold_ingot");
    setOne(g, 1, "gold_ingot");
    setOne(g, 2, "gold_ingot");
    setOne(g, 3, "gold_ingot");
    CHECK(c2(g) == "-");

    // 3x3 recipes (for the crafting table to come): shapes and mirroring.
    std::vector<ItemStack> g3(9);
    for (int i : {0, 1, 2, 3, 5, 6, 7, 8}) setOne(g3, i, "oak_planks");
    CHECK(craft(*recipes, 3, 3, g3) == "chestx1");
    g3 = std::vector<ItemStack>(9);
    for (int i : {0, 1, 2}) setOne(g3, i, "cobblestone");
    setOne(g3, 4, "stick");
    setOne(g3, 7, "stick");
    CHECK(craft(*recipes, 3, 3, g3) == "stone_pickaxex1");
    g3 = std::vector<ItemStack>(9);
    for (int i : {0, 3, 4, 6, 7, 8}) setOne(g3, i, "oak_planks");  // stairs, one way
    CHECK(craft(*recipes, 3, 3, g3).rfind("oak_stairsx4", 0) == 0);
    g3 = std::vector<ItemStack>(9);
    for (int i : {2, 4, 5, 6, 7, 8}) setOne(g3, i, "oak_planks");  // the mirrored way
    CHECK(craft(*recipes, 3, 3, g3).rfind("oak_stairsx4", 0) == 0);
    g3 = std::vector<ItemStack>(9);
    for (int i : {0, 1, 2, 3, 5, 6, 7, 8}) setOne(g3, i, "cobblestone");
    CHECK(craft(*recipes, 3, 3, g3) == "furnacex1");
    g3 = std::vector<ItemStack>(9);  // a chest ring with a hole in it: no
    for (int i : {0, 1, 2, 3, 5, 6, 7}) setOne(g3, i, "oak_planks");
    CHECK(craft(*recipes, 3, 3, g3) == "-");
    // transmute: a dye and a bundle
    g = std::vector<ItemStack>(4);
    setOne(g, 0, "white_bundle");
    setOne(g, 1, "red_dye");
    CHECK(c2(g) == "red_bundlex1");

    // The cached copy has the same recipes and finds the same results.
    {
        const auto cacheStart = std::chrono::steady_clock::now();
        std::string cacheError;
        const auto cached = RecipeManager::loadFromGameData(dataDir, itemCache, *items, cacheError);
        std::printf("  recipes from recipes.cache: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cacheStart).count());
        CHECK(cached && cached->isFromCache() && cached->recipeCount() == recipes->recipeCount() && cached->shaped().size() == recipes->shaped().size());
        if (cached) {
            std::vector<ItemStack> cg(4);
            setOne(cg, 0, "oak_planks");
            setOne(cg, 2, "oak_planks");
            CHECK(craft(*cached, 2, 2, cg) == "stickx4");
            std::vector<ItemStack> tg(9);
            for (int i : {0, 1, 2, 3, 5, 6, 7, 8}) setOne(tg, i, "oak_planks");
            CHECK(craft(*cached, 3, 3, tg) == "chestx1");
            bool same = true;
            for (std::size_t i = 0; i < recipes->shaped().size(); ++i) {
                const ShapedRecipe& a = recipes->shaped()[i];
                const ShapedRecipe& b = cached->shaped()[i];
                if (a.id != b.id || a.width != b.width || a.height != b.height || a.symmetrical != b.symmetrical || a.result.item != b.result.item || a.result.count != b.result.count) same = false;
                for (std::size_t c = 0; c < a.cells.size() && same; ++c) same = a.cells[c].items == b.cells[c].items;
            }
            CHECK(same);
        }
    }

    // The inventory menu crafts from the real recipes too.
    installRecipeManager(recipes);
    {
        Rig r;
        r.put(1, S("birch_log", 2));
        CHECK_SLOT(r, kRes, "birch_planksx4");
        r.click(kRes, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 1, "-");
        CHECK(r.inventory.countOf(itemNamed("birch_planks")) == 8);
    }
    // The creative tabs, the names and the pictures from the game's files.
    test_creative_tabs_real(dataDir, *items);
    test_block_support_real(dataDir, *blocks);
    test_player_preview_real(dataDir / "assets");
    test_item_names_real(dataDir / "assets");
    test_item_icons_real(dataDir / "assets", blocks);
}

// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
// The hotbar's fading item name
