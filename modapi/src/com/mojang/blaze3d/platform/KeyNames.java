package com.mojang.blaze3d.platform;

// The names the game gives keyboard keys in its options file (key.keyboard.<name>), and GLFW's codes for them.
final class KeyNames {
    private KeyNames() {}

    private static final String[] SPECIAL_NAMES = {"space", "apostrophe", "comma", "minus", "period", "slash", "semicolon", "equal", "left.bracket", "backslash", "right.bracket", "grave.accent", "world.1", "world.2",
        "escape", "enter", "tab", "backspace", "insert", "delete", "right", "left", "down", "up", "page.up", "page.down", "home", "end", "caps.lock", "scroll.lock", "num.lock", "print.screen", "pause",
        "left.shift", "left.control", "left.alt", "left.win", "right.shift", "right.control", "right.alt", "right.win", "menu"};
    private static final int[] SPECIAL_CODES = {32, 39, 44, 45, 46, 47, 59, 61, 91, 92, 93, 96, 161, 162, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 280, 281, 282, 283, 284,
        340, 341, 342, 343, 344, 345, 346, 347, 348};

    static String nameOf(int code) {
        if (code >= 65 && code <= 90) return String.valueOf((char) (code + 32));
        if (code >= 48 && code <= 57) return String.valueOf((char) code);
        if (code >= 290 && code <= 314) return "f" + (code - 289);
        if (code >= 320 && code <= 329) return "keypad." + (code - 320);
        for (int i = 0; i < SPECIAL_CODES.length; i++) {
            if (SPECIAL_CODES[i] == code) return SPECIAL_NAMES[i];
        }
        return "unknown";
    }

    static int codeOf(String name) {
        if (name.length() == 1) {
            char c = name.charAt(0);
            if (c >= 'a' && c <= 'z') return c - 32;
            if (c >= '0' && c <= '9') return c;
        }
        if (name.startsWith("f") && name.length() <= 3) {
            try { int n = Integer.parseInt(name.substring(1)); if (n >= 1 && n <= 25) return 289 + n; } catch (NumberFormatException e) { }
        }
        if (name.startsWith("keypad.")) {
            try { return 320 + Integer.parseInt(name.substring(7)); } catch (NumberFormatException e) { }
        }
        for (int i = 0; i < SPECIAL_NAMES.length; i++) {
            if (SPECIAL_NAMES[i].equals(name)) return SPECIAL_CODES[i];
        }
        return -1;
    }
}
