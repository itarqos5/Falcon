package java.io;

import java.util.ArrayList;
import java.util.List;

public class BufferedReader extends Reader {
    private final Reader in;

    public BufferedReader(Reader in) { this.in = in; }
    public BufferedReader(Reader in, int size) { this.in = in; }

    @Override public int read() throws IOException { return in.read(); }

    public String readLine() throws IOException {
        StringBuilder sb = new StringBuilder();
        int c = in.read();
        if (c == -1) return null;
        while (c != -1 && c != '\n') {
            if (c != '\r') sb.append((char) c);
            c = in.read();
        }
        return sb.toString();
    }

    public java.util.stream.Stream<String> lines() {
        List<String> all = new ArrayList<>();
        try {
            String line;
            while ((line = readLine()) != null) all.add(line);
        } catch (IOException e) { throw new UncheckedIOException(e); }
        return all.stream();
    }

    @Override public void close() throws IOException { in.close(); }
}
