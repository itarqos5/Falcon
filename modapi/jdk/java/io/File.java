package java.io;

import java.nio.file.Path;
import java.nio.file.Paths;

public class File implements Comparable<File> {
    public static final String separator = "/";
    private final String path;

    public File(String path) { this.path = path.replace('\\', '/'); }
    public File(File parent, String child) { this.path = (parent.path.endsWith("/") ? parent.path : parent.path + "/") + child; }
    public File(String parent, String child) { this(new File(parent), child); }

    public String getPath() { return path; }
    public String getName() { int i = path.lastIndexOf('/'); return i < 0 ? path : path.substring(i + 1); }
    public String getParent() { int i = path.lastIndexOf('/'); return i <= 0 ? null : path.substring(0, i); }
    public File getParentFile() { String p = getParent(); return p == null ? null : new File(p); }
    public String getAbsolutePath() { return net.falcon.Fs.absolute(path); }
    public File getAbsoluteFile() { return new File(getAbsolutePath()); }
    public boolean exists() { return net.falcon.Fs.exists(path); }
    public boolean isDirectory() { return net.falcon.Fs.isDirectory(path); }
    public boolean isFile() { return net.falcon.Fs.isRegularFile(path); }
    public boolean mkdirs() { return net.falcon.Fs.mkdirs(path); }
    public boolean mkdir() { return net.falcon.Fs.mkdirs(path); }
    public boolean delete() { return net.falcon.Fs.delete(path); }
    public long length() { return net.falcon.Fs.size(path); }
    public long lastModified() { return net.falcon.Fs.lastModified(path); }
    public Path toPath() { return Paths.get(path); }
    public String[] list() { return net.falcon.Fs.list(path); }
    public File[] listFiles() { String[] names = list(); if (names == null) return null; File[] files = new File[names.length]; for (int i = 0; i < names.length; i++) files[i] = new File(this, names[i]); return files; }
    @Override public int compareTo(File o) { return path.compareTo(o.path); }
    @Override public boolean equals(Object o) { return o instanceof File f && f.path.equals(path); }
    @Override public int hashCode() { return path.hashCode(); }
    @Override public String toString() { return path; }
}
