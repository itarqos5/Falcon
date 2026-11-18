package net.minecraft.tags;

import net.minecraft.resources.Identifier;
import net.minecraft.world.item.Item;

public final class ItemTags {
    private ItemTags() {}

    public static final TagKey<Item> PICKAXES = tag("pickaxes");
    public static final TagKey<Item> AXES = tag("axes");
    public static final TagKey<Item> SHOVELS = tag("shovels");
    public static final TagKey<Item> HOES = tag("hoes");
    public static final TagKey<Item> SWORDS = tag("swords");
    public static final TagKey<Item> LOGS = tag("logs");
    public static final TagKey<Item> PLANKS = tag("planks");
    public static final TagKey<Item> WOOL = tag("wool");
    public static final TagKey<Item> SIGNS = tag("signs");
    public static final TagKey<Item> BEDS = tag("beds");

    private static TagKey<Item> tag(String name) { return new TagKey<>(Identifier.withDefaultNamespace(name)); }
}
