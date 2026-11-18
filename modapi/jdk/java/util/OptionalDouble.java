package java.util;

public final class OptionalDouble {
    private final boolean present;
    private final double value;

    private OptionalDouble(boolean present, double value) { this.present = present; this.value = value; }
    public static OptionalDouble empty() { return new OptionalDouble(false, 0); }
    public static OptionalDouble of(double v) { return new OptionalDouble(true, v); }
    public boolean isPresent() { return present; }
    public double getAsDouble() { if (!present) throw new NoSuchElementException("No value present"); return value; }
    public double orElse(double other) { return present ? value : other; }
}
