package java.time;

public final class Instant implements Comparable<Instant> {
    private final long millis;

    private Instant(long millis) { this.millis = millis; }
    public static Instant now() { return new Instant(System.currentTimeMillis()); }
    public static Instant ofEpochMilli(long m) { return new Instant(m); }
    public static Instant ofEpochSecond(long s) { return new Instant(s * 1000L); }
    public static Instant parse(CharSequence text) { return new Instant(0L); }
    public long toEpochMilli() { return millis; }
    public long getEpochSecond() { return millis / 1000L; }
    public boolean isBefore(Instant o) { return millis < o.millis; }
    public boolean isAfter(Instant o) { return millis > o.millis; }
    public Instant minusNanos(long n) { return new Instant(millis - n / 1000000L); }
    public Instant plusMillis(long n) { return new Instant(millis + n); }
    public Instant plusSeconds(long n) { return new Instant(millis + n * 1000L); }
    @Override public int compareTo(Instant o) { return Long.compare(millis, o.millis); }
    @Override public boolean equals(Object o) { return o instanceof Instant i && i.millis == millis; }
    @Override public int hashCode() { return Long.hashCode(millis); }
    @Override public String toString() { return "Instant[" + millis + "]"; }
}
