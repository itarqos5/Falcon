package net.minecraft.core;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import net.minecraft.resources.Identifier;

public class Registry<T> implements Iterable<T> {
    private final Identifier name;
    protected final Map<Identifier, T> byId = new HashMap<>();
    protected final Map<T, Identifier> byValue = new HashMap<>();
    protected final List<T> values = new ArrayList<>();

    public Registry(Identifier name) { this.name = name; }

    public Identifier name() { return name; }

    public static <V, T extends V> T register(Registry<V> registry, Identifier id, T value) {
        registry.add(id, value);
        net.falcon.Registrations.registered(registry, id, value);
        return value;
    }

    public static <V, T extends V> T register(Registry<V> registry, net.minecraft.resources.ResourceKey<V> key, T value) { return register(registry, key.identifier(), value); }
    public static <V, T extends V> T register(Registry<V> registry, String id, T value) { return register(registry, Identifier.parse(id), value); }

    public void add(Identifier id, T value) {
        if (!byId.containsKey(id)) values.add(value);
        byId.put(id, value);
        byValue.put(value, id);
    }

    public Identifier getKey(T value) { return byValue.get(value); }
    public T getValue(Identifier id) { return byId.get(id); }
    public java.util.Optional<T> get(Identifier id) { return java.util.Optional.ofNullable(byId.get(id)); }
    public boolean containsKey(Identifier id) { return byId.containsKey(id); }
    public int size() { return values.size(); }
    @Override public Iterator<T> iterator() { return values.iterator(); }
    public java.util.Set<Identifier> keySet() { return byId.keySet(); }
}
