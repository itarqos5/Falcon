package java.lang.reflect;

public class InvocationTargetException extends ReflectiveOperationException {
    private final Throwable target;

    public InvocationTargetException(Throwable target) { super(target); this.target = target; }
    public Throwable getTargetException() { return target; }
}
