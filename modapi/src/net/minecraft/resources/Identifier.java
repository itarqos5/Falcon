package net.minecraft.resources;

public final class Identifier implements Comparable<Identifier> {
    private final String namespace, path;

    private Identifier(String namespace, String path) {
        this.namespace = namespace;
        this.path = path;
    }

    public static Identifier fromNamespaceAndPath(String namespace, String path) { return new Identifier(namespace, path); }
    public static Identifier withDefaultNamespace(String path) { return new Identifier("minecraft", path); }
    public static Identifier of(String namespace, String path) { return new Identifier(namespace, path); }

    public static Identifier parse(String text) {
        Identifier id = tryParse(text);
        if (id == null) throw new IllegalArgumentException("Not a valid identifier: " + text);
        return id;
    }

    public static Identifier tryParse(String text) {
        if (text == null || text.isEmpty()) return null;
        int colon = text.indexOf(':');
        String ns = colon < 0 ? "minecraft" : (colon == 0 ? "minecraft" : text.substring(0, colon));
        String path = colon < 0 ? text : text.substring(colon + 1);
        if (path.isEmpty()) return null;
        for (int i = 0; i < ns.length(); i++) {
            char c = ns.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return null;
        }
        for (int i = 0; i < path.length(); i++) {
            char c = path.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return null;
        }
        return new Identifier(ns, path);
    }

    public String getNamespace() { return namespace; }
    public String getPath() { return path; }
    public Identifier withPath(String path) { return new Identifier(namespace, path); }
    public Identifier withPrefix(String prefix) { return new Identifier(namespace, prefix + path); }
    public Identifier withSuffix(String suffix) { return new Identifier(namespace, path + suffix); }
    @Override public String toString() { return namespace + ":" + path; }
    public String toShortString() { return namespace.equals("minecraft") ? path : toString(); }
    @Override public boolean equals(Object o) { return o instanceof Identifier other && other.namespace.equals(namespace) && other.path.equals(path); }
    @Override public int hashCode() { return 31 * namespace.hashCode() + path.hashCode(); }
    @Override public int compareTo(Identifier other) { int c = path.compareTo(other.path); return c != 0 ? c : namespace.compareTo(other.namespace); }
}
