package java.util;

public final class UUID implements Comparable<UUID> {
    private final long most, least;

    public UUID(long most, long least) { this.most = most; this.least = least; }

    public static UUID randomUUID() {
        long a = (long) (Math.random() * 9.007199254740992E15) ^ (System.nanoTime() << 11), b = (long) (Math.random() * 9.007199254740992E15) * 1099511628211L ^ System.currentTimeMillis();
        a = (a & 0xFFFFFFFFFFFF0FFFL) | 0x4000L;
        b = (b & 0x3FFFFFFFFFFFFFFFL) | 0x8000000000000000L;
        return new UUID(a, b);
    }

    public static UUID nameUUIDFromBytes(byte[] name) {
        long h1 = 1125899906842597L, h2 = 0x9E3779B97F4A7C15L;
        for (byte b : name) { h1 = 31 * h1 + b; h2 = (h2 ^ b) * 1099511628211L; }
        h1 = (h1 & 0xFFFFFFFFFFFF0FFFL) | 0x3000L;
        h2 = (h2 & 0x3FFFFFFFFFFFFFFFL) | 0x8000000000000000L;
        return new UUID(h1, h2);
    }

    public static UUID fromString(String s) {
        String[] parts = s.split("-");
        if (parts.length != 5) throw new IllegalArgumentException("Invalid UUID string: " + s);
        long m = (Long.parseLong(parts[0], 16) << 32) | (Long.parseLong(parts[1], 16) << 16) | Long.parseLong(parts[2], 16);
        long l = (Long.parseLong(parts[3], 16) << 48) | Long.parseLong(parts[4], 16);
        return new UUID(m, l);
    }

    public long getMostSignificantBits() { return most; }
    public long getLeastSignificantBits() { return least; }
    public int version() { return (int) ((most >> 12) & 0x0f); }

    private static String digits(long val, int count) {
        String h = Long.toHexString(val & ((1L << (count * 4)) - 1L));
        StringBuilder sb = new StringBuilder();
        for (int i = h.length(); i < count; i++) sb.append('0');
        return sb.append(h).toString();
    }

    @Override public String toString() { return digits(most >>> 32, 8) + "-" + digits(most >>> 16, 4) + "-" + digits(most, 4) + "-" + digits(least >>> 48, 4) + "-" + digits(least, 12); }
    @Override public boolean equals(Object o) { return o instanceof UUID u && u.most == most && u.least == least; }
    @Override public int hashCode() { long x = most ^ least; return (int) (x >> 32) ^ (int) x; }
    @Override public int compareTo(UUID o) { return most < o.most ? -1 : (most > o.most ? 1 : (least < o.least ? -1 : (least > o.least ? 1 : 0))); }
}
