#pragma once

// Shared by the files of this test (D:/Projects/Falcon/tests/test_items_*.cpp): the includes, CHECK and the helpers, and the tests that live in the other files.
// Tests of the item and inventory layer that need no OpenGL: the item registry, stacking, the inventory, the click
// logic of the menus (written from the game's AbstractContainerMenu rules), recipes, creative tabs and item model
// resolution. Tests that read the game's data run only when it is on this machine.

#include "falcon/assets/asset_locator.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/game/creative_inventory.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/hotbar_hud.hpp"
#include "falcon/game/remote_menu.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/render/item_icons.hpp"
#include "falcon/render/player_preview.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/block_support.hpp"
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

inline int g_failures = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

using namespace falcon;


inline Item itemNamed(const char* name) { return itemRegistry().find(name).value(); }
inline ItemStack S(const char* name, int count = 1) { return ItemStack(itemNamed(name), count); }

// "stone x5" style description of a stack for readable failures.
inline std::string describe(const ItemStack& s) {
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

inline constexpr int kRes = InventoryMenu::kResult;
inline constexpr int kHot = InventoryMenu::kHotbarStart;  // menu slot of hotbar slot 0
inline constexpr int kOff = InventoryMenu::kOffhandSlot;

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

void setOne(std::vector<ItemStack>& grid, int index, const char* name);

// The tests that are in the other files.
void test_item_stack_rules();
void test_inventory_add();
void test_menu_pickup_and_place();
void test_menu_armor_and_offhand();
void test_menu_quick_move();
void test_menu_swap_and_throw();
void drag(Rig& r, int type, const std::vector<int>& slots);
void test_menu_drag();
void test_menu_double_click();
void test_menu_crafting();
void test_menu_misc();
void test_menu_click_hook();
void test_remote_menu();
std::string craft(const RecipeManager& recipes, int w, int h, const std::vector<ItemStack>& grid);
void test_recipes_minimal();
void test_block_support_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks);
void test_item_registry_and_recipes_real(const std::filesystem::path& dataDir);
