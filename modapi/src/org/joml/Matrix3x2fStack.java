package org.joml;

public class Matrix3x2fStack extends Matrix3x2f {
    private final Matrix3x2f[] stack;
    private int depth = 0;

    public Matrix3x2fStack(int size) { stack = new Matrix3x2f[size]; for (int i = 0; i < size; i++) stack[i] = new Matrix3x2f(); }

    public Matrix3x2fStack pushMatrix() { stack[depth++].set(this); return this; }
    public Matrix3x2fStack popMatrix() { if (depth > 0) set(stack[--depth]); return this; }
    public Matrix3x2fStack clear() { depth = 0; identity(); return this; }
}
