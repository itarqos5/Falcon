package net.minecraft.core;

public class BlockPos extends Vec3i {
    public static final BlockPos ZERO = new BlockPos(0, 0, 0);
    private static final int PACKED_X_LENGTH = 26, PACKED_Z_LENGTH = 26, PACKED_Y_LENGTH = 12;
    private static final long PACKED_X_MASK = (1L << PACKED_X_LENGTH) - 1L, PACKED_Y_MASK = (1L << PACKED_Y_LENGTH) - 1L, PACKED_Z_MASK = (1L << PACKED_Z_LENGTH) - 1L;
    private static final int Y_OFFSET = 0, Z_OFFSET = PACKED_Y_LENGTH, X_OFFSET = PACKED_Y_LENGTH + PACKED_Z_LENGTH;

    public BlockPos(int x, int y, int z) { super(x, y, z); }
    public BlockPos(Vec3i v) { this(v.getX(), v.getY(), v.getZ()); }

    public static BlockPos containing(double x, double y, double z) { return new BlockPos(net.minecraft.util.Mth.floor(x), net.minecraft.util.Mth.floor(y), net.minecraft.util.Mth.floor(z)); }
    public static BlockPos containing(net.minecraft.world.phys.Vec3 v) { return containing(v.x, v.y, v.z); }
    public static long asLong(int x, int y, int z) { long l = 0L; l |= ((long) x & PACKED_X_MASK) << X_OFFSET; l |= ((long) y & PACKED_Y_MASK) << Y_OFFSET; l |= ((long) z & PACKED_Z_MASK) << Z_OFFSET; return l; }
    public static int getX(long packed) { return (int) (packed << 64 - X_OFFSET - PACKED_X_LENGTH >> 64 - PACKED_X_LENGTH); }
    public static int getY(long packed) { return (int) (packed << 64 - PACKED_Y_LENGTH >> 64 - PACKED_Y_LENGTH); }
    public static int getZ(long packed) { return (int) (packed << 64 - Z_OFFSET - PACKED_Z_LENGTH >> 64 - PACKED_Z_LENGTH); }
    public static BlockPos of(long packed) { return new BlockPos(getX(packed), getY(packed), getZ(packed)); }

    public long asLong() { return asLong(getX(), getY(), getZ()); }
    public BlockPos immutable() { return this; }
    @Override public BlockPos offset(int dx, int dy, int dz) { return dx == 0 && dy == 0 && dz == 0 ? this : new BlockPos(getX() + dx, getY() + dy, getZ() + dz); }
    public BlockPos offset(Vec3i v) { return offset(v.getX(), v.getY(), v.getZ()); }
    public BlockPos above() { return relative(Direction.UP); }
    public BlockPos above(int n) { return relative(Direction.UP, n); }
    public BlockPos below() { return relative(Direction.DOWN); }
    public BlockPos below(int n) { return relative(Direction.DOWN, n); }
    public BlockPos north() { return relative(Direction.NORTH); }
    public BlockPos north(int n) { return relative(Direction.NORTH, n); }
    public BlockPos south() { return relative(Direction.SOUTH); }
    public BlockPos south(int n) { return relative(Direction.SOUTH, n); }
    public BlockPos west() { return relative(Direction.WEST); }
    public BlockPos west(int n) { return relative(Direction.WEST, n); }
    public BlockPos east() { return relative(Direction.EAST); }
    public BlockPos east(int n) { return relative(Direction.EAST, n); }
    public BlockPos relative(Direction d) { return new BlockPos(getX() + d.getStepX(), getY() + d.getStepY(), getZ() + d.getStepZ()); }
    public BlockPos relative(Direction d, int n) { return n == 0 ? this : new BlockPos(getX() + d.getStepX() * n, getY() + d.getStepY() * n, getZ() + d.getStepZ() * n); }
    public net.minecraft.world.phys.Vec3 getCenter() { return net.minecraft.world.phys.Vec3.atCenterOf(this); }
    public net.minecraft.world.phys.Vec3 getBottomCenter() { return net.minecraft.world.phys.Vec3.atBottomCenterOf(this); }
    @Override public String toString() { return "BlockPos{x=" + getX() + ", y=" + getY() + ", z=" + getZ() + "}"; }

    public static class MutableBlockPos extends BlockPos {
        private int mx, my, mz;

        public MutableBlockPos() { this(0, 0, 0); }
        public MutableBlockPos(int x, int y, int z) { super(0, 0, 0); mx = x; my = y; mz = z; }
        public MutableBlockPos(double x, double y, double z) { this(net.minecraft.util.Mth.floor(x), net.minecraft.util.Mth.floor(y), net.minecraft.util.Mth.floor(z)); }

        @Override public int getX() { return mx; }
        @Override public int getY() { return my; }
        @Override public int getZ() { return mz; }
        @Override public BlockPos offset(int dx, int dy, int dz) { return new BlockPos(mx + dx, my + dy, mz + dz); }
        @Override public BlockPos immutable() { return new BlockPos(mx, my, mz); }
        public MutableBlockPos set(int x, int y, int z) { mx = x; my = y; mz = z; return this; }
        public MutableBlockPos set(Vec3i v) { return set(v.getX(), v.getY(), v.getZ()); }
        public MutableBlockPos setWithOffset(Vec3i v, int dx, int dy, int dz) { return set(v.getX() + dx, v.getY() + dy, v.getZ() + dz); }
        public MutableBlockPos setWithOffset(Vec3i v, Direction d) { return set(v.getX() + d.getStepX(), v.getY() + d.getStepY(), v.getZ() + d.getStepZ()); }
        public MutableBlockPos move(int dx, int dy, int dz) { return set(mx + dx, my + dy, mz + dz); }
        public MutableBlockPos move(Direction d) { return set(mx + d.getStepX(), my + d.getStepY(), mz + d.getStepZ()); }
    }
}
