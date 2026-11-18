package java.io;

public class BufferedWriter extends Writer {
    private final Writer out;

    public BufferedWriter(Writer out) { this.out = out; }
    public BufferedWriter(Writer out, int size) { this.out = out; }

    @Override public void write(int c) throws IOException { out.write(c); }
    @Override public void write(String s) throws IOException { out.write(s); }
    public void newLine() throws IOException { out.write("\n"); }
    @Override public void flush() throws IOException { out.flush(); }
    @Override public void close() throws IOException { out.close(); }
}
