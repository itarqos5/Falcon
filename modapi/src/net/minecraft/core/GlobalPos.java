package net.minecraft.core;

import net.minecraft.resources.ResourceKey;

public record GlobalPos(ResourceKey<?> dimension, BlockPos pos) {
    public static GlobalPos of(ResourceKey<?> dimension, BlockPos pos) { return new GlobalPos(dimension, pos); }
}
