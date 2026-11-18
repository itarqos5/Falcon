package net.minecraft.world.phys;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;

public class BlockHitResult extends HitResult {
    private final Direction direction;
    private final BlockPos blockPos;
    private final boolean miss, inside;

    public BlockHitResult(Vec3 location, Direction direction, BlockPos pos, boolean inside) { super(location); this.direction = direction; this.blockPos = pos; this.miss = false; this.inside = inside; }
    private BlockHitResult(boolean miss, Vec3 location, Direction direction, BlockPos pos) { super(location); this.direction = direction; this.blockPos = pos; this.miss = miss; this.inside = false; }

    public static BlockHitResult miss(Vec3 location, Direction direction, BlockPos pos) { return new BlockHitResult(true, location, direction, pos); }
    public BlockPos getBlockPos() { return blockPos; }
    public Direction getDirection() { return direction; }
    public boolean isInside() { return inside; }
    @Override public Type getType() { return miss ? Type.MISS : Type.BLOCK; }
}
