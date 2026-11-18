package java.util.concurrent.atomic;

public class AtomicLong extends Number {
    private long value;

    public AtomicLong() {}
    public AtomicLong(long v) { value = v; }
    public long get() { return value; }
    public void set(long v) { value = v; }
    public long incrementAndGet() { return ++value; }
    public long getAndIncrement() { return value++; }
    public long addAndGet(long d) { value += d; return value; }
    public boolean compareAndSet(long expect, long update) { if (value != expect) return false; value = update; return true; }
    @Override public int intValue() { return (int) value; }
    @Override public long longValue() { return value; }
    @Override public float floatValue() { return value; }
    @Override public double doubleValue() { return value; }
    @Override public String toString() { return Long.toString(value); }
}
