package net.falcon.jdk;

import java.util.HashMap;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

public final class MapExt {
    private MapExt() {}

    public static <K, V> V computeIfAbsent(Map<K, V> m, K key, Function<? super K, ? extends V> f) {
        V v = m.get(key);
        if (v == null) { v = f.apply(key); if (v != null) m.put(key, v); }
        return v;
    }
    public static <K, V> V computeIfPresent(Map<K, V> m, K key, BiFunction<? super K, ? super V, ? extends V> f) {
        V old = m.get(key);
        if (old == null) return null;
        V v = f.apply(key, old);
        if (v == null) m.remove(key); else m.put(key, v);
        return v;
    }
    public static <K, V> V compute(Map<K, V> m, K key, BiFunction<? super K, ? super V, ? extends V> f) {
        V v = f.apply(key, m.get(key));
        if (v == null) m.remove(key); else m.put(key, v);
        return v;
    }
    public static <K, V> V merge(Map<K, V> m, K key, V value, BiFunction<? super V, ? super V, ? extends V> f) {
        V old = m.get(key);
        V v = old == null ? value : f.apply(old, value);
        if (v == null) m.remove(key); else m.put(key, v);
        return v;
    }
    public static <K, V> V getOrDefault(Map<K, V> m, Object key, V def) { return m.containsKey(key) ? m.get(key) : def; }
    public static <K, V> V putIfAbsent(Map<K, V> m, K key, V value) { V v = m.get(key); if (v == null) m.put(key, value); return v; }
    public static <K, V> boolean remove(Map<K, V> m, Object key, Object value) { if (m.containsKey(key) && java.util.Objects.equals(m.get(key), value)) { m.remove(key); return true; } return false; }
    public static <K, V> void forEach(Map<K, V> m, BiConsumer<? super K, ? super V> action) { for (Map.Entry<K, V> e : new java.util.ArrayList<>(m.entrySet())) action.accept(e.getKey(), e.getValue()); }
    public static <K, V> void replaceAll(Map<K, V> m, BiFunction<? super K, ? super V, ? extends V> f) { for (Map.Entry<K, V> e : new java.util.ArrayList<>(m.entrySet())) m.put(e.getKey(), f.apply(e.getKey(), e.getValue())); }
    public static <K, V> Map<K, V> copyOf(Map<? extends K, ? extends V> m) { return new HashMap<>(m); }
    public static <K, V> Map<K, V> of() { return new HashMap<>(); }
    public static <K, V> Map<K, V> of(K k1, V v1) { Map<K, V> m = new HashMap<>(); m.put(k1, v1); return m; }
    public static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2) { Map<K, V> m = of(k1, v1); m.put(k2, v2); return m; }
    public static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3) { Map<K, V> m = of(k1, v1, k2, v2); m.put(k3, v3); return m; }
    public static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4) { Map<K, V> m = of(k1, v1, k2, v2, k3, v3); m.put(k4, v4); return m; }
    public static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5) { Map<K, V> m = of(k1, v1, k2, v2, k3, v3, k4, v4); m.put(k5, v5); return m; }
    public static <K, V> Map.Entry<K, V> entry(K k, V v) { return new java.util.AbstractMap.SimpleEntry<>(k, v); }
}
