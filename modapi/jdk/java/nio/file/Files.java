package java.nio.file;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.attribute.FileTime;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;
import net.falcon.Fs;

public final class Files {
    private Files() {}

    private static String s(Path p) { return p.toString(); }

    public static boolean exists(Path p, LinkOption... options) { return Fs.exists(s(p)); }
    public static boolean notExists(Path p, LinkOption... options) { return !Fs.exists(s(p)); }
    public static boolean isDirectory(Path p, LinkOption... options) { return Fs.isDirectory(s(p)); }
    public static boolean isRegularFile(Path p, LinkOption... options) { return Fs.isRegularFile(s(p)); }
    public static boolean isReadable(Path p) { return Fs.exists(s(p)); }
    public static boolean isWritable(Path p) { return true; }
    public static long size(Path p) throws IOException { if (!Fs.exists(s(p))) throw new NoSuchFileException(s(p)); return Fs.size(s(p)); }
    public static FileTime getLastModifiedTime(Path p, LinkOption... options) throws IOException { if (!Fs.exists(s(p))) throw new NoSuchFileException(s(p)); return FileTime.fromMillis(Fs.lastModified(s(p))); }

    public static Path createDirectories(Path p, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException { if (!Fs.isDirectory(s(p)) && !Fs.mkdirs(s(p))) throw new AccessDeniedException(s(p)); return p; }
    public static Path createDirectory(Path p, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException { if (Fs.exists(s(p))) throw new FileAlreadyExistsException(s(p)); return createDirectories(p); }
    public static Path createFile(Path p, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException { if (Fs.exists(s(p))) throw new FileAlreadyExistsException(s(p)); write(p, new byte[0]); return p; }

    public static byte[] readAllBytes(Path p) throws IOException { byte[] d = Fs.read(s(p)); if (d == null) throw new NoSuchFileException(s(p)); return d; }
    public static String readString(Path p) throws IOException { return new String(readAllBytes(p), StandardCharsets.UTF_8); }
    public static String readString(Path p, Charset cs) throws IOException { return readString(p); }
    public static List<String> readAllLines(Path p) throws IOException {
        String text = readString(p);
        List<String> lines = new ArrayList<>();
        if (text.isEmpty()) return lines;
        for (String line : text.split("\n", -1)) lines.add(line.endsWith("\r") ? line.substring(0, line.length() - 1) : line);
        if (text.endsWith("\n")) lines.remove(lines.size() - 1);
        return lines;
    }
    public static List<String> readAllLines(Path p, Charset cs) throws IOException { return readAllLines(p); }
    public static Stream<String> lines(Path p) throws IOException { return readAllLines(p).stream(); }

    private static boolean appends(OpenOption[] options) { for (OpenOption o : options) { if (o == StandardOpenOption.APPEND) return true; } return false; }
    private static boolean createsNew(OpenOption[] options) { for (OpenOption o : options) { if (o == StandardOpenOption.CREATE_NEW) return true; } return false; }

    public static Path write(Path p, byte[] data, OpenOption... options) throws IOException {
        if (createsNew(options) && Fs.exists(s(p))) throw new FileAlreadyExistsException(s(p));
        if (!Fs.write(s(p), data, appends(options))) throw new AccessDeniedException(s(p));
        return p;
    }
    public static Path writeString(Path p, CharSequence text, OpenOption... options) throws IOException { return write(p, String.valueOf(text).getBytes(StandardCharsets.UTF_8), options); }
    public static Path writeString(Path p, CharSequence text, Charset cs, OpenOption... options) throws IOException { return writeString(p, text, options); }
    public static Path write(Path p, Iterable<? extends CharSequence> lines, OpenOption... options) throws IOException {
        StringBuilder sb = new StringBuilder();
        for (CharSequence line : lines) sb.append(line).append('\n');
        return writeString(p, sb, options);
    }
    public static Path write(Path p, Iterable<? extends CharSequence> lines, Charset cs, OpenOption... options) throws IOException { return write(p, lines, options); }

    public static InputStream newInputStream(Path p, OpenOption... options) throws IOException { return new ByteArrayInputStream(readAllBytes(p)); }
    public static BufferedReader newBufferedReader(Path p) throws IOException { return new BufferedReader(new InputStreamReader(newInputStream(p))); }
    public static BufferedReader newBufferedReader(Path p, Charset cs) throws IOException { return newBufferedReader(p); }

    public static OutputStream newOutputStream(Path p, OpenOption... options) throws IOException {
        final String path = s(p);
        final boolean append = appends(options);
        if (createsNew(options) && Fs.exists(path)) throw new FileAlreadyExistsException(path);
        if (!Fs.write(path, new byte[0], append)) throw new AccessDeniedException(path);
        return new ByteArrayOutputStream() {
            @Override public void flush() { Fs.write(path, toByteArray(), true); reset(); }
            @Override public void close() { flush(); }
        };
    }
    public static BufferedWriter newBufferedWriter(Path p, OpenOption... options) throws IOException { return new BufferedWriter(new OutputStreamWriter(newOutputStream(p, options))); }
    public static BufferedWriter newBufferedWriter(Path p, Charset cs, OpenOption... options) throws IOException { return newBufferedWriter(p, options); }

    public static void delete(Path p) throws IOException { if (!Fs.exists(s(p))) throw new NoSuchFileException(s(p)); if (!Fs.delete(s(p))) throw new DirectoryNotEmptyException(s(p)); }
    public static boolean deleteIfExists(Path p) throws IOException { if (!Fs.exists(s(p))) return false; delete(p); return true; }

    private static boolean replaces(CopyOption[] options) { for (CopyOption o : options) { if (o == StandardCopyOption.REPLACE_EXISTING) return true; } return false; }
    public static Path move(Path from, Path to, CopyOption... options) throws IOException {
        if (!Fs.exists(s(from))) throw new NoSuchFileException(s(from));
        if (Fs.exists(s(to)) && !replaces(options)) throw new FileAlreadyExistsException(s(to));
        if (!Fs.move(s(from), s(to), true)) throw new AccessDeniedException(s(to));
        return to;
    }
    public static Path copy(Path from, Path to, CopyOption... options) throws IOException {
        if (!Fs.exists(s(from))) throw new NoSuchFileException(s(from));
        if (Fs.exists(s(to)) && !replaces(options)) throw new FileAlreadyExistsException(s(to));
        if (!Fs.copy(s(from), s(to), true)) throw new AccessDeniedException(s(to));
        return to;
    }

    public static Stream<Path> list(Path dir) throws IOException {
        String[] names = Fs.list(s(dir));
        if (names == null) throw new NoSuchFileException(s(dir));
        List<Path> out = new ArrayList<>();
        for (String n : names) out.add(dir.resolve(n));
        return out.stream();
    }

    public static Stream<Path> walk(Path start, FileVisitOption... options) throws IOException {
        List<Path> out = new ArrayList<>();
        walkInto(start, out);
        return out.stream();
    }
    public static Stream<Path> walk(Path start, int depth, FileVisitOption... options) throws IOException { return walk(start); }

    private static void walkInto(Path p, List<Path> out) {
        if (!Fs.exists(s(p))) return;
        out.add(p);
        String[] names = Fs.list(s(p));
        if (names == null) return;
        for (String n : names) walkInto(p.resolve(n), out);
    }

    public static Path createTempFile(String prefix, String suffix, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException {
        Path p = Paths.get(net.falcon.Host.gameDir(), "temp", (prefix == null ? "" : prefix) + System.nanoTime() + (suffix == null ? ".tmp" : suffix));
        createDirectories(p.getParent());
        write(p, new byte[0]);
        return p;
    }
    public static Path createTempFile(Path dir, String prefix, String suffix, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException {
        Path p = dir.resolve((prefix == null ? "" : prefix) + System.nanoTime() + (suffix == null ? ".tmp" : suffix));
        write(p, new byte[0]);
        return p;
    }
    public static Path createTempDirectory(String prefix, java.nio.file.attribute.FileAttribute<?>... attrs) throws IOException {
        Path p = Paths.get(net.falcon.Host.gameDir(), "temp", (prefix == null ? "" : prefix) + System.nanoTime());
        return createDirectories(p);
    }
}
