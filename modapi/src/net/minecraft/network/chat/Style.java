package net.minecraft.network.chat;

import net.minecraft.ChatFormatting;

public final class Style {
    public static final Style EMPTY = new Style(null, null, null, null, null, null, null, null);

    private final TextColor color;
    private final Boolean bold, italic, underlined, strikethrough, obfuscated;
    private final ClickEvent clickEvent;
    private final HoverEvent hoverEvent;

    private Style(TextColor color, Boolean bold, Boolean italic, Boolean underlined, Boolean strikethrough, Boolean obfuscated, ClickEvent clickEvent, HoverEvent hoverEvent) {
        this.color = color; this.bold = bold; this.italic = italic; this.underlined = underlined; this.strikethrough = strikethrough; this.obfuscated = obfuscated;
        this.clickEvent = clickEvent; this.hoverEvent = hoverEvent;
    }

    public TextColor getColor() { return color; }
    public boolean isBold() { return bold == Boolean.TRUE; }
    public boolean isItalic() { return italic == Boolean.TRUE; }
    public boolean isUnderlined() { return underlined == Boolean.TRUE; }
    public boolean isStrikethrough() { return strikethrough == Boolean.TRUE; }
    public boolean isObfuscated() { return obfuscated == Boolean.TRUE; }
    public boolean isEmpty() { return this == EMPTY || (color == null && bold == null && italic == null && underlined == null && strikethrough == null && obfuscated == null && clickEvent == null && hoverEvent == null); }
    public ClickEvent getClickEvent() { return clickEvent; }
    public HoverEvent getHoverEvent() { return hoverEvent; }

    public Style withColor(TextColor c) { return new Style(c, bold, italic, underlined, strikethrough, obfuscated, clickEvent, hoverEvent); }
    public Style withColor(int rgb) { return withColor(TextColor.fromRgb(rgb)); }
    public Style withColor(ChatFormatting f) { return withColor(TextColor.fromLegacyFormat(f)); }
    public Style withBold(Boolean v) { return new Style(color, v, italic, underlined, strikethrough, obfuscated, clickEvent, hoverEvent); }
    public Style withItalic(Boolean v) { return new Style(color, bold, v, underlined, strikethrough, obfuscated, clickEvent, hoverEvent); }
    public Style withUnderlined(Boolean v) { return new Style(color, bold, italic, v, strikethrough, obfuscated, clickEvent, hoverEvent); }
    public Style withStrikethrough(Boolean v) { return new Style(color, bold, italic, underlined, v, obfuscated, clickEvent, hoverEvent); }
    public Style withObfuscated(Boolean v) { return new Style(color, bold, italic, underlined, strikethrough, v, clickEvent, hoverEvent); }
    public Style withClickEvent(ClickEvent e) { return new Style(color, bold, italic, underlined, strikethrough, obfuscated, e, hoverEvent); }
    public Style withHoverEvent(HoverEvent e) { return new Style(color, bold, italic, underlined, strikethrough, obfuscated, clickEvent, e); }

    public Style applyFormat(ChatFormatting f) {
        if (f == ChatFormatting.RESET) return EMPTY;
        if (f.isColor()) return withColor(f);
        switch (f) {
            case BOLD: return withBold(Boolean.TRUE);
            case ITALIC: return withItalic(Boolean.TRUE);
            case UNDERLINE: return withUnderlined(Boolean.TRUE);
            case STRIKETHROUGH: return withStrikethrough(Boolean.TRUE);
            case OBFUSCATED: return withObfuscated(Boolean.TRUE);
            default: return this;
        }
    }

    public Style applyFormats(ChatFormatting... formats) { Style s = this; for (ChatFormatting f : formats) s = s.applyFormat(f); return s; }

    // This style where it says nothing, the parent's where it does.
    public Style applyTo(Style parent) {
        if (isEmpty()) return parent;
        if (parent.isEmpty()) return this;
        return new Style(color != null ? color : parent.color, bold != null ? bold : parent.bold, italic != null ? italic : parent.italic, underlined != null ? underlined : parent.underlined,
            strikethrough != null ? strikethrough : parent.strikethrough, obfuscated != null ? obfuscated : parent.obfuscated, clickEvent != null ? clickEvent : parent.clickEvent, hoverEvent != null ? hoverEvent : parent.hoverEvent);
    }

    @Override public boolean equals(Object o) {
        return o instanceof Style s && java.util.Objects.equals(color, s.color) && java.util.Objects.equals(bold, s.bold) && java.util.Objects.equals(italic, s.italic) && java.util.Objects.equals(underlined, s.underlined)
            && java.util.Objects.equals(strikethrough, s.strikethrough) && java.util.Objects.equals(obfuscated, s.obfuscated) && java.util.Objects.equals(clickEvent, s.clickEvent) && java.util.Objects.equals(hoverEvent, s.hoverEvent);
    }
    @Override public int hashCode() { return java.util.Objects.hash(color, bold, italic, underlined, strikethrough, obfuscated, clickEvent, hoverEvent); }
}
