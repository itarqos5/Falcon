package net.falcon.jdk;

public final class LongExt {
    private LongExt() {}

    public static int signum(long l) { return l > 0 ? 1 : (l < 0 ? -1 : 0); }
    public static long sum(long a, long b) { return a + b; }
    public static String toHexString(long v) { if (v == 0) return "0"; StringBuilder sb = new StringBuilder(); long x = v; while (x != 0) { sb.append("0123456789abcdef".charAt((int) (x & 15))); x >>>= 4; } return sb.reverse().toString(); }
}
