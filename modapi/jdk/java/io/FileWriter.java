package java.io;

public class FileWriter extends OutputStreamWriter {
    public FileWriter(String path) throws IOException { super(new FileOutputStream(path)); }
    public FileWriter(File file) throws IOException { super(new FileOutputStream(file)); }
    public FileWriter(String path, boolean append) throws IOException { super(new FileOutputStream(path, append)); }
}
