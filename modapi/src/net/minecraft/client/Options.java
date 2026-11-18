package net.minecraft.client;

import java.util.ArrayList;
import java.util.List;

public class Options {
    public KeyMapping[] keyMappings = new KeyMapping[0];
    public final KeyMapping keyShift = new KeyMapping("key.sneak", com.mojang.blaze3d.platform.InputConstants.Type.KEYBOARD, 340, KeyMapping.Category.MOVEMENT);
    public final KeyMapping keyJump = new KeyMapping("key.jump", com.mojang.blaze3d.platform.InputConstants.Type.KEYBOARD, 32, KeyMapping.Category.MOVEMENT);
    public final KeyMapping keyUse = new KeyMapping("key.use", com.mojang.blaze3d.platform.InputConstants.Type.MOUSE, 1, KeyMapping.Category.GAMEPLAY);
    public final KeyMapping keyAttack = new KeyMapping("key.attack", com.mojang.blaze3d.platform.InputConstants.Type.MOUSE, 0, KeyMapping.Category.GAMEPLAY);
    private final OptionInstance<Double> fov = new OptionInstance<>(70.0);
    private final OptionInstance<Integer> renderDistance = new OptionInstance<>(12);
    private final OptionInstance<Boolean> toggleCrouch = new OptionInstance<>(Boolean.FALSE);
    public boolean hideGui = false;
    public String languageCode = "en_us";

    void addKeyMapping(KeyMapping mapping) {
        KeyMapping[] next = new KeyMapping[keyMappings.length + 1];
        for (int i = 0; i < keyMappings.length; i++) next[i] = keyMappings[i];
        next[keyMappings.length] = mapping;
        keyMappings = next;
    }

    public OptionInstance<Double> fov() { return fov; }
    public OptionInstance<Integer> renderDistance() { return renderDistance; }
    public OptionInstance<Boolean> toggleCrouch() { return toggleCrouch; }
    public void load() {}
    public void save() {}
}
