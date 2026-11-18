package java.util;

public final class OptionalInt {
    private final boolean present;
    private final int value;

    private OptionalInt(boolean present, int value) { this.present = present; this.value = value; }
    public static OptionalInt empty() { return new OptionalInt(false, 0); }
    public static OptionalInt of(int v) { return new OptionalInt(true, v); }
    public boolean isPresent() { return present; }
    public boolean isEmpty() { return !present; }
    public int getAsInt() { if (!present) throw new NoSuchElementException("No value present"); return value; }
    public int orElse(int other) { return present ? value : other; }
}
