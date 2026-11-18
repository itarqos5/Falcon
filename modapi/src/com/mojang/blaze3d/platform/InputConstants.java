package com.mojang.blaze3d.platform;

import net.falcon.Host;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.network.chat.Component;

public final class InputConstants {
    private InputConstants() {}

    public static final Key UNKNOWN = Type.KEYBOARD.getOrCreate(-1);

    public enum Type {
        KEYBOARD("key.keyboard"), MOUSE("key.mouse");
        private final String prefix;
        Type(String prefix) { this.prefix = prefix; }
        public Key getOrCreate(int value) { return new Key(prefix, this, value); }
        public String prefix() { return prefix; }
    }

    public static final class Key {
        private final String prefix;
        private final Type type;
        private final int value;

        Key(String prefix, Type type, int value) { this.prefix = prefix; this.type = type; this.value = value; }

        public Type getType() { return type; }
        public int getValue() { return value; }

        public String getName() {
            if (value < 0) return prefix + ".unknown";
            if (type == Type.MOUSE) return value == 0 ? "key.mouse.left" : (value == 1 ? "key.mouse.right" : (value == 2 ? "key.mouse.middle" : "key.mouse." + (value + 1)));
            return prefix + "." + KeyNames.nameOf(value);
        }

        public Component getDisplayName() { return Component.translatable(getName()); }
        @Override public boolean equals(Object o) { return o instanceof Key k && k.type == type && k.value == value; }
        @Override public int hashCode() { return type.ordinal() * 1000003 + value; }
        @Override public String toString() { return getName(); }
    }

    // "key.keyboard.h", "key.mouse.left"
    public static Key getKey(String name) {
        if (name == null) return UNKNOWN;
        if (name.startsWith("key.mouse.")) {
            String rest = name.substring("key.mouse.".length());
            if (rest.equals("left")) return Type.MOUSE.getOrCreate(0);
            if (rest.equals("right")) return Type.MOUSE.getOrCreate(1);
            if (rest.equals("middle")) return Type.MOUSE.getOrCreate(2);
            try { return Type.MOUSE.getOrCreate(Integer.parseInt(rest) - 1); } catch (NumberFormatException e) { return UNKNOWN; }
        }
        if (name.startsWith("key.keyboard.")) {
            int code = KeyNames.codeOf(name.substring("key.keyboard.".length()));
            return code < 0 ? UNKNOWN : Type.KEYBOARD.getOrCreate(code);
        }
        return UNKNOWN;
    }

    public static Key getKey(KeyEvent event) { return Type.KEYBOARD.getOrCreate(event.key()); }
    public static boolean isKeyDown(int glfwKey) { return Host.keyDown(glfwKey); }
}
