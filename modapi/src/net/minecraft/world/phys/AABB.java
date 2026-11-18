package net.minecraft.world.phys;

import java.util.Optional;
import net.minecraft.core.BlockPos;

public class AABB {
    public final double minX, minY, minZ, maxX, maxY, maxZ;

    public AABB(double x1, double y1, double z1, double x2, double y2, double z2) {
        this.minX = Math.min(x1, x2); this.minY = Math.min(y1, y2); this.minZ = Math.min(z1, z2);
        this.maxX = Math.max(x1, x2); this.maxY = Math.max(y1, y2); this.maxZ = Math.max(z1, z2);
    }

    public AABB(BlockPos p) { this(p.getX(), p.getY(), p.getZ(), p.getX() + 1, p.getY() + 1, p.getZ() + 1); }
    public AABB(BlockPos a, BlockPos b) { this(a.getX(), a.getY(), a.getZ(), b.getX(), b.getY(), b.getZ()); }
    public AABB(Vec3 a, Vec3 b) { this(a.x, a.y, a.z, b.x, b.y, b.z); }

    public static AABB unitCubeFromLowerCorner(Vec3 v) { return new AABB(v.x, v.y, v.z, v.x + 1.0, v.y + 1.0, v.z + 1.0); }
    public static AABB ofSize(Vec3 c, double sx, double sy, double sz) { return new AABB(c.x - sx / 2.0, c.y - sy / 2.0, c.z - sz / 2.0, c.x + sx / 2.0, c.y + sy / 2.0, c.z + sz / 2.0); }

    public double getXsize() { return maxX - minX; }
    public double getYsize() { return maxY - minY; }
    public double getZsize() { return maxZ - minZ; }
    public Vec3 getCenter() { return new Vec3((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5); }
    public AABB inflate(double x, double y, double z) { return new AABB(minX - x, minY - y, minZ - z, maxX + x, maxY + y, maxZ + z); }
    public AABB inflate(double v) { return inflate(v, v, v); }
    public AABB deflate(double v) { return inflate(-v); }
    public AABB move(double x, double y, double z) { return new AABB(minX + x, minY + y, minZ + z, maxX + x, maxY + y, maxZ + z); }
    public AABB move(Vec3 v) { return move(v.x, v.y, v.z); }
    public AABB move(BlockPos p) { return move(p.getX(), p.getY(), p.getZ()); }
    public AABB minmax(AABB o) { return new AABB(Math.min(minX, o.minX), Math.min(minY, o.minY), Math.min(minZ, o.minZ), Math.max(maxX, o.maxX), Math.max(maxY, o.maxY), Math.max(maxZ, o.maxZ)); }
    public boolean intersects(AABB o) { return minX < o.maxX && maxX > o.minX && minY < o.maxY && maxY > o.minY && minZ < o.maxZ && maxZ > o.minZ; }
    public boolean contains(double x, double y, double z) { return x >= minX && x < maxX && y >= minY && y < maxY && z >= minZ && z < maxZ; }
    public boolean contains(Vec3 v) { return contains(v.x, v.y, v.z); }

    // Where a line from one point to another first enters the box, if it does.
    public Optional<Vec3> clip(Vec3 from, Vec3 to) {
        double[] t = {1.0};
        double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        boolean hit = clipAxis(from.x, from.y, from.z, dx, dy, dz, t);
        return hit ? Optional.of(new Vec3(from.x + dx * t[0], from.y + dy * t[0], from.z + dz * t[0])) : Optional.empty();
    }

    private boolean clipAxis(double ox, double oy, double oz, double dx, double dy, double dz, double[] result) {
        double tMin = 0.0, tMax = 1.0;
        double[] o = {ox, oy, oz}, d = {dx, dy, dz}, lo = {minX, minY, minZ}, hi = {maxX, maxY, maxZ};
        for (int a = 0; a < 3; a++) {
            if (Math.abs(d[a]) < 1.0E-7) {
                if (o[a] < lo[a] || o[a] > hi[a]) return false;
            } else {
                double t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
                if (t1 > t2) { double s = t1; t1 = t2; t2 = s; }
                tMin = Math.max(tMin, t1);
                tMax = Math.min(tMax, t2);
                if (tMin > tMax) return false;
            }
        }
        result[0] = tMin;
        return true;
    }

    @Override public boolean equals(Object o) { return o instanceof AABB b && b.minX == minX && b.minY == minY && b.minZ == minZ && b.maxX == maxX && b.maxY == maxY && b.maxZ == maxZ; }
    @Override public int hashCode() { return Double.hashCode(minX) * 31 + Double.hashCode(minY) * 17 + Double.hashCode(minZ) * 7 + Double.hashCode(maxX) * 5 + Double.hashCode(maxY) * 3 + Double.hashCode(maxZ); }
    @Override public String toString() { return "AABB[" + minX + ", " + minY + ", " + minZ + "] -> [" + maxX + ", " + maxY + ", " + maxZ + "]"; }
}
