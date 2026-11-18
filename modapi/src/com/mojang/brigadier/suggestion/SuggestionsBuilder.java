package com.mojang.brigadier.suggestion;

import java.util.ArrayList;
import java.util.List;

public class SuggestionsBuilder {
    private final List<String> items = new ArrayList<>();

    public SuggestionsBuilder suggest(String text) { items.add(text); return this; }
    public String getRemaining() { return ""; }
    public java.util.concurrent.CompletableFuture<Suggestions> buildFuture() { return java.util.concurrent.CompletableFuture.completedFuture(new Suggestions(items)); }
}
