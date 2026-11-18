package org.joml;

public class Matrix3f {
    public float m00 = 1, m01, m02, m10, m11 = 1, m12, m20, m21, m22 = 1;

    public Vector3f transform(Vector3f v) { return v; }
}
