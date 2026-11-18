package java.lang.reflect;

public final class Field extends AccessibleObject {
    private final String owner, name, descriptor;
    private final int modifiers;
    private final Class<?> type;

    public Field(String owner, String name, String descriptor, int modifiers, Class<?> type) { this.owner = owner; this.name = name; this.descriptor = descriptor; this.modifiers = modifiers; this.type = type; }

    public String getName() { return name; }
    public int getModifiers() { return modifiers; }
    public Class<?> getType() { return type; }
    public Class<?> getDeclaringClass() { try { return Class.forName(owner.replace('/', '.')); } catch (ClassNotFoundException e) { return null; } }

    public Object get(Object target) throws IllegalAccessException { return net.falcon.Reflect.getAny(owner, name, target); }
    public int getInt(Object target) throws IllegalAccessException { return ((Number) get(target)).intValue(); }
    public long getLong(Object target) throws IllegalAccessException { return ((Number) get(target)).longValue(); }
    public float getFloat(Object target) throws IllegalAccessException { return ((Number) get(target)).floatValue(); }
    public double getDouble(Object target) throws IllegalAccessException { return ((Number) get(target)).doubleValue(); }
    public boolean getBoolean(Object target) throws IllegalAccessException { return (Boolean) get(target); }
    public void set(Object target, Object value) throws IllegalAccessException { net.falcon.Reflect.setAny(owner, name, target, value); }
    public void setInt(Object target, int value) throws IllegalAccessException { set(target, Integer.valueOf(value)); }
    public void setBoolean(Object target, boolean value) throws IllegalAccessException { set(target, Boolean.valueOf(value)); }

    @Override public String toString() { return owner.replace('/', '.') + "." + name; }
}
