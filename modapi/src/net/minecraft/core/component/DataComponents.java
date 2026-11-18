package net.minecraft.core.component;

public final class DataComponents {
    private DataComponents() {}

    public static final DataComponentType<Object> CUSTOM_DATA = new DataComponentType<>("custom_data");
    public static final DataComponentType<Object> FOOD = new DataComponentType<>("food");
    public static final DataComponentType<Object> LORE = new DataComponentType<>("lore");
    public static final DataComponentType<Object> CUSTOM_NAME = new DataComponentType<>("custom_name");
    public static final DataComponentType<Object> ITEM_NAME = new DataComponentType<>("item_name");
}
