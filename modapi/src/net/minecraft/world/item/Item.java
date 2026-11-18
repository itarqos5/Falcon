package net.minecraft.world.item;

import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.world.level.ItemLike;
import net.minecraft.world.level.block.Block;

public class Item implements ItemLike {
    public final Properties properties;
    public Block blockOf;

    public Item(Properties properties) { this.properties = properties; }

    @Override public Item asItem() { return this; }
    public int getMaxStackSize() { return properties.stacksTo; }
    public int getMaxDamage() { return properties.durability; }
    public net.minecraft.network.chat.Component getName(ItemStack stack) { return net.minecraft.network.chat.Component.translatable(descriptionId()); }
    public String descriptionId() {
        Identifier id = BuiltInRegistries.ITEM.getKey(this);
        return (blockOf != null ? "block." : "item.") + id.getNamespace() + "." + id.getPath().replace('/', '.');
    }
    @Override public String toString() { return String.valueOf(BuiltInRegistries.ITEM.getKey(this)); }

    public static class Properties {
        public int stacksTo = 64, durability = 0, nutrition = 0;
        public float saturation = 0.0F;
        public boolean fireResistant = false;
        public String rarity = "common";

        public Properties stacksTo(int n) { stacksTo = n; return this; }
        public Properties durability(int n) { durability = n; stacksTo = 1; return this; }
        public Properties fireResistant() { fireResistant = true; return this; }
        public Properties rarity(Rarity r) { rarity = r.name().toLowerCase(); return this; }
        public Properties food(net.minecraft.world.food.FoodProperties f) { nutrition = f.nutrition(); saturation = f.saturation(); return this; }
        public Properties setId(Object key) { return this; }
        public Properties useItemDescriptionPrefix() { return this; }
        public Properties overrideDescription(String key) { return this; }
    }
}
