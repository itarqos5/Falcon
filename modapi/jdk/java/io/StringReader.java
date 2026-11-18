package java.io;

public class StringReader extends Reader {
    private final String text;
    private int pos;

    public StringReader(String text) { this.text = text; }
    @Override public int read() { return pos < text.length() ? text.charAt(pos++) : -1; }
}
