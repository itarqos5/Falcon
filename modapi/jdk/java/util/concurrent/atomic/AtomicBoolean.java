package java.util.concurrent.atomic;

public class AtomicBoolean {
    private boolean value;

    public AtomicBoolean() {}
    public AtomicBoolean(boolean v) { value = v; }
    public boolean get() { return value; }
    public void set(boolean v) { value = v; }
    public boolean getAndSet(boolean v) { boolean o = value; value = v; return o; }
    public boolean compareAndSet(boolean expect, boolean update) { if (value != expect) return false; value = update; return true; }
    @Override public String toString() { return Boolean.toString(value); }
}
