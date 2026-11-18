package java.net;

public final class URI implements Comparable<URI> {
    private final String text;

    private URI(String text) { this.text = text; }

    public static URI create(String text) { return new URI(text); }
    public URI(String text, boolean checked) { this.text = text; }
    public String getScheme() { int i = text.indexOf(':'); return i < 0 ? null : text.substring(0, i); }
    public String getHost() { int s = text.indexOf("//"); if (s < 0) return null; int e = text.indexOf('/', s + 2); String h = text.substring(s + 2, e < 0 ? text.length() : e); int c = h.lastIndexOf(':'); return c < 0 ? h : h.substring(0, c); }
    public String getPath() { int s = text.indexOf("//"); int e = s < 0 ? 0 : text.indexOf('/', s + 2); return e < 0 ? "" : text.substring(e); }
    public String toASCIIString() { return text; }
    @Override public int compareTo(URI o) { return text.compareTo(o.text); }
    @Override public boolean equals(Object o) { return o instanceof URI u && u.text.equals(text); }
    @Override public int hashCode() { return text.hashCode(); }
    @Override public String toString() { return text; }
}
