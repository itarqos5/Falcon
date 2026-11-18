package java.io;

public class StringWriter extends Writer {
    private final StringBuilder sb = new StringBuilder();

    @Override public void write(int c) { sb.append((char) c); }
    @Override public void write(String s) { sb.append(s); }
    @Override public String toString() { return sb.toString(); }
    public StringBuffer getBuffer() { return new StringBuffer(sb.toString()); }
}
