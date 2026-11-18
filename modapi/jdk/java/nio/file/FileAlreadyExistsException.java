package java.nio.file;

public class FileAlreadyExistsException extends FileSystemException {
    public FileAlreadyExistsException(String file) { super(file); }
}
