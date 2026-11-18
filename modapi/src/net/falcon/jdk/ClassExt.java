package net.falcon.jdk;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

// What Class does in Java here (the VM's own Class object keeps the name): methods, fields and constructors by name, as reflection finds them.
public final class ClassExt {
    private ClassExt() {}

    private static String slashes(Class<?> c) { return c.getName().replace('.', '/'); }

    private static String[] names(Class<?>[] types) {
        String[] out = new String[types == null ? 0 : types.length];
        for (int i = 0; i < out.length; i++) out[i] = types[i].getName();
        return out;
    }

    public static Method getMethod(Class<?> c, String name, Class<?>[] types) throws NoSuchMethodException {
        String info = net.falcon.Reflect.methodInfo(slashes(c), name, names(types));
        if (info == null) throw new NoSuchMethodException(c.getName() + "." + name);
        return method(c, name, info, types);
    }

    public static Method getDeclaredMethod(Class<?> c, String name, Class<?>[] types) throws NoSuchMethodException { return getMethod(c, name, types); }

    private static Method method(Class<?> c, String name, String info, Class<?>[] types) {
        String[] parts = info.split("\u0000", -1);  // owner, descriptor, modifiers, return type
        Class<?> ret = null;
        try { ret = Class.forName(parts[3]); } catch (ClassNotFoundException e) { }
        return new Method(parts[0], name, parts[1], Integer.parseInt(parts[2]), ret, types == null ? new Class<?>[0] : types);
    }

    public static Field getField(Class<?> c, String name) throws NoSuchFieldException {
        String info = net.falcon.Reflect.fieldInfo(slashes(c), name);
        if (info == null) throw new NoSuchFieldException(name);
        String[] parts = info.split("\u0000", -1);  // owner, descriptor, modifiers, type
        Class<?> type = null;
        try { type = Class.forName(parts[3]); } catch (ClassNotFoundException e) { }
        return new Field(parts[0], name, parts[1], Integer.parseInt(parts[2]), type);
    }

    public static Field getDeclaredField(Class<?> c, String name) throws NoSuchFieldException { return getField(c, name); }

    @SuppressWarnings("unchecked")
    public static <T> Constructor<T> getConstructor(Class<T> c, Class<?>[] types) throws NoSuchMethodException {
        String desc = net.falcon.Reflect.constructorInfo(slashes(c), names(types));
        if (desc == null) throw new NoSuchMethodException(c.getName() + ".<init>");
        return new Constructor<>(slashes(c), desc, types == null ? new Class<?>[0] : types);
    }

    public static <T> Constructor<T> getDeclaredConstructor(Class<T> c, Class<?>[] types) throws NoSuchMethodException { return getConstructor(c, types); }

    @SuppressWarnings("unchecked")
    public static <T> T cast(Class<T> c, Object o) {
        if (o != null && !c.isInstance(o)) throw new ClassCastException("Cannot cast " + o.getClass().getName() + " to " + c.getName());
        return (T) o;
    }

    public static boolean isRecord(Class<?> c) { return net.falcon.Reflect.isRecord(slashes(c)); }
    public static boolean isEnum(Class<?> c) { return net.falcon.Reflect.isEnum(slashes(c)); }
    public static boolean isInterface(Class<?> c) { return net.falcon.Reflect.isInterface(slashes(c)); }

    public static InputStream getResourceAsStream(Class<?> c, String path) {
        String p = path.startsWith("/") ? path.substring(1) : path;
        byte[] data = net.falcon.Reflect.resource(p);
        return data == null ? null : new ByteArrayInputStream(data);
    }

    public static ClassLoader getClassLoader(Class<?> c) { return null; }
    public static Class<?>[] getDeclaredClasses(Class<?> c) { return new Class<?>[0]; }
    public static Class<?>[] getInterfaces(Class<?> c) { return new Class<?>[0]; }
    public static boolean isAssignableFrom(Class<?> c, Class<?> other) { return net.falcon.Reflect.isAssignable(slashes(c), slashes(other)); }
}
