package java.lang.reflect;

public final class Method extends AccessibleObject {
    private final String owner, name, descriptor;
    private final int modifiers;
    private final Class<?> returnType;
    private final Class<?>[] parameterTypes;

    public Method(String owner, String name, String descriptor, int modifiers, Class<?> returnType, Class<?>[] parameterTypes) {
        this.owner = owner; this.name = name; this.descriptor = descriptor; this.modifiers = modifiers; this.returnType = returnType; this.parameterTypes = parameterTypes;
    }

    public String getName() { return name; }
    public int getModifiers() { return modifiers; }
    public Class<?> getReturnType() { return returnType; }
    public Class<?>[] getParameterTypes() { return parameterTypes; }
    public int getParameterCount() { return parameterTypes.length; }
    public Class<?> getDeclaringClass() { try { return Class.forName(owner.replace('/', '.')); } catch (ClassNotFoundException e) { return null; } }

    public Object invoke(Object target, Object... args) throws IllegalAccessException, InvocationTargetException {
        try {
            return net.falcon.Reflect.invoke(owner, name, descriptor, target, args == null ? new Object[0] : args);
        } catch (Throwable t) {
            throw new InvocationTargetException(t);
        }
    }

    @Override public String toString() { return owner.replace('/', '.') + "." + name; }
}
