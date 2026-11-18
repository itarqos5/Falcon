package net.minecraft.world.phys;

import net.minecraft.core.Vec3i;
import net.minecraft.util.Mth;

public class Vec3 {
    public static final Vec3 ZERO = new Vec3(0.0, 0.0, 0.0);
    public final double x, y, z;

    public Vec3(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }

    public static Vec3 atLowerCornerOf(Vec3i v) { return new Vec3(v.getX(), v.getY(), v.getZ()); }
    public static Vec3 atCenterOf(Vec3i v) { return new Vec3((double) v.getX() + 0.5, (double) v.getY() + 0.5, (double) v.getZ() + 0.5); }
    public static Vec3 atBottomCenterOf(Vec3i v) { return new Vec3((double) v.getX() + 0.5, v.getY(), (double) v.getZ() + 0.5); }
    public static Vec3 directionFromRotation(float xRot, float yRot) {
        float f = Mth.cos(-yRot * 0.017453292F - 3.1415927F), g = Mth.sin(-yRot * 0.017453292F - 3.1415927F), h = -Mth.cos(-xRot * 0.017453292F), i = Mth.sin(-xRot * 0.017453292F);
        return new Vec3(g * h, i, f * h);
    }

    public double x() { return x; }
    public double y() { return y; }
    public double z() { return z; }
    public Vec3 add(double dx, double dy, double dz) { return new Vec3(x + dx, y + dy, z + dz); }
    public Vec3 add(Vec3 v) { return new Vec3(x + v.x, y + v.y, z + v.z); }
    public Vec3 subtract(double dx, double dy, double dz) { return new Vec3(x - dx, y - dy, z - dz); }
    public Vec3 subtract(Vec3 v) { return new Vec3(x - v.x, y - v.y, z - v.z); }
    public Vec3 scale(double f) { return new Vec3(x * f, y * f, z * f); }
    public Vec3 multiply(double fx, double fy, double fz) { return new Vec3(x * fx, y * fy, z * fz); }
    public Vec3 reverse() { return new Vec3(-x, -y, -z); }
    public double dot(Vec3 v) { return x * v.x + y * v.y + z * v.z; }
    public Vec3 cross(Vec3 v) { return new Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }
    public double length() { return Math.sqrt(x * x + y * y + z * z); }
    public double lengthSqr() { return x * x + y * y + z * z; }
    public Vec3 normalize() { double d = length(); return d < 1.0E-5 ? ZERO : new Vec3(x / d, y / d, z / d); }
    public double distanceTo(Vec3 v) { double dx = v.x - x, dy = v.y - y, dz = v.z - z; return Math.sqrt(dx * dx + dy * dy + dz * dz); }
    public double distanceToSqr(Vec3 v) { double dx = v.x - x, dy = v.y - y, dz = v.z - z; return dx * dx + dy * dy + dz * dz; }
    public double distanceToSqr(double px, double py, double pz) { double dx = px - x, dy = py - y, dz = pz - z; return dx * dx + dy * dy + dz * dz; }
    public Vec3 lerp(Vec3 to, double t) { return new Vec3(Mth.lerp(t, x, to.x), Mth.lerp(t, y, to.y), Mth.lerp(t, z, to.z)); }
    @Override public boolean equals(Object o) { return o instanceof Vec3 v && Double.compare(v.x, x) == 0 && Double.compare(v.y, y) == 0 && Double.compare(v.z, z) == 0; }
    @Override public int hashCode() { return Double.hashCode(x) * 31 * 31 + Double.hashCode(y) * 31 + Double.hashCode(z); }
    @Override public String toString() { return "(" + x + ", " + y + ", " + z + ")"; }
}
