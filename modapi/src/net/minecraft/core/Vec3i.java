package net.minecraft.core;

public class Vec3i implements Comparable<Vec3i> {
    public static final Vec3i ZERO = new Vec3i(0, 0, 0);
    private int x, y, z;

    public Vec3i(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }

    public int getX() { return x; }
    public int getY() { return y; }
    public int getZ() { return z; }
    protected Vec3i setX(int v) { x = v; return this; }
    protected Vec3i setY(int v) { y = v; return this; }
    protected Vec3i setZ(int v) { z = v; return this; }
    public double distSqr(Vec3i o) { return distToLowCornerSqr(o.getX(), o.getY(), o.getZ()); }
    public double distToLowCornerSqr(double px, double py, double pz) { double dx = (double) getX() - px, dy = (double) getY() - py, dz = (double) getZ() - pz; return dx * dx + dy * dy + dz * dz; }
    public double distToCenterSqr(double px, double py, double pz) { double dx = (double) getX() + 0.5 - px, dy = (double) getY() + 0.5 - py, dz = (double) getZ() + 0.5 - pz; return dx * dx + dy * dy + dz * dz; }
    public int distManhattan(Vec3i o) { return Math.abs(getX() - o.getX()) + Math.abs(getY() - o.getY()) + Math.abs(getZ() - o.getZ()); }
    public Vec3i offset(int dx, int dy, int dz) { return dx == 0 && dy == 0 && dz == 0 ? this : new Vec3i(getX() + dx, getY() + dy, getZ() + dz); }
    @Override public boolean equals(Object o) { return o instanceof Vec3i v && getX() == v.getX() && getY() == v.getY() && getZ() == v.getZ(); }
    @Override public int hashCode() { return (getY() + getZ() * 31) * 31 + getX(); }
    @Override public int compareTo(Vec3i o) { return getY() == o.getY() ? (getZ() == o.getZ() ? getX() - o.getX() : getZ() - o.getZ()) : getY() - o.getY(); }
    @Override public String toString() { return "Vec3i{x=" + getX() + ", y=" + getY() + ", z=" + getZ() + "}"; }
}
