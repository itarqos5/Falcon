package java.lang.reflect;

public class AccessibleObject {
    private boolean accessible;

    public void setAccessible(boolean flag) { accessible = flag; }
    public boolean trySetAccessible() { accessible = true; return true; }
    public boolean isAccessible() { return accessible; }
    public boolean canAccess(Object o) { return true; }
}
