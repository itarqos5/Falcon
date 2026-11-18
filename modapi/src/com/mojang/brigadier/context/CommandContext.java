package com.mojang.brigadier.context;

import java.util.Map;

public class CommandContext<S> {
    private final S source;
    private final Map<String, Object> arguments;

    public CommandContext(S source, Map<String, Object> arguments) { this.source = source; this.arguments = arguments; }

    public S getSource() { return source; }

    @SuppressWarnings("unchecked")
    public <V> V getArgument(String name, Class<V> type) { return (V) arguments.get(name); }
}
