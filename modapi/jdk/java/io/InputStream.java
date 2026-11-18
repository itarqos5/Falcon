package java.io;

public abstract class InputStream implements Closeable {
    public abstract int read() throws IOException;

    public int read(byte[] b) throws IOException { return read(b, 0, b.length); }

    public int read(byte[] b, int off, int len) throws IOException {
        if (len == 0) return 0;
        int c = read();
        if (c == -1) return -1;
        b[off] = (byte) c;
        int i = 1;
        for (; i < len; i++) {
            c = read();
            if (c == -1) break;
            b[off + i] = (byte) c;
        }
        return i;
    }

    public byte[] readAllBytes() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int c;
        while ((c = read()) != -1) out.write(c);
        return out.toByteArray();
    }

    public long skip(long n) throws IOException { long s = 0; while (s < n && read() != -1) s++; return s; }
    public int available() throws IOException { return 0; }
    @Override public void close() throws IOException {}
}
