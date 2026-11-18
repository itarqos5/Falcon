package java.util.concurrent.atomic;

public class AtomicInteger extends Number {
    private int value;

    public AtomicInteger() {}
    public AtomicInteger(int v) { value = v; }
    public int get() { return value; }
    public void set(int v) { value = v; }
    public int getAndSet(int v) { int o = value; value = v; return o; }
    public int incrementAndGet() { return ++value; }
    public int decrementAndGet() { return --value; }
    public int getAndIncrement() { return value++; }
    public int getAndDecrement() { return value--; }
    public int addAndGet(int d) { value += d; return value; }
    public int getAndAdd(int d) { int o = value; value += d; return o; }
    public boolean compareAndSet(int expect, int update) { if (value != expect) return false; value = update; return true; }
    @Override public int intValue() { return value; }
    @Override public long longValue() { return value; }
    @Override public float floatValue() { return value; }
    @Override public double doubleValue() { return value; }
    @Override public String toString() { return Integer.toString(value); }
}
