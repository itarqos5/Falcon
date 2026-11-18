package java.time;

public final class Duration {
    private final long millis;

    private Duration(long millis) { this.millis = millis; }
    public static Duration ofMillis(long m) { return new Duration(m); }
    public static Duration ofSeconds(long s) { return new Duration(s * 1000L); }
    public static Duration between(Instant a, Instant b) { return new Duration(b.toEpochMilli() - a.toEpochMilli()); }
    public long toMillis() { return millis; }
    public long getSeconds() { return millis / 1000L; }
}
