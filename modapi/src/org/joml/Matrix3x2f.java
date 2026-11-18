package org.joml;

// A 2D transform: x' = m00 x + m10 y + m20, y' = m01 x + m11 y + m21.
public class Matrix3x2f {
    public float m00 = 1.0F, m01 = 0.0F, m10 = 0.0F, m11 = 1.0F, m20 = 0.0F, m21 = 0.0F;

    public Matrix3x2f() {}
    public Matrix3x2f(Matrix3x2f o) { set(o); }

    public float m00() { return m00; }
    public float m01() { return m01; }
    public float m10() { return m10; }
    public float m11() { return m11; }
    public float m20() { return m20; }
    public float m21() { return m21; }
    public Matrix3x2f set(Matrix3x2f o) { m00 = o.m00; m01 = o.m01; m10 = o.m10; m11 = o.m11; m20 = o.m20; m21 = o.m21; return this; }
    public Matrix3x2f identity() { m00 = 1.0F; m01 = 0.0F; m10 = 0.0F; m11 = 1.0F; m20 = 0.0F; m21 = 0.0F; return this; }

    public Matrix3x2f translate(float x, float y) {
        m20 = m00 * x + m10 * y + m20;
        m21 = m01 * x + m11 * y + m21;
        return this;
    }

    public Matrix3x2f scale(float x, float y) {
        m00 *= x; m01 *= x; m10 *= y; m11 *= y;
        return this;
    }

    public Matrix3x2f scale(float s) { return scale(s, s); }

    public Matrix3x2f rotate(float angle) {
        float c = (float) Math.cos(angle), s = (float) Math.sin(angle);
        float n00 = m00 * c + m10 * s, n01 = m01 * c + m11 * s, n10 = m00 * -s + m10 * c, n11 = m01 * -s + m11 * c;
        m00 = n00; m01 = n01; m10 = n10; m11 = n11;
        return this;
    }

    public Matrix3x2f rotateAbout(float angle, float x, float y) { translate(x, y); rotate(angle); return translate(-x, -y); }
    public float transformX(float x, float y) { return m00 * x + m10 * y + m20; }
    public float transformY(float x, float y) { return m01 * x + m11 * y + m21; }
}
