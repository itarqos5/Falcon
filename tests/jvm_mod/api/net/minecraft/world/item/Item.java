package net.minecraft.world.item;
import net.minecraft.world.food.FoodProperties;
public class Item {
    public Item(Properties properties) {}
    public static class Properties {
        public Properties stacksTo(int count) { return this; }
        public Properties durability(int uses) { return this; }
        public Properties fireResistant() { return this; }
        public Properties food(FoodProperties food) { return this; }
        public Properties rarity(Rarity rarity) { return this; }
    }
}
