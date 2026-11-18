package net.minecraft.world.level.block.state;

import net.minecraft.core.BlockPos;
import net.minecraft.tags.TagKey;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.shapes.CollisionContext;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;

// A block at a place, as the host knows it: the block's name and flags (opaque, full collision).
public class BlockState {
    private final Block block;
    private final String name;
    private final int flags;

    public BlockState(Block block) { this.block = block; this.name = null; this.flags = 3; }
    public BlockState(String name, int flags) { this.block = net.minecraft.world.level.block.Blocks.byName(name); this.name = name; this.flags = flags; }

    public Block getBlock() { return block; }
    public String blockName() { return name != null ? name : net.minecraft.world.level.block.Block.idOf(block); }
    public boolean isAir() { String n = blockName(); return n.equals("minecraft:air") || n.equals("minecraft:cave_air") || n.equals("minecraft:void_air"); }
    public boolean canOcclude() { return (flags & 2) != 0; }
    public boolean canBeReplaced() { String n = blockName(); return isAir() || n.equals("minecraft:snow") || n.equals("minecraft:short_grass") || n.equals("minecraft:tall_grass") || n.equals("minecraft:water") || n.equals("minecraft:lava"); }
    public boolean is(Object blockOrItem) { return blockOrItem == block; }
    public boolean is(TagKey<?> tag) { return false; }
    public FluidState getFluidState() { String n = blockName(); return new FluidState(n.equals("minecraft:water") ? net.minecraft.world.level.material.Fluids.WATER : (n.equals("minecraft:lava") ? net.minecraft.world.level.material.Fluids.LAVA : null)); }
    public <T extends Comparable<T>> T getValue(Property<T> property) { return null; }
    public VoxelShape getShape(BlockGetter level, BlockPos pos, CollisionContext ctx) { return (flags & 4) != 0 || (flags & 2) != 0 ? Shapes.block() : Shapes.empty(); }
    public VoxelShape getCollisionShape(BlockGetter level, BlockPos pos) { return getShape(level, pos, null); }
    public VoxelShape getCollisionShape(BlockGetter level, BlockPos pos, CollisionContext ctx) { return getShape(level, pos, ctx); }
    public Object getMenuProvider(net.minecraft.world.level.Level level, BlockPos pos) { return null; }
}
