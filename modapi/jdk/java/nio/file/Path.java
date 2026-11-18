package java.nio.file;

import java.util.Iterator;

public interface Path extends Comparable<Path>, Iterable<Path> {
    static Path of(String first, String... more) { return Paths.get(first, more); }

    Path resolve(String other);
    Path resolve(Path other);
    Path resolveSibling(String other);
    Path getParent();
    Path getFileName();
    Path getRoot();
    Path toAbsolutePath();
    Path normalize();
    Path relativize(Path other);
    boolean startsWith(String other);
    boolean startsWith(Path other);
    boolean endsWith(String other);
    boolean endsWith(Path other);
    boolean isAbsolute();
    int getNameCount();
    Path getName(int index);
    Path subpath(int from, int to);
    java.io.File toFile();
    java.net.URI toUri();
    @Override Iterator<Path> iterator();
    @Override String toString();
}
