package net.minecraft.network.chat.contents;

import java.util.Optional;
import net.minecraft.network.chat.ComponentContents;
import net.minecraft.network.chat.FormattedText;
import net.minecraft.network.chat.Style;

public record PlainTextContents(String text) implements ComponentContents {
    public static final PlainTextContents EMPTY = new PlainTextContents("");

    public static PlainTextContents create(String text) { return text.isEmpty() ? EMPTY : new PlainTextContents(text); }
    @Override public <T> Optional<T> visit(FormattedText.StyledContentConsumer<T> consumer, Style style) { return consumer.accept(style, text); }
    @Override public <T> Optional<T> visit(FormattedText.ContentConsumer<T> consumer) { return consumer.accept(text); }
    @Override public String toString() { return "literal{" + text + "}"; }
}
