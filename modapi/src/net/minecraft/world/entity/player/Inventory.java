package net.minecraft.world.entity.player;

import net.minecraft.core.NonNullList;
import net.minecraft.world.item.ItemStack;

public class Inventory {
    public int selected = 0;
    public NonNullList<ItemStack> getNonEquipmentItems() { return NonNullList.withSize(36, ItemStack.EMPTY); }
    public ItemStack getItem(int slot) { return ItemStack.EMPTY; }
    public ItemStack getSelectedItem() { return ItemStack.EMPTY; }
    public int getSelectedSlot() { return selected; }
}
