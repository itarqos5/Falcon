package net.minecraft.core;

public enum Direction {
    DOWN(0, -1, 0, Axis.Y), UP(0, 1, 0, Axis.Y), NORTH(0, 0, -1, Axis.Z), SOUTH(0, 0, 1, Axis.Z), WEST(-1, 0, 0, Axis.X), EAST(1, 0, 0, Axis.X);

    public enum Axis { X, Y, Z; public boolean isHorizontal() { return this != Y; } public boolean isVertical() { return this == Y; } }

    private final int x, y, z;
    private final Axis axis;

    Direction(int x, int y, int z, Axis axis) { this.x = x; this.y = y; this.z = z; this.axis = axis; }

    public int getStepX() { return x; }
    public int getStepY() { return y; }
    public int getStepZ() { return z; }
    public Axis getAxis() { return axis; }
    public Vec3i getUnitVec3i() { return new Vec3i(x, y, z); }
    public String getName() { return name().toLowerCase(); }
    public int get3DDataValue() { return ordinal(); }

    public Direction getOpposite() {
        switch (this) {
            case DOWN: return UP;
            case UP: return DOWN;
            case NORTH: return SOUTH;
            case SOUTH: return NORTH;
            case WEST: return EAST;
            default: return WEST;
        }
    }

    public static Direction from3DDataValue(int i) { return values()[Math.floorMod(i, 6)]; }

    public static Direction fromYRot(double yRot) {
        int i = net.minecraft.util.Mth.floor(yRot / 90.0 + 0.5) & 3;
        return i == 0 ? SOUTH : (i == 1 ? WEST : (i == 2 ? NORTH : EAST));
    }
}
