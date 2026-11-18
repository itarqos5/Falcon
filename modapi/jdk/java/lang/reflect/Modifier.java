package java.lang.reflect;

public final class Modifier {
    private Modifier() {}

    public static final int PUBLIC = 1, PRIVATE = 2, PROTECTED = 4, STATIC = 8, FINAL = 16, TRANSIENT = 128, ABSTRACT = 1024;

    public static boolean isPublic(int m) { return (m & PUBLIC) != 0; }
    public static boolean isPrivate(int m) { return (m & PRIVATE) != 0; }
    public static boolean isStatic(int m) { return (m & STATIC) != 0; }
    public static boolean isFinal(int m) { return (m & FINAL) != 0; }
    public static boolean isAbstract(int m) { return (m & ABSTRACT) != 0; }
    public static boolean isTransient(int m) { return (m & TRANSIENT) != 0; }
}
