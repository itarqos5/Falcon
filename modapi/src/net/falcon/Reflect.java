package net.falcon;

// Reflection, as far as the reflective Gson needs it (modapi/src/com/google/gson/Gson.java): answered by the VM, by class name.
public final class Reflect {
    private Reflect() {}

    // "name\u0000descriptor\u0000signature" of each instance field that is not transient: the class's own first, then each superclass's.
    public static native String[] fields(String className);
    public static native Object get(Object o, String field);
    public static native void set(Object o, String field, Object value);
    public static native Object newInstance(String className);
    public static native Object enumValue(String className, String name);
    public static native boolean isEnum(String className);
    public static native boolean isRecord(String className);
    public static native String className(Object o);
    // The generic signature of an object's class ("Lcom/google/gson/reflect/TypeToken<Ljava/util/List<Lcom/x/Foo;>;>;" for an anonymous TypeToken subclass); empty when it has none.
    public static native String classSignature(Object o);
}
