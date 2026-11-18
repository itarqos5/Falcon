package net.minecraft.world.item;

import net.minecraft.core.component.DataComponentMap;
import net.minecraft.core.component.DataComponentType;
import net.minecraft.network.chat.Component;
import net.minecraft.tags.TagKey;
import net.minecraft.world.level.ItemLike;

public class ItemStack {
    public static final ItemStack EMPTY = new ItemStack((ItemLike) null, 0);
    private Item item;
    private int count;

    public ItemStack(ItemLike item) { this(item, 1); }
    public ItemStack(ItemLike item, int count) { this.item = item == null ? null : item.asItem(); this.count = count; }

    public boolean isEmpty() { return item == null || count <= 0 || item == net.minecraft.world.item.Items.AIR; }
    public Item getItem() { return item == null ? Items.AIR : item; }
    public int getCount() { return count; }
    public void setCount(int n) { count = n; }
    public void shrink(int n) { count -= n; }
    public void grow(int n) { count += n; }
    public ItemStack copy() { return new ItemStack(item, count); }
    public Component getHoverName() { return getItem().getName(this); }
    public boolean is(Object itemOrTag) { return itemOrTag instanceof Item i ? getItem() == i : false; }
    public boolean is(TagKey<Item> tag) { return false; }
    public boolean has(DataComponentType<?> type) { return false; }
    public <T> T get(DataComponentType<? extends T> type) { return null; }
    public DataComponentMap getComponents() { return DataComponentMap.EMPTY; }
    public net.minecraft.world.item.component.SwingAnimation getInteractAnimation() { return null; }
    public int getMaxStackSize() { return getItem().getMaxStackSize(); }
}
