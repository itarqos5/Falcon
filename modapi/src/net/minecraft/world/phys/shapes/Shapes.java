package net.minecraft.world.phys.shapes;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.phys.AABB;

public final class Shapes {
    private Shapes() {}

    public interface DoubleLineConsumer { void consume(double x1, double y1, double z1, double x2, double y2, double z2); }

    private static final VoxelShape EMPTY = new VoxelShape(new ArrayList<>());

    public static VoxelShape empty() { return EMPTY; }
    public static VoxelShape block() { return box(0, 0, 0, 1, 1, 1); }
    public static VoxelShape box(double x1, double y1, double z1, double x2, double y2, double z2) { List<AABB> l = new ArrayList<>(); l.add(new AABB(x1, y1, z1, x2, y2, z2)); return new VoxelShape(l); }
    public static VoxelShape create(AABB box) { List<AABB> l = new ArrayList<>(); l.add(box); return new VoxelShape(l); }
    public static VoxelShape or(VoxelShape a, VoxelShape b) { List<AABB> l = new ArrayList<>(a.toAabbs()); l.addAll(b.toAabbs()); return new VoxelShape(l); }
    public static VoxelShape or(VoxelShape a, VoxelShape... more) { VoxelShape r = a; for (VoxelShape m : more) r = or(r, m); return r; }
    public static VoxelShape joinUnoptimized(VoxelShape a, VoxelShape b, BooleanOp op) { return op == BooleanOp.OR ? or(a, b) : (op == BooleanOp.AND ? EMPTY : a); }
}
