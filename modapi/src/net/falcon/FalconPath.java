package net.falcon;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

// A path as text, with forward slashes: what the mods get from Paths.get and FabricLoader.getConfigDir.
public final class FalconPath implements Path {
    private final String text;

    public FalconPath(String text) {
        String t = text;
        while (t.length() > 1 && t.endsWith("/") && !(t.length() == 3 && t.charAt(1) == ':')) t = t.substring(0, t.length() - 1);
        this.text = t;
    }

    private List<String> names() {
        List<String> out = new ArrayList<>();
        for (String part : text.substring(rootText().length()).split("/", -1)) {
            if (!part.isEmpty()) out.add(part);
        }
        return out;
    }

    private String rootText() { return text.startsWith("/") ? "/" : (text.length() >= 2 && text.charAt(1) == ':' ? text.substring(0, text.length() >= 3 && text.charAt(2) == '/' ? 3 : 2) : ""); }

    @Override public Path resolve(String other) { return resolve(new FalconPath(other)); }
    @Override
    public Path resolve(Path other) {
        String o = other.toString();
        if (o.isEmpty()) return this;
        if (((FalconPath) other).isAbsolute()) return other;
        if (text.isEmpty()) return other;
        return new FalconPath(text.endsWith("/") ? text + o : text + "/" + o);
    }
    @Override public Path resolveSibling(String other) { Path p = getParent(); return p == null ? new FalconPath(other) : p.resolve(other); }

    @Override
    public Path getParent() {
        int i = text.lastIndexOf('/');
        if (i < 0) return null;
        if (i == 0) return text.length() > 1 ? new FalconPath("/") : null;
        return new FalconPath(text.substring(0, i));
    }

    @Override public Path getFileName() { List<String> n = names(); return n.isEmpty() ? null : new FalconPath(n.get(n.size() - 1)); }
    @Override public Path getRoot() { String r = rootText(); return r.isEmpty() ? null : new FalconPath(r); }
    @Override public boolean isAbsolute() { return !rootText().isEmpty(); }
    @Override public Path toAbsolutePath() { return isAbsolute() ? this : new FalconPath(Fs.absolute(text)); }

    @Override
    public Path normalize() {
        List<String> out = new ArrayList<>();
        for (String n : names()) {
            if (n.equals(".")) continue;
            if (n.equals("..") && !out.isEmpty() && !out.get(out.size() - 1).equals("..") && !(out.size() == 1 && out.get(0).endsWith(":"))) { out.remove(out.size() - 1); continue; }
            out.add(n);
        }
        String root = rootText();
        StringBuilder sb = new StringBuilder(root);
        for (int i = 0; i < out.size(); i++) {
            if (i > 0) sb.append('/');
            sb.append(out.get(i));
        }
        if (root.length() == 2 && out.size() > 0 && out.get(0).equals(root)) return new FalconPath(sb.toString());
        return new FalconPath(sb.toString());
    }

    @Override
    public Path relativize(Path other) {
        List<String> a = names(), b = ((FalconPath) other).names();
        int i = 0;
        while (i < a.size() && i < b.size() && a.get(i).equals(b.get(i))) i++;
        StringBuilder sb = new StringBuilder();
        for (int k = i; k < a.size(); k++) { if (sb.length() > 0) sb.append('/'); sb.append(".."); }
        for (int k = i; k < b.size(); k++) { if (sb.length() > 0) sb.append('/'); sb.append(b.get(k)); }
        return new FalconPath(sb.toString());
    }

    @Override public boolean startsWith(String other) { return startsWith(new FalconPath(other)); }
    @Override
    public boolean startsWith(Path other) {
        FalconPath o = (FalconPath) other;
        if (isAbsolute() != o.isAbsolute()) return false;
        List<String> a = names(), b = o.names();
        if (b.size() > a.size()) return false;
        for (int i = 0; i < b.size(); i++) { if (!a.get(i).equals(b.get(i))) return false; }
        return true;
    }
    @Override public boolean endsWith(String other) { return endsWith(new FalconPath(other)); }
    @Override
    public boolean endsWith(Path other) {
        List<String> a = names(), b = ((FalconPath) other).names();
        if (b.size() > a.size()) return false;
        for (int i = 0; i < b.size(); i++) { if (!a.get(a.size() - b.size() + i).equals(b.get(i))) return false; }
        return true;
    }

    @Override public int getNameCount() { return names().size(); }
    @Override public Path getName(int index) { return new FalconPath(names().get(index)); }
    @Override public Path subpath(int from, int to) { List<String> n = names(); StringBuilder sb = new StringBuilder(); for (int i = from; i < to; i++) { if (sb.length() > 0) sb.append('/'); sb.append(n.get(i)); } return new FalconPath(sb.toString()); }
    @Override public java.io.File toFile() { return new java.io.File(text); }
    @Override public java.net.URI toUri() { return java.net.URI.create("file:///" + (text.startsWith("/") ? text.substring(1) : text)); }

    @Override
    public Iterator<Path> iterator() {
        List<Path> out = new ArrayList<>();
        for (String n : names()) out.add(new FalconPath(n));
        return out.iterator();
    }

    @Override public int compareTo(Path other) { return text.compareTo(other.toString()); }
    @Override public boolean equals(Object o) { return o instanceof FalconPath p && p.text.equals(text); }
    @Override public int hashCode() { return text.hashCode(); }
    @Override public String toString() { return text; }
}
