package java.io;

public class FileOutputStream extends ByteArrayOutputStream {
    private final String path;
    private final boolean append;

    public FileOutputStream(String path) throws FileNotFoundException { this(path, false); }
    public FileOutputStream(File file) throws FileNotFoundException { this(file.getPath(), false); }
    public FileOutputStream(File file, boolean append) throws FileNotFoundException { this(file.getPath(), append); }
    public FileOutputStream(String path, boolean append) throws FileNotFoundException { this.path = path; this.append = append; if (!net.falcon.Fs.write(path, new byte[0], append)) throw new FileNotFoundException(path); }

    @Override public void flush() { net.falcon.Fs.write(path, toByteArray(), append); reset(); }
    @Override public void close() { flush(); }
}
