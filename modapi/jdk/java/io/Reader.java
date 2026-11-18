package java.io;

public abstract class Reader implements Closeable {
    public abstract int read() throws IOException;

    public int read(char[] buf) throws IOException { return read(buf, 0, buf.length); }
    public int read(char[] buf, int off, int len) throws IOException {
        int i = 0;
        for (; i < len; i++) {
            int c = read();
            if (c == -1) return i == 0 ? -1 : i;
            buf[off + i] = (char) c;
        }
        return i;
    }
    @Override public void close() throws IOException {}
}
