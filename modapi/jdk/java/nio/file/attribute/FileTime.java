package java.nio.file.attribute;

public final class FileTime implements Comparable<FileTime> {
    private final long millis;

    private FileTime(long millis) { this.millis = millis; }
    public static FileTime fromMillis(long millis) { return new FileTime(millis); }
    public long toMillis() { return millis; }
    public java.time.Instant toInstant() { return java.time.Instant.ofEpochMilli(millis); }
    @Override public int compareTo(FileTime o) { return Long.compare(millis, o.millis); }
}
