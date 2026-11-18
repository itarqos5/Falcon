package java.io;

import java.nio.charset.Charset;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;
    private final StringBuilder pending = new StringBuilder();

    public OutputStreamWriter(OutputStream out) { this.out = out; }
    public OutputStreamWriter(OutputStream out, Charset charset) { this.out = out; }

    @Override public void write(int c) { pending.append((char) c); }
    @Override public void write(String s) { pending.append(s); }
    @Override public void flush() throws IOException { if (pending.length() > 0) { out.write(pending.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)); pending.setLength(0); } out.flush(); }
    @Override public void close() throws IOException { flush(); out.close(); }
}
