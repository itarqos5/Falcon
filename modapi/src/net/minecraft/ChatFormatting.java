package net.minecraft;

public enum ChatFormatting {
    BLACK("BLACK", '0', 0x000000), DARK_BLUE("DARK_BLUE", '1', 0x0000AA), DARK_GREEN("DARK_GREEN", '2', 0x00AA00), DARK_AQUA("DARK_AQUA", '3', 0x00AAAA), DARK_RED("DARK_RED", '4', 0xAA0000),
    DARK_PURPLE("DARK_PURPLE", '5', 0xAA00AA), GOLD("GOLD", '6', 0xFFAA00), GRAY("GRAY", '7', 0xAAAAAA), DARK_GRAY("DARK_GRAY", '8', 0x555555), BLUE("BLUE", '9', 0x5555FF),
    GREEN("GREEN", 'a', 0x55FF55), AQUA("AQUA", 'b', 0x55FFFF), RED("RED", 'c', 0xFF5555), LIGHT_PURPLE("LIGHT_PURPLE", 'd', 0xFF55FF), YELLOW("YELLOW", 'e', 0xFFFF55), WHITE("WHITE", 'f', 0xFFFFFF),
    OBFUSCATED("OBFUSCATED", 'k', true), BOLD("BOLD", 'l', true), STRIKETHROUGH("STRIKETHROUGH", 'm', true), UNDERLINE("UNDERLINE", 'n', true), ITALIC("ITALIC", 'o', true), RESET("RESET", 'r', -1);

    private final String name;
    private final char code;
    private final boolean modifier;
    private final Integer color;

    ChatFormatting(String name, char code, int color) { this.name = name; this.code = code; this.modifier = false; this.color = color >= 0 ? Integer.valueOf(color) : null; }
    ChatFormatting(String name, char code, boolean modifier) { this.name = name; this.code = code; this.modifier = modifier; this.color = null; }

    public char getChar() { return code; }
    public Integer getColor() { return color; }
    public boolean isFormat() { return modifier; }
    public boolean isColor() { return !modifier && this != RESET; }
    public String getName() { return name.toLowerCase(); }
    @Override public String toString() { return "§" + code; }

    public static ChatFormatting getByCode(char code) {
        for (ChatFormatting f : values()) {
            if (f.code == code) return f;
        }
        return null;
    }

    public static String stripFormatting(String text) {
        if (text == null) return null;
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '§' && i + 1 < text.length()) { i++; continue; }
            out.append(c);
        }
        return out.toString();
    }
}
