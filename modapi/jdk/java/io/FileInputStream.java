package java.io;

public class FileInputStream extends ByteArrayInputStream {
    public FileInputStream(String path) throws FileNotFoundException { super(open(path)); }
    public FileInputStream(File file) throws FileNotFoundException { this(file.getPath()); }

    private static byte[] open(String path) throws FileNotFoundException {
        byte[] data = net.falcon.Fs.read(path);
        if (data == null) throw new FileNotFoundException(path + " (No such file or directory)");
        return data;
    }
}
