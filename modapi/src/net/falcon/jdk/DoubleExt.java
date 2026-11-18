package net.falcon.jdk;

public final class DoubleExt {
    private DoubleExt() {}

    public static boolean isFinite(double d) { return !(d != d) && d != Double.POSITIVE_INFINITY && d != Double.NEGATIVE_INFINITY; }
    public static boolean isNaN(double d) { return d != d; }
    public static int compare(double a, double b) { return a < b ? -1 : (a > b ? 1 : (a == b ? 0 : (a != a ? (b != b ? 0 : 1) : -1))); }
}
