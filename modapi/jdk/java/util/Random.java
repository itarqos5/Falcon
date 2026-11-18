package java.util;

public class Random {
    private long seed;

    public Random() { this(System.nanoTime() ^ 0x5DEECE66DL); }
    public Random(long seed) { this.seed = (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1); }

    protected int next(int bits) {
        seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
        return (int) (seed >>> (48 - bits));
    }
    public int nextInt() { return next(32); }
    public int nextInt(int bound) {
        if (bound <= 0) throw new IllegalArgumentException("bound must be positive");
        if ((bound & -bound) == bound) return (int) ((bound * (long) next(31)) >> 31);
        int bits, val;
        do { bits = next(31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
        return val;
    }
    public int nextInt(int origin, int bound) { return origin + nextInt(bound - origin); }
    public long nextLong() { return ((long) next(32) << 32) + next(32); }
    public boolean nextBoolean() { return next(1) != 0; }
    public double nextDouble() { return (((long) next(26) << 27) + next(27)) * 0x1.0p-53; }
    public float nextFloat() { return next(24) / (float) (1 << 24); }
}
