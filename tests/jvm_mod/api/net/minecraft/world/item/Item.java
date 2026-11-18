package net.minecraft.world.item;
public class Item {
    public Item(Properties properties) {}
    public static class Properties {
        public Properties stacksTo(int count) { return this; }
        public Properties durability(int uses) { return this; }
        public Properties fireResistant() { return this; }
    }
}
