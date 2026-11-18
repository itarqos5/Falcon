package net.fabricmc.fabric.api.client.keybinding.v1;

import net.minecraft.client.KeyMapping;

public final class KeyBindingHelper {
    private KeyBindingHelper() {}

    public static KeyMapping registerKeyBinding(KeyMapping mapping) {
        KeyMapping.registerMod(mapping);
        return mapping;
    }
}
