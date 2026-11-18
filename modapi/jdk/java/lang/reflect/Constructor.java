package java.lang.reflect;

public final class Constructor<T> extends AccessibleObject {
    private final String owner, descriptor;
    private final Class<?>[] parameterTypes;

    public Constructor(String owner, String descriptor, Class<?>[] parameterTypes) { this.owner = owner; this.descriptor = descriptor; this.parameterTypes = parameterTypes; }

    public Class<?>[] getParameterTypes() { return parameterTypes; }
    public int getParameterCount() { return parameterTypes.length; }

    @SuppressWarnings("unchecked")
    public T newInstance(Object... args) throws InstantiationException, IllegalAccessException, InvocationTargetException {
        try {
            return (T) net.falcon.Reflect.construct(owner, descriptor, args == null ? new Object[0] : args);
        } catch (Throwable t) {
            throw new InvocationTargetException(t);
        }
    }
}
