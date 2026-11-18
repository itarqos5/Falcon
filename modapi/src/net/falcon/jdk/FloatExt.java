package net.falcon.jdk;

public final class FloatExt {
    private FloatExt() {}

    public static boolean isFinite(float f) { return !(f != f) && f != Float.POSITIVE_INFINITY && f != Float.NEGATIVE_INFINITY; }
    public static boolean isNaN(float f) { return f != f; }
    public static int compare(float a, float b) { return a < b ? -1 : (a > b ? 1 : (a == b ? 0 : (a != a ? (b != b ? 0 : 1) : -1))); }
}
