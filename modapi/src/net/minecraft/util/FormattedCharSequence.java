package net.minecraft.util;

public interface FormattedCharSequence {
    String text();

    static FormattedCharSequence of(String text) { return () -> text; }
    static FormattedCharSequence forward(String text, net.minecraft.network.chat.Style style) { return () -> text; }
}
