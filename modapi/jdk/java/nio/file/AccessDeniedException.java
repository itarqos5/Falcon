package java.nio.file;

public class AccessDeniedException extends FileSystemException {
    public AccessDeniedException(String file) { super(file); }
}
