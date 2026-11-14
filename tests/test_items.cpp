// Tests of the item and inventory layer that need no OpenGL: the item registry, stacking, the inventory, the click
// logic of the menus (written from the game's AbstractContainerMenu rules), recipes, creative tabs and item model
// resolution. Tests that read the game's data run only when it is on this machine.

#include "falcon/assets/asset_locator.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/game/creative_inventory.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/hotbar_hud.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/mining.hpp"
#include "falcon/render/item_icons.hpp"
#include "falcon/render/player_preview.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/block_support.hpp"
#include "falcon/world/block_tags.hpp"
#include "falcon/world/loot.hpp"
#include "falcon/world/inventory.hpp"
#include "falcon/world/item_registry.hpp"
#include "falcon/world/menu.hpp"
#include "falcon/world/recipes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
int g_failures = 0;
}

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

using namespace falcon;

namespace {

Item itemNamed(const char* name) { return itemRegistry().find(name).value(); }
ItemStack S(const char* name, int count = 1) { return ItemStack(itemNamed(name), count); }

// "stone x5" style description of a stack for readable failures.
std::string describe(const ItemStack& s) {
    if (s.isEmpty()) return "-";
    return s.type().name + "x" + std::to_string(s.count) + (s.damage != 0 ? "!" + std::to_string(s.damage) : std::string());
}

// A player with an inventory and a menu, and a record of what was dropped.
struct Rig {
    Inventory inventory;
    std::vector<ItemStack> dropped;
    std::unique_ptr<InventoryMenu> menu;
    explicit Rig(bool creative = false) {
        inventory.infiniteMaterials = creative;
        MenuPlayer p;
        p.inventory = &inventory;
        p.infiniteMaterials = creative;
        p.dropHook = [this](const ItemStack& s) { dropped.push_back(s); };
        menu = std::make_unique<InventoryMenu>(p);
    }
    ItemStack& at(int menuSlot) { return menu->slot(menuSlot).getItem(); }
    void put(int menuSlot, ItemStack s) { menu->slot(menuSlot).set(std::move(s)); }
    void click(int slot, int button = 0, ClickType type = ClickType::Pickup) { menu->clicked(slot, button, type); }
    [[nodiscard]] std::string carried() const { return describe(menu->carried()); }
    [[nodiscard]] std::string slotText(int menuSlot) { return describe(at(menuSlot)); }
    [[nodiscard]] int droppedCount(const char* name) const {
        int n = 0;
        for (const ItemStack& s : dropped) {
            if (!s.isEmpty() && s.type().name == name) n += s.count;
        }
        return n;
    }
};

constexpr int kRes = InventoryMenu::kResult;
constexpr int kHot = InventoryMenu::kHotbarStart;  // menu slot of hotbar slot 0
constexpr int kOff = InventoryMenu::kOffhandSlot;

#define CHECK_SLOT(rig, slot, text)                                                                                   \
    do {                                                                                                              \
        if ((rig).slotText(slot) != (text)) {                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": slot " << (slot) << " is " << (rig).slotText(slot) << ", want " << (text) << '\n'; \
            ++g_failures;                                                                                             \
        }                                                                                                             \
    } while (false)
#define CHECK_CARRIED(rig, text)                                                                                      \
    do {                                                                                                              \
        if ((rig).carried() != (text)) {                                                                              \
            std::cerr << __FILE__ << ':' << __LINE__ << ": carried is " << (rig).carried() << ", want " << (text) << '\n'; \
            ++g_failures;                                                                                             \
        }                                                                                                             \
    } while (false)

// ---------------------------------------------------------------------------------------------------------------

void test_item_stack_rules() {
    ItemStack stone = S("stone", 10);
    CHECK(!stone.isEmpty() && stone.maxStackSize() == 64 && stone.isStackable());
    ItemStack taken = stone.split(4);
    CHECK(taken.count == 4 && stone.count == 6 && taken.item == stone.item);
    ItemStack all = stone.split(100);  // more than there is: everything
    CHECK(all.count == 6 && stone.isEmpty());
    CHECK(ItemStack().isEmpty() && S("stone", 0).isEmpty() && !S("stone", 1).isEmpty());
    CHECK(S("apple", 3).copyWithCount(7).count == 7 && ItemStack().copyWithCount(5).isEmpty());

    ItemStack pick = S("diamond_pickaxe");
    CHECK(pick.maxStackSize() == 1 && pick.isDamageable() && !pick.isDamaged() && !pick.isStackable());
    CHECK(pick.barWidth() == 13);
    pick.damage = 1;
    CHECK(pick.isDamaged() && pick.barWidth() == 13 && pick.maxDamage() == 1561);
    pick.damage = 780;
    CHECK(pick.barWidth() == 7);  // round(13 - 780 * 13 / 1561) = round(6.5) = 7
    pick.damage = 1561;
    CHECK(pick.barWidth() == 0 && pick.barColor() == 0xFF0000);
    ItemStack fresh = S("diamond_pickaxe");
    CHECK(fresh.barColor() == 0x00FF00);

    // Like items stack only when the components match.
    ItemStack a = S("stone", 1), b = S("stone", 5);
    CHECK(isSameItemSameComponents(a, b) && isSameItem(a, b));
    b.customName = "Rock";
    CHECK(!isSameItemSameComponents(a, b) && isSameItem(a, b));
    CHECK(!isSameItemSameComponents(S("stone"), S("dirt")));
    CHECK(isSameItemSameComponents(ItemStack(), ItemStack()));
    ItemStack worn = S("diamond_pickaxe");
    worn.damage = 5;
    CHECK(!isSameItemSameComponents(worn, S("diamond_pickaxe")));
}

void test_inventory_add() {
    Inventory inv;
    ItemStack s = S("stone", 70);
    CHECK(inv.add(s) && s.isEmpty());
    CHECK(describe(inv.item(0)) == "stonex64" && describe(inv.item(1)) == "stonex6");
    ItemStack more = S("stone", 60);
    CHECK(inv.add(more) && more.isEmpty());
    CHECK(describe(inv.item(1)) == "stonex64" && describe(inv.item(2)) == "stonex2");

    // The held slot is looked at first, then the offhand.
    Inventory held;
    held.item(4) = S("apple", 10);
    held.item(2) = S("apple", 10);
    held.selected = 4;
    ItemStack apple = S("apple", 5);
    CHECK(held.add(apple) && held.item(4).count == 15 && held.item(2).count == 10);
    held.offhand() = S("stone", 3);
    ItemStack stone = S("stone", 2);
    held.item(0) = S("stone", 9);
    held.selected = 7;
    CHECK(held.add(stone) && held.offhand().count == 5 && held.item(0).count == 9);

    // A full inventory takes nothing; what does not fit stays in the stack.
    Inventory full;
    for (int i = 0; i < Inventory::kMainSize; ++i) full.item(i) = S("dirt", 64);
    ItemStack rock = S("stone", 4);
    CHECK(!full.add(rock) && rock.count == 4);
    full.item(20) = S("stone", 62);
    full.add(rock);  // (like the game's, the return value only tells about the last round: 2 went in, 2 are left)
    CHECK(rock.count == 2 && full.item(20).count == 64);
    // Creative: always succeeds.
    Inventory creative;
    creative.infiniteMaterials = true;
    for (int i = 0; i < Inventory::kMainSize; ++i) creative.item(i) = S("dirt", 64);
    ItemStack gift = S("stone", 4);
    CHECK(creative.add(gift) && gift.isEmpty());

    // A worn tool needs a slot of its own and is never merged.
    Inventory tools;
    ItemStack pick = S("diamond_pickaxe");
    pick.damage = 10;
    CHECK(tools.add(pick) && pick.isEmpty() && tools.item(0).damage == 10);
    ItemStack pick2 = S("diamond_pickaxe");
    CHECK(tools.add(pick2) && tools.item(1).type().name == "diamond_pickaxe");

    // Pick block: findSlotMatching, suitable hotbar slot, pickSlot, addAndPick.
    Inventory p;
    p.item(20) = S("cobblestone", 7);
    p.selected = 3;
    CHECK(p.findSlotMatching(S("cobblestone")) == 20 && p.findSlotMatching(S("dirt")) == -1);
    p.item(3) = S("dirt", 2);
    p.pickSlot(20);  // the held slot is occupied, so the next empty hotbar slot (4) gets it
    CHECK(p.selected == 4 && describe(p.item(4)) == "cobblestonex7" && p.item(20).isEmpty() && describe(p.item(3)) == "dirtx2");
    Inventory q;
    q.selected = 2;
    for (int i = 0; i < 9; ++i) q.item(i) = S("dirt", i + 1);
    q.addAndPick(S("stone"));  // no empty hotbar slot: the held one is replaced and its stack moves out of the hotbar
    CHECK(q.selected == 2 && q.item(2).type().name == "stone" && q.item(9).type().name == "dirt" && q.item(9).count == 3);
    Inventory r;
    r.selected = 5;
    r.item(5) = S("sand", 4);
    r.addAndPick(S("stone"));
    CHECK(r.selected == 6 && r.item(6).type().name == "stone" && r.item(5).count == 4);

    // Q drops one, Ctrl+Q the stack.
    Inventory d;
    d.item(0) = S("stone", 5);
    ItemStack one = d.removeFromSelected(false);
    CHECK(one.count == 1 && d.item(0).count == 4);
    ItemStack rest = d.removeFromSelected(true);
    CHECK(rest.count == 4 && d.item(0).isEmpty() && d.removeFromSelected(true).isEmpty());
}

// ---------------------------------------------------------------------------------------------------------------
// Menu clicks

void test_menu_pickup_and_place() {
    {  // left click picks up the whole stack, left click on an empty slot puts it all down
        Rig r;
        r.put(kHot, S("stone", 20));
        r.click(kHot);
        CHECK_CARRIED(r, "stonex20");
        CHECK_SLOT(r, kHot, "-");
        r.click(10);
        CHECK_CARRIED(r, "-");
        CHECK_SLOT(r, 10, "stonex20");
    }
    {  // right click picks up half, rounded up; right click on an empty slot puts one down
        Rig r;
        r.put(10, S("stone", 5));
        r.click(10, 1);
        CHECK_CARRIED(r, "stonex3");
        CHECK_SLOT(r, 10, "stonex2");
        r.click(11, 1);
        CHECK_CARRIED(r, "stonex2");
        CHECK_SLOT(r, 11, "stonex1");
        r.click(11, 1);  // onto the same item: one more
        CHECK_CARRIED(r, "stonex1");
        CHECK_SLOT(r, 11, "stonex2");
        r.click(11, 1);  // the last one
        CHECK_CARRIED(r, "-");
        CHECK_SLOT(r, 11, "stonex3");
    }
    {  // a single item right-clicked: all of it
        Rig r;
        r.put(10, S("stone", 1));
        r.click(10, 1);
        CHECK_CARRIED(r, "stonex1");
        CHECK_SLOT(r, 10, "-");
    }
    {  // left click onto the same item merges up to the stack limit
        Rig r;
        r.put(10, S("stone", 40));
        r.put(11, S("stone", 30));
        r.click(11);
        r.click(10);
        CHECK_SLOT(r, 10, "stonex64");
        CHECK_CARRIED(r, "stonex6");
    }
    {  // another item: the stacks swap
        Rig r;
        r.put(10, S("stone", 3));
        r.put(11, S("dirt", 4));
        r.click(10);
        r.click(11);
        CHECK_SLOT(r, 11, "stonex3");
        CHECK_CARRIED(r, "dirtx4");
    }
    {  // right click on another item does swap too (the carried stack fits)
        Rig r;
        r.put(10, S("stone", 3));
        r.put(11, S("dirt", 4));
        r.click(10);
        r.click(11, 1);
        CHECK_SLOT(r, 11, "stonex3");
        CHECK_CARRIED(r, "dirtx4");
    }
    {  // a tool (stack size 1) swaps and never merges
        Rig r;
        r.put(10, S("diamond_pickaxe"));
        r.put(11, S("diamond_pickaxe"));
        r.click(10);
        r.click(11);
        CHECK_SLOT(r, 11, "diamond_pickaxex1");
        CHECK_CARRIED(r, "diamond_pickaxex1");
    }
    {  // clicking an empty slot with nothing on the cursor does nothing
        Rig r;
        r.click(10);
        r.click(10, 1);
        CHECK_CARRIED(r, "-");
        CHECK_SLOT(r, 10, "-");
    }
    {  // a stack of 16 and the limit of 64: ender pearls stack to 16 only
        Rig r;
        r.put(10, S("ender_pearl", 16));
        r.put(11, S("ender_pearl", 10));
        r.click(11);
        r.click(10);
        CHECK_SLOT(r, 10, "ender_pearlx16");
        CHECK_CARRIED(r, "ender_pearlx10");
    }
}

void test_menu_armor_and_offhand() {
    {  // the helmet slot only takes helmets; the chestplate slot only chestplates
        Rig r;
        r.put(10, S("iron_chestplate"));
        r.click(10);
        r.click(5);  // helmet slot
        CHECK_SLOT(r, 5, "-");
        CHECK_CARRIED(r, "iron_chestplatex1");
        r.click(6);  // chest slot
        CHECK_SLOT(r, 6, "iron_chestplatex1");
        CHECK_CARRIED(r, "-");
        CHECK(r.inventory.item(Inventory::kChest).type().name == "iron_chestplate");
    }
    {  // the offhand takes anything, up to a full stack
        Rig r;
        r.put(10, S("stone", 30));
        r.click(10);
        r.click(kOff);
        CHECK_SLOT(r, kOff, "stonex30");
        CHECK(r.inventory.offhand().count == 30);
    }
    {  // shift-click: chestplate to the chest slot, a second one to the hotbar/inventory
        Rig r;
        r.put(9, S("iron_chestplate"));
        r.put(10, S("iron_chestplate"));
        r.click(9, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 9, "-");
        CHECK_SLOT(r, 6, "iron_chestplatex1");
        r.click(10, 0, ClickType::QuickMove);  // the slot is taken: main -> hotbar
        CHECK_SLOT(r, 6, "iron_chestplatex1");
        CHECK_SLOT(r, 10, "-");
        CHECK_SLOT(r, kHot, "iron_chestplatex1");
    }
    {  // shift-click on the worn armor: back into the inventory (the inventory slots first, 9 up)
        Rig r;
        r.put(8, S("iron_helmet"));  // wrong slot content for the test, but shift-click only looks at the index
        r.click(8, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 8, "-");
        CHECK_SLOT(r, 9, "iron_helmetx1");
    }
    {  // a shield goes to the offhand
        Rig r;
        r.put(kHot + 2, S("shield"));
        r.click(kHot + 2, 0, ClickType::QuickMove);
        CHECK_SLOT(r, kOff, "shieldx1");
        CHECK_SLOT(r, kHot + 2, "-");
    }
}

void test_menu_quick_move() {
    {  // hotbar -> main: the first free main slot, after merging onto stacks that have room
        Rig r;
        r.put(kHot, S("stone", 30));
        r.put(12, S("stone", 40));
        r.click(kHot, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 12, "stonex64");
        CHECK_SLOT(r, 9, "stonex6");
        CHECK_SLOT(r, kHot, "-");
    }
    {  // main -> hotbar
        Rig r;
        r.put(20, S("dirt", 5));
        r.put(kHot + 1, S("dirt", 60));
        r.click(20, 0, ClickType::QuickMove);
        CHECK_SLOT(r, kHot + 1, "dirtx64");
        CHECK_SLOT(r, kHot, "dirtx1");
        CHECK_SLOT(r, 20, "-");
    }
    {  // a full destination: the stack stays where it was
        Rig r;
        for (int i = 9; i < 36; ++i) r.put(i, S("dirt", 64));
        r.put(kHot, S("stone", 5));
        r.click(kHot, 0, ClickType::QuickMove);
        CHECK_SLOT(r, kHot, "stonex5");
    }
    {  // partly moved: the rest stays
        Rig r;
        for (int i = 9; i < 36; ++i) r.put(i, S("dirt", 64));
        r.put(10, S("stone", 40));  // the only slot with room for stone
        r.put(kHot, S("stone", 50));
        r.click(kHot, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 10, "stonex64");
        CHECK_SLOT(r, kHot, "stonex26");
    }
    {  // shift-click from a crafting grid slot goes to the inventory, from the main slots to the hotbar
        Rig r;
        r.put(1, S("oak_planks", 5));
        r.click(1, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 1, "-");
        CHECK_SLOT(r, 9, "oak_planksx5");
    }
}

void test_menu_swap_and_throw() {
    {  // a number key swaps a hovered slot with a hotbar slot
        Rig r;
        r.put(10, S("stone", 5));
        r.put(kHot + 2, S("dirt", 3));
        r.click(10, 2, ClickType::Swap);
        CHECK_SLOT(r, 10, "dirtx3");
        CHECK_SLOT(r, kHot + 2, "stonex5");
    }
    {  // into an empty hotbar slot, and from an empty slot
        Rig r;
        r.put(10, S("stone", 5));
        r.click(10, 0, ClickType::Swap);
        CHECK_SLOT(r, kHot, "stonex5");
        CHECK_SLOT(r, 10, "-");
        r.click(11, 0, ClickType::Swap);  // hovered slot empty: the hotbar stack moves in
        CHECK_SLOT(r, 11, "stonex5");
        CHECK_SLOT(r, kHot, "-");
    }
    {  // F: swap with the offhand
        Rig r;
        r.put(10, S("stone", 5));
        r.put(kOff, S("apple", 2));
        r.click(10, 40, ClickType::Swap);
        CHECK_SLOT(r, 10, "applex2");
        CHECK_SLOT(r, kOff, "stonex5");
    }
    {  // swapping with an armor slot only works when the item fits it
        Rig r;
        r.put(kHot, S("stone", 5));
        r.click(5, 0, ClickType::Swap);  // helmet slot refuses stone
        CHECK_SLOT(r, 5, "-");
        CHECK_SLOT(r, kHot, "stonex5");
        r.put(kHot, S("iron_helmet"));
        r.click(5, 0, ClickType::Swap);
        CHECK_SLOT(r, 5, "iron_helmetx1");
        CHECK_SLOT(r, kHot, "-");
        r.click(5, 0, ClickType::Swap);  // and back
        CHECK_SLOT(r, kHot, "iron_helmetx1");
    }
    {  // Q drops one, Ctrl+Q the whole stack
        Rig r;
        r.put(10, S("stone", 5));
        r.click(10, 0, ClickType::Throw);
        CHECK_SLOT(r, 10, "stonex4");
        CHECK(r.droppedCount("stone") == 1);
        r.click(10, 1, ClickType::Throw);
        CHECK_SLOT(r, 10, "-");
        CHECK(r.droppedCount("stone") == 5);
    }
    {  // a click outside the window: left drops the carried stack, right one at a time
        Rig r;
        r.put(10, S("stone", 3));
        r.click(10);
        r.click(kOutsideSlot, 1);
        CHECK_CARRIED(r, "stonex2");
        CHECK(r.droppedCount("stone") == 1);
        r.click(kOutsideSlot, 0);
        CHECK_CARRIED(r, "-");
        CHECK(r.droppedCount("stone") == 3);
    }
    {  // Q does nothing while a stack is on the cursor
        Rig r;
        r.put(10, S("stone", 3));
        r.put(11, S("dirt", 3));
        r.click(10);
        r.click(11, 0, ClickType::Throw);
        CHECK_SLOT(r, 11, "dirtx3");
        CHECK(r.dropped.empty());
    }
    {  // middle click: a full stack in creative only
        Rig c(true);
        c.put(10, S("stone", 1));
        c.click(10, 2, ClickType::Clone);
        CHECK_CARRIED(c, "stonex64");
        CHECK_SLOT(c, 10, "stonex1");
        Rig s(false);
        s.put(10, S("stone", 1));
        s.click(10, 2, ClickType::Clone);
        CHECK_CARRIED(s, "-");
    }
}

// A drag: header 0 (start) with the type, then header 1 for each slot passed over, then header 2 (end).
void drag(Rig& r, int type, const std::vector<int>& slots) {
    r.click(kOutsideSlot, ContainerMenu::getQuickcraftMask(0, type), ClickType::QuickCraft);
    for (int s : slots) r.click(s, ContainerMenu::getQuickcraftMask(1, type), ClickType::QuickCraft);
    r.click(kOutsideSlot, ContainerMenu::getQuickcraftMask(2, type), ClickType::QuickCraft);
}

void test_menu_drag() {
    {  // left drag spreads evenly, rounding down; the rest stays on the cursor
        Rig r;
        r.put(10, S("stone", 10));
        r.click(10);
        drag(r, 0, {11, 12, 13});
        CHECK_SLOT(r, 11, "stonex3");
        CHECK_SLOT(r, 12, "stonex3");
        CHECK_SLOT(r, 13, "stonex3");
        CHECK_CARRIED(r, "stonex1");
    }
    {  // right drag gives one to each
        Rig r;
        r.put(10, S("stone", 5));
        r.click(10);
        drag(r, 1, {11, 12, 13});
        CHECK_SLOT(r, 11, "stonex1");
        CHECK_SLOT(r, 13, "stonex1");
        CHECK_CARRIED(r, "stonex2");
    }
    {  // as many slots as items: one each, nothing left
        Rig r;
        r.put(10, S("stone", 3));
        r.click(10);
        drag(r, 0, {11, 12, 13});
        CHECK_SLOT(r, 11, "stonex1");
        CHECK_CARRIED(r, "-");
    }
    {  // a slot may only be joined while there are items left for it: 2 items, 4 slots -> the first two slots
        Rig r;
        r.put(10, S("stone", 2));
        r.click(10);
        drag(r, 0, {11, 12, 13, 14});
        CHECK_SLOT(r, 11, "stonex1");
        CHECK_SLOT(r, 12, "stonex1");
        CHECK_SLOT(r, 13, "-");
        CHECK_CARRIED(r, "-");
    }
    {  // slots already holding the same item grow, capped at the stack limit; slots with another item are skipped
        Rig r;
        r.put(10, S("stone", 64));
        r.put(11, S("stone", 63));
        r.put(12, S("stone", 63));
        r.put(13, S("dirt", 5));
        r.click(10);
        drag(r, 0, {11, 12, 13});
        CHECK_SLOT(r, 11, "stonex64");
        CHECK_SLOT(r, 12, "stonex64");
        CHECK_SLOT(r, 13, "dirtx5");
        CHECK_CARRIED(r, "stonex62");
    }
    {  // dragging over one slot is just a click
        Rig r;
        r.put(10, S("stone", 10));
        r.click(10);
        drag(r, 0, {11});
        CHECK_SLOT(r, 11, "stonex10");
        CHECK_CARRIED(r, "-");
        Rig q;
        q.put(10, S("stone", 10));
        q.click(10);
        drag(q, 1, {11});  // right button: one
        CHECK_SLOT(q, 11, "stonex1");
        CHECK_CARRIED(q, "stonex9");
    }
    {  // slots that cannot take the item (armor) are not part of the drag
        Rig r;
        r.put(10, S("stone", 6));
        r.click(10);
        drag(r, 0, {5, 11, 12});
        CHECK_SLOT(r, 5, "-");
        CHECK_SLOT(r, 11, "stonex3");
        CHECK_SLOT(r, 12, "stonex3");
    }
    {  // creative: the middle button fills every slot to its maximum
        Rig c(true);
        c.put(10, S("stone", 1));
        c.click(10);
        drag(c, 2, {11, 12});
        CHECK_SLOT(c, 11, "stonex64");
        CHECK_SLOT(c, 12, "stonex64");
        Rig s(false);
        s.put(10, S("stone", 5));
        s.click(10);
        drag(s, 2, {11, 12});  // not allowed in survival: nothing happens
        CHECK_SLOT(s, 11, "-");
        CHECK_CARRIED(s, "stonex5");
    }
    {  // an empty cursor never drags
        Rig r;
        drag(r, 0, {11, 12});
        CHECK_SLOT(r, 11, "-");
    }
}

void test_menu_double_click() {
    {  // the double click gathers matching stacks onto the cursor, scanning from slot 0 up
        Rig r;
        r.put(10, S("cobblestone", 20));
        r.put(11, S("cobblestone", 30));
        r.put(12, S("dirt", 30));
        r.put(kHot, S("cobblestone", 30));
        r.put(20, S("cobblestone", 1));
        r.click(10);  // the first click picked up 20
        r.click(10, 0, ClickType::PickupAll);
        // The cursor takes up to 64: 20 + 30 (slot 11) + 14 of slot 20/hotbar in slot order (20 before 36).
        CHECK_CARRIED(r, "cobblestonex64");
        CHECK_SLOT(r, 11, "-");
        CHECK_SLOT(r, 20, "-");
        CHECK_SLOT(r, kHot, "cobblestonex17");
        CHECK_SLOT(r, 12, "dirtx30");
    }
    {  // full stacks are left for the second pass
        Rig r;
        r.put(10, S("cobblestone", 5));
        r.put(11, S("cobblestone", 64));
        r.put(12, S("cobblestone", 10));
        r.click(10);
        r.click(10, 0, ClickType::PickupAll);
        CHECK_SLOT(r, 12, "-");               // the partial stack is taken first (5 + 10 = 15)
        CHECK_SLOT(r, 11, "cobblestonex15");  // then the full one gives what is still needed: 64 - 15 = 49
        CHECK_CARRIED(r, "cobblestonex64");
    }
    {  // nothing on the cursor: nothing is gathered
        Rig r;
        r.put(10, S("cobblestone", 5));
        r.put(11, S("cobblestone", 5));
        r.click(10, 0, ClickType::PickupAll);
        CHECK_CARRIED(r, "-");
        CHECK_SLOT(r, 11, "cobblestonex5");
    }
}

void test_menu_crafting() {
    {  // one log makes four planks; taking the result uses the log up
        Rig r;
        r.put(1, S("oak_log", 3));
        CHECK_SLOT(r, kRes, "oak_planksx4");
        r.click(kRes);
        CHECK_CARRIED(r, "oak_planksx4");
        CHECK_SLOT(r, 1, "oak_logx2");
        CHECK_SLOT(r, kRes, "oak_planksx4");  // still craftable
        r.click(kRes);                       // the cursor holds planks: same item, 4 + 4
        CHECK_CARRIED(r, "oak_planksx8");
        CHECK_SLOT(r, 1, "oak_logx1");
        r.click(kRes);
        CHECK_CARRIED(r, "oak_planksx12");
        CHECK_SLOT(r, 1, "-");
        CHECK_SLOT(r, kRes, "-");
    }
    {  // the result does not fit on a different carried stack
        Rig r;
        r.put(1, S("oak_log"));
        r.put(10, S("stone", 3));
        r.click(10);
        r.click(kRes);
        CHECK_CARRIED(r, "stonex3");
        CHECK_SLOT(r, 1, "oak_logx1");
    }
    {  // the result slot cannot be filled by hand
        Rig r;
        r.put(10, S("stone", 3));
        r.click(10);
        r.click(kRes);
        CHECK_SLOT(r, kRes, "-");
        CHECK_CARRIED(r, "stonex3");
    }
    {  // a full cursor stack (64 planks) cannot take more
        Rig r;
        r.put(1, S("oak_log", 2));
        r.put(10, S("oak_planks", 62));
        r.click(10);
        r.click(kRes);
        CHECK_CARRIED(r, "oak_planksx62");  // 4 do not fit (62 + 4 > 64): refused
        CHECK_SLOT(r, 1, "oak_logx2");
    }
    {  // shift-click on the result crafts as many as the grid allows, into the inventory
        Rig r;
        r.put(2, S("oak_log", 3));
        r.click(kRes, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 2, "-");
        CHECK_SLOT(r, kRes, "-");
        int planks = 0;
        for (int i = 9; i < 45; ++i) {
            if (!r.at(i).isEmpty() && r.at(i).type().name == "oak_planks") planks += r.at(i).count;
        }
        CHECK(planks == 12);
        CHECK(r.dropped.empty());
    }
    {  // shift-click crafts into the hotbar first? No: result goes to the end of the inventory, reversed (hotbar last slot first)
        Rig r;
        r.put(1, S("oak_log", 1));
        r.click(kRes, 0, ClickType::QuickMove);
        CHECK_SLOT(r, 44, "oak_planksx4");  // the result is moved into slots 9..44 from the back
    }
    {  // sticks: two planks one above the other, anywhere in the grid
        Rig r;
        r.put(1, S("oak_planks", 2));
        r.put(3, S("oak_planks", 2));
        CHECK_SLOT(r, kRes, "stickx4");
        Rig r2;
        r2.put(2, S("oak_planks"));
        r2.put(4, S("oak_planks"));
        CHECK_SLOT(r2, kRes, "stickx4");
        Rig side;  // side by side: no recipe
        side.put(1, S("oak_planks"));
        side.put(2, S("oak_planks"));
        CHECK_SLOT(side, kRes, "-");
        Rig diag;  // diagonal: none either
        diag.put(1, S("oak_planks"));
        diag.put(4, S("oak_planks"));
        CHECK_SLOT(diag, kRes, "-");
    }
    {  // a crafting table needs four planks in a square; three do not
        Rig r;
        r.put(1, S("oak_planks"));
        r.put(2, S("oak_planks"));
        r.put(3, S("oak_planks"));
        CHECK_SLOT(r, kRes, "-");
        r.put(4, S("oak_planks"));
        CHECK_SLOT(r, kRes, "crafting_tablex1");
        r.click(kRes);
        CHECK_CARRIED(r, "crafting_tablex1");
        CHECK_SLOT(r, 1, "-");
        CHECK_SLOT(r, 4, "-");
        CHECK_SLOT(r, kRes, "-");
    }
    {  // an item in the grid that has no recipe leaves the result empty, and taking items back removes the result
        Rig r;
        r.put(1, S("stone"));
        CHECK_SLOT(r, kRes, "-");
        r.put(1, S("oak_log"));
        CHECK_SLOT(r, kRes, "oak_planksx4");
        r.click(1);  // pick the log up again
        CHECK_SLOT(r, kRes, "-");
    }
    {  // closing the menu puts the grid and the cursor stack back into the inventory
        Rig r;
        r.put(1, S("stone", 5));
        r.put(10, S("dirt", 3));
        r.click(10);
        r.menu->removed();
        int stone = r.inventory.countOf(itemNamed("stone")), dirt = r.inventory.countOf(itemNamed("dirt"));
        CHECK(stone == 5 && dirt == 3 && r.dropped.empty());
        CHECK_SLOT(r, kRes, "-");
        CHECK_CARRIED(r, "-");
        // ... or drops what does not fit
        Rig f;
        for (int i = 0; i < 36; ++i) f.inventory.item(i) = S("dirt", 64);
        f.put(2, S("stone", 5));
        f.menu->removed();
        CHECK(f.droppedCount("stone") == 5);
    }
}

void test_menu_misc() {
    {  // a stack that is too big for the slot's limit is refused on swap (armor slot holds 1)
        Rig r;
        r.put(10, S("iron_helmet"));
        r.put(11, S("iron_helmet"));
        r.click(10);
        r.click(5);  // helmet slot, empty: fine
        r.click(11);
        r.click(5);  // swap a helmet with the worn one
        CHECK_SLOT(r, 5, "iron_helmetx1");
        CHECK_CARRIED(r, "iron_helmetx1");
    }
    {  // hotbar slot numbers
        CHECK(InventoryMenu::isHotbarSlot(36) && InventoryMenu::isHotbarSlot(44) && InventoryMenu::isHotbarSlot(45));
        CHECK(!InventoryMenu::isHotbarSlot(35) && !InventoryMenu::isHotbarSlot(0));
    }
    {  // slot layout of the game's InventoryMenu
        Rig r;
        const InventoryMenu& m = *r.menu;
        CHECK(m.slotCount() == 46);
        CHECK(m.slot(0).x == 154 && m.slot(0).y == 28);
        CHECK(m.slot(1).x == 98 && m.slot(1).y == 18 && m.slot(4).x == 116 && m.slot(4).y == 36);
        CHECK(m.slot(5).x == 8 && m.slot(5).y == 8 && m.slot(8).y == 62);
        CHECK(m.slot(9).x == 8 && m.slot(9).y == 84 && m.slot(35).x == 8 + 8 * 18 && m.slot(35).y == 84 + 36);
        CHECK(m.slot(36).x == 8 && m.slot(36).y == 142 && m.slot(44).x == 8 + 8 * 18);
        CHECK(m.slot(45).x == 77 && m.slot(45).y == 62);
        CHECK(m.slot(5).container == &r.inventory && m.slot(5).slot == Inventory::kHead && m.slot(8).slot == Inventory::kFeet);
        CHECK(m.slot(9).slot == 9 && m.slot(36).slot == 0 && m.slot(45).slot == Inventory::kOffhand);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Recipes

void setOne(std::vector<ItemStack>& grid, int index, const char* name) { grid[static_cast<std::size_t>(index)] = S(name); }

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

// How long blocks take to dig and what they drop, from the game's tags and loot tables.
void test_mining_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks, const ItemRegistry& items) {
    const BlockTags tags(dataDir / "data" / "minecraft" / "tags" / "block", blocks);
    const MiningRules mining(blocks, tags);
    const BlockLoot loot(dataDir, blocks);
    const auto b = [&](const char* text) {
        const auto s = blocks.parse(text);
        CHECK(s.has_value());
        return s.value_or(Block::Air);
    };
    const auto stack = [&](const char* name) {
        const auto item = items.find(name);
        CHECK(item.has_value());
        return item ? ItemStack(*item, 1) : ItemStack();
    };
    const ItemStack hand, wooden = stack("wooden_pickaxe"), iron = stack("iron_pickaxe"), diamond = stack("diamond_pickaxe"), shears = stack("shears");
    const auto near = [](float a, float c) { return std::fabs(a - c) < 1.0e-5f * std::max(1.0f, std::fabs(c)); };
    // Tags follow other tags: #minecraft:logs holds oak logs through #minecraft:oak_logs.
    CHECK(tags.contains("#minecraft:logs", static_cast<std::size_t>(*blocks.findBlock("oak_log"))));
    CHECK(tags.contains("minecraft:mineable/pickaxe", static_cast<std::size_t>(*blocks.findBlock("stone"))));
    CHECK(!tags.contains("#minecraft:mineable/pickaxe", static_cast<std::size_t>(*blocks.findBlock("dirt"))));
    // Dirt (hardness 0.5) by hand: no tool is needed, and a shovel is faster. Stone (1.5): the pickaxe is the right tool, by hand it is 100 not 30.
    const Block dirt = b("dirt"), stone = b("stone");
    CHECK(mining.correctForDrops(hand, dirt) && near(mining.progressPerTick(hand, dirt, true, false), 1.0f / 0.5f / 30.0f));
    CHECK(!mining.correctForDrops(hand, stone) && near(mining.progressPerTick(hand, stone, true, false), 1.0f / 1.5f / 100.0f));
    CHECK(mining.correctForDrops(wooden, stone) && near(mining.progressPerTick(wooden, stone, true, false), 2.0f / 1.5f / 30.0f));
    CHECK(near(mining.progressPerTick(iron, stone, true, false), 6.0f / 1.5f / 30.0f));
    CHECK(near(mining.progressPerTick(wooden, stone, false, false), 2.0f / 1.5f / 30.0f / 5.0f));  // in the air: a fifth
    // Iron ore needs a stone pickaxe; a wooden one digs it at its speed but gets no drops. Obsidian wants diamond. Bedrock never breaks; a flower is instant.
    CHECK(!mining.correctForDrops(wooden, b("iron_ore")) && mining.correctForDrops(stack("stone_pickaxe"), b("iron_ore")));
    CHECK(!mining.correctForDrops(iron, b("obsidian")) && mining.correctForDrops(diamond, b("obsidian")));
    CHECK(mining.progressPerTick(diamond, b("bedrock"), true, false) == 0.0f);
    CHECK(mining.progressPerTick(hand, b("poppy"), true, false) >= 1.0f);
    CHECK(destroyStage(0.0f) == -1 && destroyStage(0.05f) == 0 && destroyStage(0.55f) == 5 && destroyStage(0.99f) == 9);
    // Loot: stone gives cobblestone, grass block gives dirt, a log gives itself, leaves give nothing by hand and themselves with shears.
    std::mt19937 random(7);
    const auto only = [&](const std::vector<ItemStack>& drops, const char* name, int count) {
        return drops.size() == 1 && drops[0].item == *items.find(name) && drops[0].count == count;
    };
    CHECK(only(loot.drops(stone, wooden, random), "cobblestone", 1));
    CHECK(only(loot.drops(b("grass_block[snowy=false]"), hand, random), "dirt", 1));
    CHECK(only(loot.drops(b("oak_log[axis=y]"), hand, random), "oak_log", 1));
    CHECK(only(loot.drops(b("oak_leaves[distance=1,persistent=true,waterlogged=false]"), shears, random), "oak_leaves", 1));
    int leafDrops = 0;
    for (int i = 0; i < 400; ++i) leafDrops += loot.drops(b("oak_leaves[distance=1,persistent=true,waterlogged=false]"), hand, random).empty() ? 0 : 1;
    CHECK(leafDrops > 0 && leafDrops < 100);  // a sapling, sticks or an apple now and then (a few in a hundred)
    CHECK(loot.drops(b("bedrock"), diamond, random).empty());
    // A slab: two slabs when it is double.
    CHECK(only(loot.drops(b("oak_slab[type=double,waterlogged=false]"), hand, random), "oak_slab", 2));
    CHECK(only(loot.drops(b("oak_slab[type=bottom,waterlogged=false]"), hand, random), "oak_slab", 1));
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
    test_mining_real(dataDir, *blocks, *items);
    test_player_preview_real(dataDir / "assets");
    test_item_names_real(dataDir / "assets");
    test_item_icons_real(dataDir / "assets", blocks);
}

// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
// The hotbar's fading item name

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

}  // namespace

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
