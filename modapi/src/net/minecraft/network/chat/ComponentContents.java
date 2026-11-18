package net.minecraft.network.chat;

import java.util.Optional;

public interface ComponentContents {
    default <T> Optional<T> visit(FormattedText.StyledContentConsumer<T> consumer, Style style) { return Optional.empty(); }
    default <T> Optional<T> visit(FormattedText.ContentConsumer<T> consumer) { return Optional.empty(); }
}
