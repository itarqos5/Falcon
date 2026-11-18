package net.fabricmc.fabric.api.client.rendering.v1.hud;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.resources.Identifier;

public final class HudElementRegistry {
    private HudElementRegistry() {}

    public static final List<HudElement> ELEMENTS = new ArrayList<>();

    public static void addFirst(Identifier id, HudElement element) { ELEMENTS.add(0, element); }
    public static void addLast(Identifier id, HudElement element) { ELEMENTS.add(element); }
    public static void attachElementBefore(Identifier before, Identifier id, HudElement element) { ELEMENTS.add(element); }
    public static void attachElementAfter(Identifier after, Identifier id, HudElement element) { ELEMENTS.add(element); }
}
