// D:/Projects/Falcon/tests/items tests (the others are in D:/Projects/Falcon/tests/test_items_*.cpp).
#include "test_items_support.hpp"

void test_hotbar_highlight() {
    SelectedItemHighlight h;
    CHECK(h.alpha() == 0 && h.timer() == 0);
    h.tick(S("stone"));  // a new item: 40 ticks
    CHECK(h.timer() == 40 && h.alpha() == 255);
    for (int i = 0; i < 30; ++i) h.tick(S("stone"));
    CHECK(h.timer() == 10 && h.alpha() == 255);  // fully opaque until 10 ticks are left
    h.tick(S("stone"));
    CHECK(h.timer() == 9 && h.alpha() == 230);   // 9 * 256 / 10 = 230.4
    for (int i = 0; i < 4; ++i) h.tick(S("stone"));
    CHECK(h.timer() == 5 && h.alpha() == 128);
    for (int i = 0; i < 5; ++i) h.tick(S("stone"));
    CHECK(h.timer() == 0 && h.alpha() == 0);
    h.tick(S("stone"));
    CHECK(h.timer() == 0);                      // it stays hidden while the same item is held
    h.tick(S("dirt", 3));                       // another item starts it again
    CHECK(h.timer() == 40);
    h.tick(S("dirt", 60));                      // the count does not matter
    CHECK(h.timer() == 39);
    h.tick(ItemStack());                        // an empty hand ends it
    CHECK(h.timer() == 0 && h.alpha() == 0);
    h.tick(S("dirt"), 0.5);                     // the option scales the time: 20 ticks
    CHECK(h.timer() == 20);
    ItemStack named = S("dirt");
    named.customName = "Mud";
    h.tick(named, 1.0);                         // a new name counts as another item
    CHECK(h.timer() == 40);
    const HotbarLayout l = computeHotbarLayout(427, 240, true);
    CHECK(l.left == 122 && l.top == 218 && l.slotX(0) == 125 && l.slotX(8) == 285 && l.slotY() == 221 && l.selectionX(3) == 181 && l.selectionY() == 217);
    CHECK(l.nameY == 181 && computeHotbarLayout(427, 240, false).nameY == 195);
    CHECK(l.offhandSpriteX == 93 && l.offhandSpriteY == 217 && l.offhandItemX == 96 && l.offhandItemY == 221);
    CHECK(formatTooltipNumber(7.0) == "7" && formatTooltipNumber(1.6) == "1.6" && formatTooltipNumber(1.2000000476837158) == "1.2" && formatTooltipNumber(0.0) == "0");
}

// ---------------------------------------------------------------------------------------------------------------
// The creative inventory

struct CreativeRig {
    Rig survival;  // the player's inventory and survival menu (made with infinite materials)
    std::shared_ptr<const CreativeTabs> tabs;
    std::unique_ptr<CreativeInventory> creative;
    CreativeRig() : survival(true) {
        tabs = CreativeTabs::makeMinimal(itemRegistry());
        creative = std::make_unique<CreativeInventory>(*survival.menu, survival.menu->player(), *tabs);
    }
    // A click on slot number \`slot\` of the open tab's slots.
    void click(int slot, int button = 0, ClickType type = ClickType::Pickup, bool outside = false) {
        const auto views = creative->slotViews();
        for (const SlotView& v : views) {
            if (v.slot->index == slot) {
                creative->slotClicked(&v, v.slot->index, button, type, false);
                return;
            }
        }
        creative->slotClicked(nullptr, slot, button, type, outside);
    }
    [[nodiscard]] std::string carried() { return describe(creative->activeMenu().carried()); }
    [[nodiscard]] std::string palette(int slot) { return describe(creative->picker().palette().item(slot)); }
    [[nodiscard]] std::string hotbar(int slot) { return describe(survival.inventory.item(slot)); }
};

void test_creative_logic() {
    CreativeRig r;
    CHECK(r.creative->tab().id == "minecraft:building_blocks" && !r.creative->isInventoryTab());
    CHECK(r.palette(0) == "stonex1" && r.palette(1) == "dirtx1" && r.palette(7) == "sandstonex1" && r.palette(8) == "-");
    // Left click takes one item, shift a full stack; the middle button a full stack too.
    r.click(0);
    CHECK(r.carried() == "stonex1");
    r.click(0);  // the cursor holds the same item: one more per click
    CHECK(r.carried() == "stonex2");
    r.click(0, 1);  // the right button puts one back
    CHECK(r.carried() == "stonex1");
    r.click(0, 0, ClickType::QuickMove);  // shift: a full stack
    CHECK(r.carried() == "stonex64");
    r.click(1);  // another item of the grid: the cursor stack is put away (deleted)
    CHECK(r.carried() == "-");
    r.click(2, 0, ClickType::QuickMove);
    CHECK(r.carried() == "cobblestonex64");
    r.click(1, 1);  // the right button on another item takes one off the cursor stack
    CHECK(r.carried() == "cobblestonex63");
    r.click(CreativePickerMenu::kPaletteSlots + 8);  // an empty hotbar slot (picker slot 45 + 8): the cursor stack goes into it
    CHECK(r.carried() == "-" && r.hotbar(8) == "cobblestonex63");
    r.click(CreativePickerMenu::kHotbarStart + 8);
    CHECK(r.carried() == "cobblestonex63" && r.hotbar(8) == "-");  // and back
    r.click(8);  // an empty grid slot: put it away
    CHECK(r.carried() == "-");
    // Middle button: a full stack.
    r.click(3, 2, ClickType::Clone);
    CHECK(r.carried() == "oak_planksx64");
    r.click(3, 2, ClickType::Clone);  // not while the cursor holds something
    CHECK(r.carried() == "oak_planksx64");
    r.click(0, 0, ClickType::Pickup);  // another item: deleted
    CHECK(r.carried() == "-");
    // A number key over an item: a full stack into that hotbar slot.
    r.click(4, 4, ClickType::Swap);
    CHECK(r.hotbar(4) == "oak_logx64");
    // Q over an item throws one (or a stack) out of thin air.
    int before = static_cast<int>(r.survival.dropped.size());
    r.click(0, 0, ClickType::Throw);
    CHECK(static_cast<int>(r.survival.dropped.size()) == before + 1 && r.survival.dropped.back().count == 1);
    r.click(0, 1, ClickType::Throw);
    CHECK(r.survival.dropped.back().count == 64);
    // A click beside the window throws the cursor stack, the right button one of it.
    r.click(0, 0, ClickType::QuickMove);
    before = static_cast<int>(r.survival.dropped.size());
    r.creative->slotClicked(nullptr, kOutsideSlot, 1, ClickType::Pickup, true);
    CHECK(r.carried() == "stonex63" && static_cast<int>(r.survival.dropped.size()) == before + 1);
    r.creative->slotClicked(nullptr, kOutsideSlot, 0, ClickType::Pickup, true);
    CHECK(r.carried() == "-" && r.survival.dropped.back().count == 63);
    // Shift-click on a hotbar slot destroys what is in it (in the grid tabs).
    r.click(CreativePickerMenu::kHotbarStart + 4, 0, ClickType::QuickMove);
    CHECK(r.hotbar(4) == "-");
    // The survival tab: the plain menu rules, and the destroy slot.
    r.creative->selectTab(*r.tabs->find("minecraft:inventory"));
    CHECK(r.creative->isInventoryTab() && r.creative->slotViews().size() == 42);  // 4 armor, the offhand, 27 + 9, and the destroy slot
    r.survival.put(InventoryMenu::kHotbarStart, S("stone", 20));
    r.click(InventoryMenu::kHotbarStart);
    CHECK(r.carried() == "stonex20");
    r.click(99);  // the destroy slot: the cursor stack is gone
    CHECK(r.carried() == "-");
    r.survival.put(InventoryMenu::kHotbarStart + 1, S("dirt", 5));
    r.survival.put(10, S("stone", 2));
    r.click(99, 0, ClickType::QuickMove);  // shift: the whole inventory is emptied
    CHECK(r.hotbar(1) == "-" && describe(r.survival.inventory.item(1)) == "-");
    r.survival.put(10, S("stone", 4));
    r.click(10, 0, ClickType::Throw);
    CHECK(r.survival.at(10).count == 3);
    // Positions of the survival tab's slots.
    bool found = false;
    for (const SlotView& v : r.creative->slotViews()) {
        if (v.slot->index == 5) found = v.x == 54 && v.y == 6;
    }
    CHECK(found);
    for (const SlotView& v : r.creative->slotViews()) {
        if (v.slot->index == 8) CHECK(v.x == 108 && v.y == 33);
        if (v.slot->index == InventoryMenu::kOffhandSlot) CHECK(v.x == 35 && v.y == 20);
        if (v.slot->index == 36) CHECK(v.x == 9 && v.y == 112);
        if (v.slot->index == 9) CHECK(v.x == 9 && v.y == 54);
        if (v.slot->index == 99) CHECK(v.x == 173 && v.y == 112);
    }
    // The carried stack of the grid goes with the tab.
    r.creative->selectTab(*r.tabs->find("minecraft:building_blocks"));
    r.click(0, 0, ClickType::QuickMove);
    CHECK(r.carried() == "stonex64");
    r.creative->selectTab(*r.tabs->find("minecraft:inventory"));
    CHECK(r.carried() == "stonex64");
    r.creative->close();  // closing in the survival tab puts the stack back into the inventory
    CHECK(r.survival.inventory.countOf(itemNamed("stone")) >= 64);
    // Search: by name.
    r.creative->selectTab(*r.tabs->find("minecraft:search"));
    CHECK(r.palette(0) == "stonex1" && r.creative->picker().items().size() == 11);
    r.creative->setSearchText("stone");
    CHECK(r.creative->picker().items().size() == 4);  // stone, cobblestone, stone_bricks... and sandstone matches too
    r.creative->setSearchText("STICK");
    CHECK(r.creative->picker().items().size() == 1 && r.palette(0) == "stickx1");
    r.creative->setSearchText("nothing like this");
    CHECK(r.creative->picker().items().empty() && r.palette(0) == "-");
}

void test_creative_scrolling() {
    // 100 items in 12 rows: 7 rows of overflow.
    CreativeRig r;
    std::vector<ItemStack> items;
    for (int i = 0; i < 100; ++i) items.push_back(S(i % 2 == 0 ? "stone" : "dirt"));
    items[9 * 5] = S("apple");   // the first item of row 5
    items[9 * 7] = S("stick");   // the first item of row 7
    r.creative->picker().setItems(items);
    CHECK(r.creative->canScroll() && r.creative->rowCount() == 12);
    CHECK(r.palette(0) == "stonex1");
    r.creative->setScroll(1.0f);
    CHECK(r.creative->picker().firstRow() == 7 && r.palette(0) == "stickx1");
    r.creative->setScroll(0.0f);
    CHECK(r.creative->picker().firstRow() == 0);
    r.creative->setScroll(0.5f);  // (int)(0.5 * 7 + 0.5) = 4
    CHECK(r.creative->picker().firstRow() == 4);
    r.creative->setScroll(0.0f);
    CHECK(r.creative->scrollBy(-1.0));  // a notch down: one row = 1/7 of the bar
    CHECK(std::abs(r.creative->scrollOffs() - 1.0f / 7.0f) < 1e-5f && r.creative->picker().firstRow() == 1);
    for (int i = 0; i < 20; ++i) (void)r.creative->scrollBy(-1.0);
    CHECK(r.creative->scrollOffs() == 1.0f && r.creative->picker().firstRow() == 7);
    (void)r.creative->scrollBy(3.0);
    CHECK(r.creative->scrollOffs() < 1.0f);
    // Not enough items to scroll: the wheel does nothing.
    r.creative->picker().setItems({S("stone"), S("dirt")});
    CHECK(!r.creative->canScroll() && !r.creative->scrollBy(-1.0));
}

void test_creative_tab_parsing() {
    const auto stack = parseStackText("{count:1,id:\"minecraft:oak_log\"}", itemRegistry());
    CHECK(stack && stack->type().name == "oak_log" && stack->count == 1 && stack->components.empty());
    const auto potion = parseStackText("{components:{\"minecraft:potion_contents\":{potion:\"minecraft:weaving\"}},count:3,id:\"minecraft:apple\"}", itemRegistry());
    CHECK(potion && potion->type().name == "apple" && potion->count == 3 && potion->components == "{\"minecraft:potion_contents\":{potion:\"minecraft:weaving\"}}");
    const auto stew = parseStackText("{components:{\"minecraft:suspicious_stew_effects\":[{duration:7,id:\"minecraft:saturation\"}]},count:1,id:\"minecraft:stone\"}", itemRegistry());
    CHECK(stew && stew->type().name == "stone");  // the id inside the components is not the item's
    CHECK(!parseStackText("{count:1,id:\"minecraft:not_an_item\"}", itemRegistry()) && !parseStackText("nonsense", itemRegistry()));
}

int countJsonItems(const nlohmann::json& tab) { return tab.contains("items") && tab["items"].is_array() ? static_cast<int>(tab["items"].size()) : 0; }

void test_creative_tabs_real(const std::filesystem::path& dataDir, const ItemRegistry& items) {
    std::string error;
    const auto tabs = CreativeTabs::loadFromGameData(dataDir, items, error);
    CHECK(tabs != nullptr);
    if (!tabs) {
        std::printf("  creative tabs failed: %s\n", error.c_str());
        return;
    }
    std::ifstream in(dataDir / "creative-inventory.json");
    const nlohmann::json doc = nlohmann::json::parse(in);
    CHECK(doc["tabs"].size() == tabs->tabs().size() && tabs->tabs().size() == 14);
    int total = 0;
    for (std::size_t i = 0; i < doc["tabs"].size(); ++i) {
        const nlohmann::json& jt = doc["tabs"][i];
        const CreativeTab& t = tabs->tabs()[i];
        CHECK(t.id == jt["id"].get<std::string>() && static_cast<int>(t.entries.size()) == countJsonItems(jt));
        CHECK(!t.icon.isEmpty() && !t.translationKey.empty());
        total += static_cast<int>(t.entries.size());
        if (static_cast<int>(t.entries.size()) != countJsonItems(jt)) std::printf("  tab %s: %zu entries, file has %d\n", t.id.c_str(), t.entries.size(), countJsonItems(jt));
    }
    CHECK(tabs->defaultTab().id == "minecraft:building_blocks" && tabs->find("minecraft:search")->entries.size() == 2034);
    CHECK(tabs->find("minecraft:building_blocks")->entries.size() == 475 && tabs->find("minecraft:natural_blocks")->entries.size() == 254);
    CHECK(tabs->find("minecraft:search")->alignedRight() && tabs->find("minecraft:inventory")->alignedRight() && !tabs->find("minecraft:combat")->alignedRight());
    CHECK(tabs->find("minecraft:search")->type == CreativeTabType::Search && !tabs->find("minecraft:op_blocks")->visible && tabs->find("minecraft:combat")->visible);
    CHECK(tabs->find("minecraft:combat")->top == false && tabs->find("minecraft:building_blocks")->top && tabs->find("minecraft:hotbar")->column == 5);
    std::printf("  %zu creative tabs, %d items in them (search tab %zu)\n", tabs->tabs().size(), total, tabs->find("minecraft:search")->entries.size());

    // Searching: the tooltip text, the id, the item tags.
    const auto oak = tabs->search("oak planks");
    bool hasPlanks = false;
    for (const CreativeEntry* e : oak) hasPlanks = hasPlanks || e->stack.type().name == "oak_planks";
    CHECK(hasPlanks && oak.size() < 40 && !oak.empty());
    CHECK(tabs->search("").size() == 2034);
    CHECK(tabs->search("ZZZZ").empty());
    const auto logs = tabs->search("#logs");
    bool hasSpruceLog = false;
    for (const CreativeEntry* e : logs) hasSpruceLog = hasSpruceLog || e->stack.type().name == "spruce_log";
    CHECK(hasSpruceLog && logs.size() > 40);
    CHECK(tabs->search("minecraft:stone").size() > 3);
    // An item with components has the name of the search tab's first tooltip line.
    bool potionNamed = false;
    for (const CreativeEntry& e : tabs->find("minecraft:search")->entries) {
        if (!e.stack.components.empty() && e.stack.type().name == "potion" && e.stack.variantName == "Potion of Healing") potionNamed = true;
    }
    CHECK(potionNamed);
    CHECK(!tabs->tabsContaining(S("oak_planks")).empty() && tabs->tabsContaining(S("oak_planks"))[0]->id == "minecraft:building_blocks");
}

void test_item_names_real(const std::filesystem::path& assetsRoot) {
    const ResourceManager resources(assetsRoot);
    const Language language = Language::load(resources, "en_us");
    CHECK(itemDisplayName(S("stone"), language) == "Stone" && itemDisplayName(S("diamond_pickaxe"), language) == "Diamond Pickaxe" &&
          itemDisplayName(S("oak_planks"), language) == "Oak Planks");
    ItemStack named = S("stone");
    named.customName = "Rock";
    CHECK(itemDisplayName(named, language) == "Rock");
    const auto sword = itemTooltipLines(S("diamond_sword"), language, false);
    CHECK(sword.size() == 5 && sword[0].text == "Diamond Sword" && sword[0].argb == 0xFFFFFFFFu && sword[2].text == "When in Main Hand:" &&
          sword[3].text == " 7 Attack Damage" && sword[4].text == " 1.6 Attack Speed" && sword[3].argb == kTooltipDarkGreen);
    const auto armor = itemTooltipLines(S("iron_chestplate"), language, false);
    CHECK(armor.size() == 4 && armor[2].text == "When on Chest:" && armor[3].text == "+6 Armor" && armor[3].argb == kTooltipBlue);
    const auto apple = itemTooltipLines(S("enchanted_golden_apple"), language, false);
    CHECK(apple.size() == 1 && apple[0].argb == 0xFF55FFFFu);  // rare: aqua
    ItemStack worn = S("diamond_pickaxe");
    worn.damage = 100;
    const auto advanced = itemTooltipLines(worn, language, true);
    CHECK(advanced.back().text == "minecraft:diamond_pickaxe" && advanced.back().argb == kTooltipDarkGray && advanced[advanced.size() - 2].text == "Durability: 1461 / 1561");
    const auto withExtra = itemTooltipLines(S("stone"), language, false, {{"Building Blocks", kTooltipBlue}});
    CHECK(withExtra.size() == 2 && withExtra[1].text == "Building Blocks");
    CHECK(language.get("container.crafting") == "Crafting" && language.get("inventory.binSlot") == "Destroy Item");
}

// The player in the inventory screens: the real player model, the default skin, turned towards the mouse (PlayerPreview).
void test_player_preview_real(const std::filesystem::path& assetsRoot) {
    ResourceManager resources(assetsRoot);
    std::string error;
    const auto preview = PlayerPreview::create(resources, assetsRoot.parent_path(), error);
    CHECK(preview != nullptr);
    if (!preview) {
        std::printf("  player preview failed: %s\n", error.c_str());
        return;
    }
    // The survival inventory's box: (26, 8)-(75, 78) at scale 30; the mouse straight below the middle of the box.
    PlayerPreview::View view;
    view.x0 = 26;
    view.y0 = 8;
    view.x1 = 75;
    view.y1 = 78;
    view.guiScale = 2;
    view.scale = 30.0f;
    view.mouseX = 50.5f;
    view.mouseY = 43.0f;
    const Image image = preview->render(view);
    CHECK(image.width == 98 && image.height == 140);
    // Where the figure is: the rows and columns that have a pixel.
    int top = image.height, bottom = -1, left = image.width, right = -1, solid = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.alphaAt(x, y) == 0) continue;
            ++solid;
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            left = std::min(left, x);
            right = std::max(right, x);
        }
    }
    CHECK(solid > 3000);
    // A player is 1.8 blocks tall (the model 1.875 with the head): 30 GUI units a block, 2 pixels a unit, between the top and the feet of
    // the box, in the middle of it, and the arms are about 1.0 block (60 pixels) apart: 16 model pixels * 0.9375.
    CHECK(top > 0 && bottom < image.height - 1);
    CHECK(bottom - top > 105 && bottom - top < 125);
    CHECK(std::abs((left + right) / 2 - image.width / 2) <= 6);
    CHECK(right - left > 35 && right - left < 70);
    // The picture is the skin: the head's face is skin coloured, the shirt is the default Steve's cyan.
    const int midX = (left + right) / 2;
    const auto at = [&](int x, int y) { return &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4]; };
    const unsigned char* shirt = at(midX, top + (bottom - top) * 55 / 100);
    CHECK(shirt[3] == 255 && shirt[2] > shirt[0] && shirt[1] > shirt[0]);  // blue-green: more green and blue than red
    // Turning the mouse to the right moves the head's nose that way: the picture changes, and mirrors the other way.
    PlayerPreview::View right2 = view, left2 = view;
    right2.mouseX = 120.0f;
    left2.mouseX = -20.0f;
    const Image a = preview->render(right2), b = preview->render(left2);
    CHECK(a.rgba != image.rgba && b.rgba != image.rgba && a.rgba != b.rgba);
    // Skin options: without the hat and jacket there are fewer pixels.
    PlayerPreview::View bare = view;
    bare.parts = {false, false, false, false, false, false, false};
    const Image plain = preview->render(bare);
    int plainSolid = 0;
    for (int y = 0; y < plain.height; ++y) {
        for (int x = 0; x < plain.width; ++x) plainSolid += plain.alphaAt(x, y) != 0 ? 1 : 0;
    }
    CHECK(plainSolid > 0 && plainSolid < solid);
}

void test_item_icons_real(const std::filesystem::path& assetsRoot, const std::shared_ptr<const BlockRegistry>& blocks) {
    ResourceManager resources(assetsRoot);
    BlockModels models(resources, *blocks);
    ItemIcons icons(models);
    // 26.3's crossed plants say "shade_direction_override": "up": their quads get the brightness of an up face (full), not the
    // darker one their diagonal planes would give. A plain block's quads have none.
    {
        const auto shadeFaces = [&](const char* name) {
            std::vector<int> faces;
            const StateRender& r = models.render(*blocks->find(name));
            for (const ModelPart& part : r.parts) {
                for (const ModelVariant& v : part.variants) {
                    for (const ModelQuad& q : *v.quads) faces.push_back(q.shadeFace);
                }
            }
            return faces;
        };
        for (const char* plant : {"short_grass", "poppy", "fern", "oak_sapling"}) {
            const std::vector<int> faces = shadeFaces(plant);
            CHECK(!faces.empty());
            for (const int f : faces) CHECK(f == kUp);
        }
        for (const int f : shadeFaces("oak_stairs")) CHECK(f == -1);
    }
    // The resolver on small definitions.
    const auto parse = [](const char* text) { return nlohmann::json::parse(text); };
    {
        const auto r = resolveItemModel(parse(R"({"model":{"type":"minecraft:model","model":"minecraft:block/stone"}})"), &models);
        CHECK(r.parts.size() == 1 && r.parts[0].model == "block/stone" && !r.empty && !r.missing);
        const auto cond = resolveItemModel(parse(R"({"model":{"type":"minecraft:condition","property":"minecraft:using_item","on_false":{"type":"minecraft:model","model":"minecraft:item/bow"},"on_true":{"type":"minecraft:model","model":"minecraft:item/bow_pulling_0"}}})"), &models);
        CHECK(cond.parts.size() == 1 && cond.parts[0].model == "item/bow");
        const auto sel = resolveItemModel(parse(R"({"model":{"type":"minecraft:select","property":"minecraft:display_context","cases":[{"when":["gui","ground"],"model":{"type":"minecraft:model","model":"minecraft:item/a"}}],"fallback":{"type":"minecraft:model","model":"minecraft:item/b"}}})"), &models);
        CHECK(sel.parts.size() == 1 && sel.parts[0].model == "item/a");
        const auto sel2 = resolveItemModel(parse(R"({"model":{"type":"minecraft:select","property":"minecraft:trim_material","cases":[{"when":"minecraft:iron","model":{"type":"minecraft:model","model":"minecraft:item/c"}}],"fallback":{"type":"minecraft:model","model":"minecraft:item/b"}}})"), &models);
        CHECK(sel2.parts.size() == 1 && sel2.parts[0].model == "item/b");
        const auto range = resolveItemModel(parse(R"({"model":{"type":"minecraft:range_dispatch","property":"minecraft:time","entries":[{"threshold":3.5,"model":{"type":"minecraft:model","model":"minecraft:item/c3"}},{"threshold":0.0,"model":{"type":"minecraft:model","model":"minecraft:item/c0"}}]}})"), &models);
        CHECK(range.parts.size() == 1 && range.parts[0].model == "item/c0");
        const auto comp = resolveItemModel(parse(R"({"model":{"type":"minecraft:composite","models":[{"type":"minecraft:model","model":"minecraft:item/x"},{"type":"minecraft:model","model":"minecraft:item/y","tints":[{"type":"minecraft:constant","value":-16711936},{"type":"minecraft:dye","default":-6265536}]}]}})"), &models);
        CHECK(comp.parts.size() == 2 && comp.parts[1].tints.size() == 2 && comp.parts[1].tints[0] == 0x00FF00 && comp.parts[1].tints[1] == 0xA06540);
        const auto special = resolveItemModel(parse(R"({"model":{"type":"minecraft:special","base":"minecraft:item/template_banner","model":{"type":"minecraft:banner","color":"red"}}})"), &models);
        CHECK(special.parts.size() == 1 && special.parts[0].special == "banner" && special.parts[0].specialDetail == "red" && special.parts[0].model == "item/template_banner");
        const auto empty = resolveItemModel(parse(R"({"model":{"type":"minecraft:empty"}})"), &models);
        CHECK(empty.empty && empty.parts.empty());
        CHECK(resolveItemModel(parse("{}"), &models).missing);
    }
    // Display transforms are merged child over parent.
    const auto stone = models.itemModelInfo("block/stone");
    CHECK(stone && !stone->generated && stone->quads && stone->quads->size() == 6 && stone->guiLight == "side");
    const DisplayTransform gui = stone->transform("gui");
    CHECK(gui.present && gui.rotation[0] == 30.0f && gui.rotation[1] == 225.0f && gui.scale[0] == 0.625f);
    CHECK(stone->transform("firstperson_righthand").present && stone->transform("firstperson_lefthand").rotation[1] == 225.0f && stone->transform("ground").scale[0] == 0.25f);
    CHECK(stone->transform("thirdperson_righthand").present && stone->transform("fixed").present);
    const auto pick = models.itemModelInfo("item/diamond_pickaxe");
    CHECK(pick && pick->generated && pick->layers.size() == 1 && pick->layers[0] == "item/diamond_pickaxe" && pick->guiLight == "front");
    CHECK(pick->transform("thirdperson_righthand").rotation[2] == 55.0f);  // handheld overrides generated
    CHECK(pick->transform("ground").scale[0] == 0.5f);                      // and inherits what it does not
    CHECK(!models.itemModelInfo("item/does_not_exist"));
    const auto potion = models.itemModelInfo("item/potion");
    CHECK(potion && potion->layers.size() == 2 && potion->layers[0] == "item/potion_overlay" && potion->layers[1] == "item/potion");

    // Every item: a model is found, and a picture comes out of it.
    const ItemRegistry& items = itemRegistry();
    int flat = 0, block = 0, standIn = 0, missing = 0, emptyModel = 0, blank = 0;
    std::vector<std::string> standIns, missingNames, blankNames;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 1; i < items.size(); ++i) {
        const ItemStack stack(static_cast<Item>(i), 1);
        const std::string kind = icons.kindOf(stack);
        if (kind == "flat") ++flat;
        else if (kind == "block") ++block;
        else if (kind == "stand-in") { ++standIn; standIns.push_back(stack.type().name); }
        else if (kind == "missing") { ++missing; missingNames.push_back(stack.type().name); }
        else ++emptyModel;
        const Image image = icons.render(stack, ItemIcons::kMaxSize);
        bool any = false;
        for (std::size_t p = 3; p < image.rgba.size(); p += 4) any = any || image.rgba[p] > 0;
        if (!any && kind != "empty") { ++blank; blankNames.push_back(stack.type().name); }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  item pictures: %d flat, %d block models, %d stand-ins, %d missing, %d empty; %d blank pictures; all in %.0f ms\n", flat, block, standIn, missing, emptyModel, blank, ms);
    std::printf("  stand-ins:");
    for (const std::string& n : standIns) std::printf(" %s", n.c_str());
    std::printf("\n");
    for (const std::string& n : missingNames) std::printf("  MISSING model: %s\n", n.c_str());
    for (const std::string& n : blankNames) std::printf("  BLANK picture: %s\n", n.c_str());
    CHECK(missing == 0 && blank == 0);
    CHECK(flat + block + standIn + emptyModel == static_cast<int>(items.size()) - 1);
    CHECK(icons.kindOf(S("diamond_pickaxe")) == "flat" && icons.kindOf(S("stone")) == "block" && icons.kindOf(S("white_banner")) == "stand-in" && icons.kindOf(S("oak_stairs")) == "block");
    // Pictures made in the background are the same as the ones made at once.
    {
        ResourceManager again(assetsRoot);
        BlockModels models2(again, *blocks);
        ItemIcons background(models2);
        const char* names[] = {"stone", "diamond_pickaxe", "oak_stairs", "white_banner", "potion", "grass_block"};
        for (const char* n : names) background.prefetch(S(n));
        background.prefetch(S("stone"));  // twice is once
        background.waitForPrefetch();
        CHECK(background.readyCount() == 6);
        bool same = true;
        for (const char* n : names) same = same && background.render(S(n), ItemIcons::kMaxSize).rgba == icons.render(S(n), ItemIcons::kMaxSize).rgba;
        CHECK(same);
    }
    // A picture of stone is a cube: opaque in the middle, transparent in a corner; a pickaxe is flat.
    const Image cube = icons.render(S("stone"), 48);
    const auto alphaAt = [&](const Image& img, int x, int y) { return img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4 + 3]; };
    CHECK(alphaAt(cube, 24, 24) == 255 && alphaAt(cube, 1, 1) == 0 && alphaAt(cube, 46, 46) == 0);
    // A tinted flat item: the leather helmet's default brown shows in the picture.
    const Image helmet = icons.render(S("leather_helmet"), 48);
    bool brown = false;
    for (std::size_t p = 0; p + 3 < helmet.rgba.size(); p += 4) {
        if (helmet.rgba[p + 3] == 255 && helmet.rgba[p] > helmet.rgba[p + 2] + 40 && helmet.rgba[p] > 100) brown = true;
    }
    CHECK(brown);
    // An item whose model is not there shows the missing texture (magenta and black) on a cube, not a crash.
    CHECK(icons.kindOfModel("no_such_item_model") == "missing" && icons.kindOfModel("stone") == "block");
    const Image gone = icons.renderModel("no_such_item_model", 48);
    bool magenta = false, black = false;
    for (std::size_t p = 0; p + 3 < gone.rgba.size(); p += 4) {
        if (gone.rgba[p + 3] == 255 && gone.rgba[p] > 0xC0 && gone.rgba[p + 1] < 0x20 && gone.rgba[p + 2] > 0xC0) magenta = true;
        if (gone.rgba[p + 3] == 255 && gone.rgba[p] < 0x10 && gone.rgba[p + 1] < 0x10 && gone.rgba[p + 2] < 0x10) black = true;
    }
    CHECK(magenta && black);
    CHECK(icons.render(ItemStack(), 8).rgba[3] == 0);
}

void run_all(const std::filesystem::path* dataDir) {
    // The built-in sets first: they need no files.
    installBlockRegistry(BlockRegistry::makeMinimal());
    installItemRegistry(ItemRegistry::makeMinimal(&blockRegistry()));
    installRecipeManager(RecipeManager::makeMinimal(itemRegistry()));
    {
        const auto minimal = ItemRegistry::makeMinimal(&blockRegistry());
        CHECK(!minimal->isFromGameData() && minimal->find("stone") && minimal->find("minecraft:stone") == minimal->find("stone"));
        CHECK(minimal->item(minimal->find("diamond_pickaxe").value()).maxDamage == 1561);
        CHECK(minimal->blockOf(minimal->find("stone").value()) == blockRegistry().findBlock("stone"));
        CHECK(minimal->itemOfBlock(blockRegistry().findBlock("oak_log").value()) == minimal->find("oak_log").value());
    }
    test_item_stack_rules();
    test_inventory_add();
    test_menu_pickup_and_place();
    test_menu_armor_and_offhand();
    test_menu_quick_move();
    test_menu_swap_and_throw();
    test_menu_drag();
    test_menu_double_click();
    test_menu_crafting();
    test_menu_misc();
    test_recipes_minimal();
    test_hotbar_highlight();
    test_creative_logic();
    test_creative_scrolling();
    test_creative_tab_parsing();
    if (dataDir != nullptr) test_item_registry_and_recipes_real(*dataDir);
}


int main() {
    const auto assets = falcon::locateAssets(std::nullopt);
    std::optional<std::filesystem::path> data;
    if (assets.location) data = assets.location->root.parent_path();
    run_all(data ? &*data : nullptr);
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All item tests passed\n";
    return 0;
}
