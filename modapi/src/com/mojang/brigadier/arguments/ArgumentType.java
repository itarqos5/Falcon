package com.mojang.brigadier.arguments;

public interface ArgumentType<T> {
    // The value of the text at the start of the input, and how much of it was used; null when it does not fit.
    T parse(String input, int[] used);
}
