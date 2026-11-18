package org.joml;

public class Matrix4f {
    public float m00 = 1, m01, m02, m03, m10, m11 = 1, m12, m13, m20, m21, m22 = 1, m23, m30, m31, m32, m33 = 1;

    public Matrix4f() {}
    public Matrix4f(Matrix4f o) { set(o); }

    public Matrix4f set(Matrix4f o) {
        m00 = o.m00; m01 = o.m01; m02 = o.m02; m03 = o.m03; m10 = o.m10; m11 = o.m11; m12 = o.m12; m13 = o.m13;
        m20 = o.m20; m21 = o.m21; m22 = o.m22; m23 = o.m23; m30 = o.m30; m31 = o.m31; m32 = o.m32; m33 = o.m33;
        return this;
    }

    public Matrix4f identity() { return set(new Matrix4f()); }

    // this = this * (a translation)
    public Matrix4f translate(float x, float y, float z) {
        m30 = m00 * x + m10 * y + m20 * z + m30;
        m31 = m01 * x + m11 * y + m21 * z + m31;
        m32 = m02 * x + m12 * y + m22 * z + m32;
        m33 = m03 * x + m13 * y + m23 * z + m33;
        return this;
    }

    public Matrix4f scale(float x, float y, float z) {
        m00 *= x; m01 *= x; m02 *= x; m03 *= x;
        m10 *= y; m11 *= y; m12 *= y; m13 *= y;
        m20 *= z; m21 *= z; m22 *= z; m23 *= z;
        return this;
    }

    public Matrix4f mul(Matrix4f r) {
        float n00 = m00 * r.m00 + m10 * r.m01 + m20 * r.m02 + m30 * r.m03, n01 = m01 * r.m00 + m11 * r.m01 + m21 * r.m02 + m31 * r.m03, n02 = m02 * r.m00 + m12 * r.m01 + m22 * r.m02 + m32 * r.m03, n03 = m03 * r.m00 + m13 * r.m01 + m23 * r.m02 + m33 * r.m03;
        float n10 = m00 * r.m10 + m10 * r.m11 + m20 * r.m12 + m30 * r.m13, n11 = m01 * r.m10 + m11 * r.m11 + m21 * r.m12 + m31 * r.m13, n12 = m02 * r.m10 + m12 * r.m11 + m22 * r.m12 + m32 * r.m13, n13 = m03 * r.m10 + m13 * r.m11 + m23 * r.m12 + m33 * r.m13;
        float n20 = m00 * r.m20 + m10 * r.m21 + m20 * r.m22 + m30 * r.m23, n21 = m01 * r.m20 + m11 * r.m21 + m21 * r.m22 + m31 * r.m23, n22 = m02 * r.m20 + m12 * r.m21 + m22 * r.m22 + m32 * r.m23, n23 = m03 * r.m20 + m13 * r.m21 + m23 * r.m22 + m33 * r.m23;
        float n30 = m00 * r.m30 + m10 * r.m31 + m20 * r.m32 + m30 * r.m33, n31 = m01 * r.m30 + m11 * r.m31 + m21 * r.m32 + m31 * r.m33, n32 = m02 * r.m30 + m12 * r.m31 + m22 * r.m32 + m32 * r.m33, n33 = m03 * r.m30 + m13 * r.m31 + m23 * r.m32 + m33 * r.m33;
        m00 = n00; m01 = n01; m02 = n02; m03 = n03; m10 = n10; m11 = n11; m12 = n12; m13 = n13; m20 = n20; m21 = n21; m22 = n22; m23 = n23; m30 = n30; m31 = n31; m32 = n32; m33 = n33;
        return this;
    }

    public Matrix4f rotate(Quaternionf q) {
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Matrix4f r = new Matrix4f();
        r.m00 = 1 - 2 * (yy + zz); r.m01 = 2 * (xy + wz); r.m02 = 2 * (xz - wy);
        r.m10 = 2 * (xy - wz); r.m11 = 1 - 2 * (xx + zz); r.m12 = 2 * (yz + wx);
        r.m20 = 2 * (xz + wy); r.m21 = 2 * (yz - wx); r.m22 = 1 - 2 * (xx + yy);
        return mul(r);
    }

    public Vector3f transformPosition(Vector3f v) { return transformPosition(v.x, v.y, v.z, v); }
    public Vector3f transformPosition(float x, float y, float z, Vector3f dest) {
        dest.x = m00 * x + m10 * y + m20 * z + m30;
        dest.y = m01 * x + m11 * y + m21 * z + m31;
        dest.z = m02 * x + m12 * y + m22 * z + m32;
        return dest;
    }
}
