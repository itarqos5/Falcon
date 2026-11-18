package net.minecraft.network.chat;

import java.util.List;
import net.minecraft.network.chat.contents.PlainTextContents;
import net.minecraft.network.chat.contents.TranslatableContents;

public interface Component extends FormattedText {
    Style getStyle();
    ComponentContents getContents();
    List<Component> getSiblings();
    MutableComponent copy();

    @Override
    default String getString() {
        StringBuilder out = new StringBuilder();
        visit(text -> { out.append(text); return java.util.Optional.empty(); });
        return out.toString();
    }

    static MutableComponent literal(String text) { return MutableComponent.create(PlainTextContents.create(text)); }
    static MutableComponent empty() { return MutableComponent.create(PlainTextContents.EMPTY); }
    static MutableComponent translatable(String key) { return MutableComponent.create(new TranslatableContents(key, null, new Object[0])); }
    static MutableComponent translatable(String key, Object... args) { return MutableComponent.create(new TranslatableContents(key, null, args)); }
    static MutableComponent translatableWithFallback(String key, String fallback, Object... args) { return MutableComponent.create(new TranslatableContents(key, fallback, args)); }
    static Component nullToEmpty(String text) { return text == null ? empty() : literal(text); }
    static MutableComponent keybind(String name) { return literal(name); }
}
