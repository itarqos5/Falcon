package net.minecraft.world.level.block;

import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.ItemLike;
import net.minecraft.world.level.block.state.BlockBehaviour;
import net.minecraft.world.level.block.state.BlockState;

public class Block extends BlockBehaviour implements ItemLike {
    private BlockState defaultState;

    public Block(BlockBehaviour.Properties properties) { super(properties); this.defaultState = new BlockState(this); }

    public static String idOf(Block block) { net.minecraft.resources.Identifier id = BuiltInRegistries.BLOCK.getKey(block); return id == null ? "" : id.toString(); }
    public BlockState defaultBlockState() { return defaultState; }
    @Override public Item asItem() { net.minecraft.resources.Identifier id = BuiltInRegistries.BLOCK.getKey(this); Item i = BuiltInRegistries.ITEM.getValue(id); return i == null ? net.minecraft.world.item.Items.AIR : i; }
    @Override public String toString() { return "Block{" + BuiltInRegistries.BLOCK.getKey(this) + "}"; }
}
