package java.nio.file;

public class FileSystemException extends java.io.IOException {
    private final String file;

    public FileSystemException(String file) { super(file); this.file = file; }
    public FileSystemException(String file, String other, String reason) { super(file + (reason == null ? "" : ": " + reason)); this.file = file; }
    public String getFile() { return file; }
}
