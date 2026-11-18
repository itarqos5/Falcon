package com.mojang.brigadier.suggestion;

import com.mojang.brigadier.context.CommandContext;

@FunctionalInterface
public interface SuggestionProvider<S> {
    java.util.concurrent.CompletableFuture<Suggestions> getSuggestions(CommandContext<S> context, SuggestionsBuilder builder);
}
