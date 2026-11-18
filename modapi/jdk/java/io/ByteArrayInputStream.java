package java.io;

public class ByteArrayInputStream extends InputStream {
    private final byte[] buf;
    private int pos, count;

    public ByteArrayInputStream(byte[] buf) { this.buf = buf; this.count = buf.length; }
    public ByteArrayInputStream(byte[] buf, int offset, int length) { this.buf = buf; this.pos = offset; this.count = Math.min(offset + length, buf.length); }

    @Override public int read() { return pos < count ? (buf[pos++] & 0xff) : -1; }
    @Override public int available() { return count - pos; }
}
