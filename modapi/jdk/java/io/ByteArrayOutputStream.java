package java.io;

public class ByteArrayOutputStream extends OutputStream {
    private byte[] buf;
    private int count;

    public ByteArrayOutputStream() { this(32); }
    public ByteArrayOutputStream(int size) { buf = new byte[Math.max(size, 16)]; }

    @Override
    public void write(int b) {
        if (count == buf.length) {
            byte[] bigger = new byte[buf.length * 2];
            for (int i = 0; i < count; i++) bigger[i] = buf[i];
            buf = bigger;
        }
        buf[count++] = (byte) b;
    }

    public byte[] toByteArray() { byte[] out = new byte[count]; for (int i = 0; i < count; i++) out[i] = buf[i]; return out; }
    public int size() { return count; }
    public void reset() { count = 0; }
    @Override public String toString() { return new String(toByteArray(), java.nio.charset.StandardCharsets.UTF_8); }
}
