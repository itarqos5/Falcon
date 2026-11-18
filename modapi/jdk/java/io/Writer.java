package java.io;

public abstract class Writer implements Appendable, Closeable, Flushable {
    public abstract void write(int c) throws IOException;

    public void write(String s) throws IOException { for (int i = 0; i < s.length(); i++) write(s.charAt(i)); }
    public void write(char[] cbuf) throws IOException { for (char c : cbuf) write(c); }
    public void write(char[] cbuf, int off, int len) throws IOException { for (int i = 0; i < len; i++) write(cbuf[off + i]); }
    public void write(String s, int off, int len) throws IOException { write(s.substring(off, off + len)); }
    @Override public Writer append(CharSequence s) throws IOException { write(String.valueOf(s)); return this; }
    @Override public Writer append(char c) throws IOException { write(c); return this; }
    @Override public void flush() throws IOException {}
    @Override public void close() throws IOException {}
}
