package java.io;

public abstract class OutputStream implements Closeable, Flushable {
    public abstract void write(int b) throws IOException;
    public void write(byte[] b) throws IOException { write(b, 0, b.length); }
    public void write(byte[] b, int off, int len) throws IOException { for (int i = 0; i < len; i++) write(b[off + i]); }
    @Override public void flush() throws IOException {}
    @Override public void close() throws IOException {}
}
