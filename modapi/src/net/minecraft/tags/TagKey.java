package net.minecraft.tags;

import net.minecraft.resources.Identifier;

public record TagKey<T>(Identifier location) {
    public static <T> TagKey<T> create(Object registry, Identifier id) { return new TagKey<>(id); }
}
