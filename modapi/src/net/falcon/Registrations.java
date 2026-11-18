package net.falcon;

import net.minecraft.core.Registry;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.block.Block;

// What a mod adds to the game's registries is told to the client here, as facts (the client makes the items and blocks itself).
public final class Registrations {
    private Registrations() {}

    public static void registered(Registry<?> registry, Identifier id, Object value) {
        if (id.getNamespace().equals("minecraft")) return;
        Host.registered(registry.name().getPath(), id.toString(), value.getClass().getSimpleName());
        if (value instanceof Block block) {
            Host.registerBlock(id.toString(), block.properties.hardness, block.properties.resistance, block.properties.light, block.properties.requiresTool, block.properties.noOcclusion, block.properties.sound);
        } else if (value instanceof Item item) {
            Item.Properties p = item.properties;
            String blockId = item.blockOf != null ? Block.idOf(item.blockOf) : "";
            Host.registerItem(id.toString(), p.stacksTo, p.durability, p.fireResistant, p.rarity, p.nutrition, p.saturation, blockId);
        }
    }
}
