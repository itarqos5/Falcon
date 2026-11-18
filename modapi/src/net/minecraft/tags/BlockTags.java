package net.minecraft.tags;

import net.minecraft.resources.Identifier;
import net.minecraft.world.level.block.Block;

public final class BlockTags {
    private BlockTags() {}

    public static final TagKey<Block> FLOWERS = new TagKey<>(Identifier.withDefaultNamespace("flowers"));
    public static final TagKey<Block> SMALL_FLOWERS = new TagKey<>(Identifier.withDefaultNamespace("small_flowers"));
    public static final TagKey<Block> SNOW = new TagKey<>(Identifier.withDefaultNamespace("snow"));
}
