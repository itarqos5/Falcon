package org.joml;

public class Quaternionf {
    public float x, y, z, w = 1.0F;

    public Quaternionf() {}
    public Quaternionf(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }

    public Quaternionf rotationXYZ(float ax, float ay, float az) {
        Quaternionf qx = axis(ax, 1, 0, 0), qy = axis(ay, 0, 1, 0), qz = axis(az, 0, 0, 1);
        return set(qx.mul(qy).mul(qz));
    }

    public Quaternionf rotationY(float a) { return set(axis(a, 0, 1, 0)); }
    public Quaternionf rotationX(float a) { return set(axis(a, 1, 0, 0)); }
    public Quaternionf rotationZ(float a) { return set(axis(a, 0, 0, 1)); }
    public Quaternionf set(Quaternionf o) { x = o.x; y = o.y; z = o.z; w = o.w; return this; }

    private static Quaternionf axis(float a, float ax, float ay, float az) { float s = (float) Math.sin(a * 0.5F); return new Quaternionf(ax * s, ay * s, az * s, (float) Math.cos(a * 0.5F)); }

    public Quaternionf mul(Quaternionf q) {
        return new Quaternionf(w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x, w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z);
    }
}
