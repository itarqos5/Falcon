package java.io;

public class FileReader extends InputStreamReader {
    public FileReader(String path) throws IOException { super(new FileInputStream(path)); }
    public FileReader(File file) throws IOException { super(new FileInputStream(file)); }
}
