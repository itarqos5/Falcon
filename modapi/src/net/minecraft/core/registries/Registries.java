package net.minecraft.core.registries;

import net.minecraft.core.Registry;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;

public final class Registries {
    private Registries() {}

    public static final ResourceKey<Registry<Object>> ITEM = ResourceKey.createRegistryKey(Identifier.withDefaultNamespace("item"));
    public static final ResourceKey<Registry<Object>> BLOCK = ResourceKey.createRegistryKey(Identifier.withDefaultNamespace("block"));
    public static final ResourceKey<Registry<Object>> DIMENSION = ResourceKey.createRegistryKey(Identifier.withDefaultNamespace("dimension"));
    public static final ResourceKey<Registry<Object>> CREATIVE_MODE_TAB = ResourceKey.createRegistryKey(Identifier.withDefaultNamespace("creative_mode_tab"));
}
