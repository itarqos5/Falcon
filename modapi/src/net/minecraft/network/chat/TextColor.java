package net.minecraft.network.chat;

public final class TextColor {
    private final int value;

    private TextColor(int value) { this.value = value; }

    public static TextColor fromRgb(int rgb) { return new TextColor(rgb & 0xFFFFFF); }
    public static TextColor fromLegacyFormat(net.minecraft.ChatFormatting f) { return f.getColor() == null ? null : new TextColor(f.getColor()); }
    public int getValue() { return value; }
    public String serialize() { String h = Integer.toHexString(value); while (h.length() < 6) h = "0" + h; return "#" + h; }
    @Override public boolean equals(Object o) { return o instanceof TextColor c && c.value == value; }
    @Override public int hashCode() { return value; }
    @Override public String toString() { return serialize(); }
}
