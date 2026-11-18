package net.falcon.jdk;

public final class MathExt {
    private MathExt() {}

    public static double hypot(double x, double y) { return Math.sqrt(x * x + y * y); }
    private static long randomSeed = System.nanoTime() ^ 0x5DEECE66DL;
    public static double random() { randomSeed = (randomSeed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1); return (randomSeed >>> 22) / (double) (1L << 26); }
    public static double toRadians(double d) { return d / 180.0 * Math.PI; }
    public static double toDegrees(double r) { return r * 180.0 / Math.PI; }
    public static double signum(double d) { return d > 0 ? 1.0 : (d < 0 ? -1.0 : d); }
    public static float signum(float f) { return f > 0 ? 1.0F : (f < 0 ? -1.0F : f); }
    public static int floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a ^ b) < 0)) q--; return q; }
    public static long floorDiv(long a, long b) { long q = a / b; if ((a % b != 0) && ((a ^ b) < 0)) q--; return q; }
    public static int floorMod(int a, int b) { int m = a % b; if (m != 0 && ((m ^ b) < 0)) m += b; return m; }
    public static long floorMod(long a, long b) { long m = a % b; if (m != 0 && ((m ^ b) < 0)) m += b; return m; }
    public static int floorMod(long a, int b) { return (int) floorMod(a, (long) b); }
    public static double rint(double d) { double f = Math.floor(d), diff = d - f; if (diff < 0.5) return f; if (diff > 0.5) return f + 1; return ((long) f % 2 == 0) ? f : f + 1; }
    public static int clamp(long v, int min, int max) { return (int) Math.min(max, Math.max(v, min)); }
    public static double clamp(double v, double min, double max) { return Math.min(max, Math.max(v, min)); }
    public static float clamp(float v, float min, float max) { return Math.min(max, Math.max(v, min)); }
}
