package java.nio.charset;

public class Charset {
    private final String name;

    Charset(String name) { this.name = name; }
    public static Charset forName(String name) { return new Charset(name); }
    public static Charset defaultCharset() { return StandardCharsets.UTF_8; }
    public String name() { return name; }
    @Override public String toString() { return name; }
}
