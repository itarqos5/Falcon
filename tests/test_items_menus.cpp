// D:/Projects/Falcon/tests/items tests: menus.
#include "test_items_support.hpp"

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

// What a server is told of a click: the slot, the slots that changed with what they hold now, and the cursor; and that the menu was closed.
void test_menu_click_hook() {
    Rig r;
    r.put(kHot, S("stone", 20));
    int calls = 0, slot = -2;
    std::vector<std::pair<int, ItemStack>> changed;
    ItemStack carried;
    r.menu->onClicked = [&](int s, int, ClickType, const std::vector<std::pair<int, ItemStack>>& c, const ItemStack& cursor) {
        ++calls;
        slot = s;
        changed = c;
        carried = cursor;
    };
    r.click(kHot);  // picked up: the slot is empty and the cursor holds the stack
    CHECK(calls == 1 && slot == kHot);
    CHECK(changed.size() == 1 && changed[0].first == kHot && changed[0].second.isEmpty());
    CHECK(carried.count == 20);
    r.click(10);  // put down in another slot
    CHECK(calls == 2 && slot == 10);
    CHECK(changed.size() == 1 && changed[0].first == 10 && changed[0].second.count == 20);
    CHECK(carried.isEmpty());
    r.click(10, 1, ClickType::Pickup);
    CHECK(calls == 3);
    int closed = 0;
    r.menu->onRemoved = [&] { ++closed; };
    r.menu->removed();
    CHECK(closed == 1);
}

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
