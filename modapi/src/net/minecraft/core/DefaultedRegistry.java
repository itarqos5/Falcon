package net.minecraft.core;

import net.minecraft.resources.Identifier;

public class DefaultedRegistry<T> extends Registry<T> {
    private final Identifier defaultKey;

    public DefaultedRegistry(String defaultKey, Identifier name) { super(name); this.defaultKey = Identifier.parse(defaultKey); }

    @Override public Identifier getKey(T value) { Identifier id = super.getKey(value); return id == null ? defaultKey : id; }
    @Override public T getValue(Identifier id) { T v = super.getValue(id); return v == null ? super.getValue(defaultKey) : v; }
}
