package com.mojang.brigadier.builder;

import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.suggestion.SuggestionProvider;
import com.mojang.brigadier.tree.CommandNode;

public class RequiredArgumentBuilder<S, T> extends ArgumentBuilder<S, RequiredArgumentBuilder<S, T>> {
    private final String name;
    private final ArgumentType<T> type;
    private SuggestionProvider<S> suggestions;

    private RequiredArgumentBuilder(String name, ArgumentType<T> type) { this.name = name; this.type = type; }

    public static <S, T> RequiredArgumentBuilder<S, T> argument(String name, ArgumentType<T> type) { return new RequiredArgumentBuilder<>(name, type); }
    public RequiredArgumentBuilder<S, T> suggests(SuggestionProvider<S> provider) { suggestions = provider; return this; }
    @Override protected RequiredArgumentBuilder<S, T> getThis() { return this; }

    @Override
    public CommandNode<S> build() {
        CommandNode<S> node = new CommandNode<>(name, type);
        fill(node);
        return node;
    }
}
