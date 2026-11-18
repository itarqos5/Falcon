package net.minecraft.core.registries;

import net.minecraft.core.DefaultedRegistry;
import net.minecraft.core.Registry;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.block.Block;

public final class BuiltInRegistries {
    private BuiltInRegistries() {}

    public static final DefaultedRegistry<Item> ITEM = new DefaultedRegistry<>("air", Identifier.withDefaultNamespace("item"));
    public static final DefaultedRegistry<Block> BLOCK = new DefaultedRegistry<>("air", Identifier.withDefaultNamespace("block"));
    public static final Registry<Object> SOUND_EVENT = new Registry<>(Identifier.withDefaultNamespace("sound_event"));
    public static final Registry<Object> CREATIVE_MODE_TAB = new Registry<>(Identifier.withDefaultNamespace("creative_mode_tab"));
    public static final Registry<Object> BLOCK_ENTITY_TYPE = new Registry<>(Identifier.withDefaultNamespace("block_entity_type"));
    public static final Registry<Object> ENTITY_TYPE = new Registry<>(Identifier.withDefaultNamespace("entity_type"));
    public static final Registry<Object> FLUID = new Registry<>(Identifier.withDefaultNamespace("fluid"));
}
