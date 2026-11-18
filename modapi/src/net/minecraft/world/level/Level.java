package net.minecraft.world.level;

import net.minecraft.core.BlockPos;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.block.state.BlockState;

public class Level implements BlockGetter {
    protected String dimension = "minecraft:overworld";

    public static final ResourceKey<Level> OVERWORLD = ResourceKey.create(net.minecraft.core.registries.Registries.DIMENSION, Identifier.withDefaultNamespace("overworld"));
    public static final ResourceKey<Level> NETHER = ResourceKey.create(net.minecraft.core.registries.Registries.DIMENSION, Identifier.withDefaultNamespace("the_nether"));
    public static final ResourceKey<Level> END = ResourceKey.create(net.minecraft.core.registries.Registries.DIMENSION, Identifier.withDefaultNamespace("the_end"));

    @SuppressWarnings("unchecked")
    public ResourceKey<Level> dimension() { return (ResourceKey<Level>) (Object) ResourceKey.create(net.minecraft.core.registries.Registries.DIMENSION, Identifier.parse(dimension)); }

    @Override
    public BlockState getBlockState(BlockPos pos) { return new BlockState(net.falcon.Host.blockAt(pos.getX(), pos.getY(), pos.getZ()), net.falcon.Host.blockFlags(pos.getX(), pos.getY(), pos.getZ())); }
    public boolean hasChunkAt(BlockPos pos) { return (net.falcon.Host.blockFlags(pos.getX(), pos.getY(), pos.getZ()) & 1) != 0; }
    public long getGameTime() { return (long) net.falcon.Host.window()[7]; }
    public boolean isClientSide() { return true; }
}
