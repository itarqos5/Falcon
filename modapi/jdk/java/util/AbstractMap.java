package java.util;

public abstract class AbstractMap<K, V> {
    public static class SimpleEntry<K, V> implements Map.Entry<K, V> {
        private final K key;
        private V value;

        public SimpleEntry(K key, V value) { this.key = key; this.value = value; }
        @Override public K getKey() { return key; }
        @Override public V getValue() { return value; }
        @Override public V setValue(V v) { V o = value; value = v; return o; }
        @Override public boolean equals(Object o) { return o instanceof Map.Entry<?, ?> e && Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue()); }
        @Override public int hashCode() { return Objects.hashCode(key) ^ Objects.hashCode(value); }
        @Override public String toString() { return key + "=" + value; }
    }

    public static class SimpleImmutableEntry<K, V> extends SimpleEntry<K, V> {
        public SimpleImmutableEntry(K key, V value) { super(key, value); }
    }
}
