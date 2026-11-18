package net.minecraft.network.chat;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.UnaryOperator;
import net.minecraft.ChatFormatting;

public final class MutableComponent implements Component {
    private final ComponentContents contents;
    private final List<Component> siblings = new ArrayList<>();
    private Style style = Style.EMPTY;

    private MutableComponent(ComponentContents contents) { this.contents = contents; }

    public static MutableComponent create(ComponentContents contents) { return new MutableComponent(contents); }

    @Override public ComponentContents getContents() { return contents; }
    @Override public List<Component> getSiblings() { return siblings; }
    @Override public Style getStyle() { return style; }

    public MutableComponent setStyle(Style style) { this.style = style; return this; }
    public MutableComponent append(String text) { return append(Component.literal(text)); }
    public MutableComponent append(Component sibling) { siblings.add(sibling); return this; }
    public MutableComponent withStyle(UnaryOperator<Style> op) { style = op.apply(style); return this; }
    public MutableComponent withStyle(Style s) { style = s.applyTo(style); return this; }
    public MutableComponent withStyle(ChatFormatting f) { style = style.applyFormat(f); return this; }
    public MutableComponent withStyle(ChatFormatting... formats) { style = style.applyFormats(formats); return this; }
    public MutableComponent withColor(int rgb) { style = style.withColor(rgb); return this; }

    @Override
    public MutableComponent copy() {
        MutableComponent c = new MutableComponent(contents);
        c.siblings.addAll(siblings);
        c.style = style;
        return c;
    }

    @Override
    public <T> Optional<T> visit(StyledContentConsumer<T> consumer, Style parent) {
        Style own = style.applyTo(parent);
        Optional<T> r = contents.visit(consumer, own);
        if (r.isPresent()) return r;
        for (Component s : siblings) {
            r = s.visit(consumer, own);
            if (r.isPresent()) return r;
        }
        return Optional.empty();
    }

    @Override
    public <T> Optional<T> visit(ContentConsumer<T> consumer) {
        Optional<T> r = contents.visit(consumer);
        if (r.isPresent()) return r;
        for (Component s : siblings) {
            r = s.visit(consumer);
            if (r.isPresent()) return r;
        }
        return Optional.empty();
    }

    @Override public String toString() { return getString(); }
    @Override public boolean equals(Object o) { return o instanceof MutableComponent m && m.contents.equals(contents) && m.siblings.equals(siblings) && m.style.equals(style); }
    @Override public int hashCode() { return contents.hashCode() * 31 + siblings.hashCode(); }
}
