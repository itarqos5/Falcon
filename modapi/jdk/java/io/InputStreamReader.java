package java.io;

import java.nio.charset.Charset;

public class InputStreamReader extends Reader {
    private final String text;
    private int pos;

    public InputStreamReader(InputStream in) throws IOException { this.text = new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8); }
    public InputStreamReader(InputStream in, Charset charset) throws IOException { this(in); }
    @Override public int read() { return pos < text.length() ? text.charAt(pos++) : -1; }
}
