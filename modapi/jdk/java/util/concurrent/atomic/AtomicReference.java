package java.util.concurrent.atomic;

public class AtomicReference<V> {
    private V value;

    public AtomicReference() {}
    public AtomicReference(V v) { value = v; }
    public V get() { return value; }
    public void set(V v) { value = v; }
    public V getAndSet(V v) { V o = value; value = v; return o; }
    public boolean compareAndSet(V expect, V update) { if (value != expect) return false; value = update; return true; }
    public V updateAndGet(java.util.function.UnaryOperator<V> f) { value = f.apply(value); return value; }
}
