package java.io;

public class PrintWriter extends Writer {
    private final Writer out;

    public PrintWriter(Writer out) { this.out = out; }
    public PrintWriter(Writer out, boolean autoFlush) { this.out = out; }
    public PrintWriter(OutputStream out) { this.out = new OutputStreamWriter(out); }

    @Override public void write(int c) { try { out.write(c); } catch (IOException e) { } }
    @Override public void write(String s) { try { out.write(s); } catch (IOException e) { } }
    public void print(String s) { write(String.valueOf(s)); }
    public void print(Object o) { write(String.valueOf(o)); }
    public void println(String s) { write(String.valueOf(s)); write("\n"); }
    public void println(Object o) { write(String.valueOf(o)); write("\n"); }
    public void println() { write("\n"); }
    public PrintWriter printf(String format, Object... args) { write(String.format(format, args)); return this; }
    @Override public void flush() { try { out.flush(); } catch (IOException e) { } }
    @Override public void close() { try { out.close(); } catch (IOException e) { } }
}
