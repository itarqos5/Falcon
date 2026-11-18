package net.minecraft.network.chat;

import java.util.Optional;

public interface FormattedText {
    FormattedText EMPTY = new FormattedText() {
        @Override public <T> Optional<T> visit(ContentConsumer<T> consumer) { return Optional.empty(); }
        @Override public <T> Optional<T> visit(StyledContentConsumer<T> consumer, Style style) { return Optional.empty(); }
    };

    <T> Optional<T> visit(ContentConsumer<T> consumer);
    <T> Optional<T> visit(StyledContentConsumer<T> consumer, Style style);

    default String getString() {
        StringBuilder out = new StringBuilder();
        visit(text -> { out.append(text); return Optional.empty(); });
        return out.toString();
    }

    static FormattedText of(String text) {
        return new FormattedText() {
            @Override public <T> Optional<T> visit(ContentConsumer<T> consumer) { return consumer.accept(text); }
            @Override public <T> Optional<T> visit(StyledContentConsumer<T> consumer, Style style) { return consumer.accept(style, text); }
        };
    }

    interface ContentConsumer<T> { Optional<T> accept(String text); }
    interface StyledContentConsumer<T> { Optional<T> accept(Style style, String text); }
}
