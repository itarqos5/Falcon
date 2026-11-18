package net.minecraft.world.level.block.state;

import java.util.function.ToIntFunction;
import net.minecraft.world.level.block.SoundType;

public class BlockBehaviour {
    public final Properties properties;

    public BlockBehaviour(Properties properties) { this.properties = properties; }

    public static class Properties {
        public float hardness = 0.0F, resistance = 0.0F;
        public int light = 0;
        public boolean requiresTool = false, noOcclusion = false;
        public String sound = "stone";

        public static Properties of() { return new Properties(); }
        public static Properties ofFullCopy(BlockBehaviour b) { Properties p = new Properties(); p.hardness = b.properties.hardness; p.resistance = b.properties.resistance; p.light = b.properties.light; p.sound = b.properties.sound; return p; }
        public static Properties ofLegacyCopy(BlockBehaviour b) { return ofFullCopy(b); }
        public Properties strength(float h, float r) { hardness = h; resistance = r; return this; }
        public Properties strength(float both) { return strength(both, both); }
        public Properties destroyTime(float h) { hardness = h; return this; }
        public Properties explosionResistance(float r) { resistance = r; return this; }
        public Properties instabreak() { hardness = 0.0F; return this; }
        public Properties sound(SoundType s) { sound = s.name; return this; }
        public Properties lightLevel(ToIntFunction<BlockState> f) { light = Math.max(0, Math.min(15, f.applyAsInt(new BlockState(null)))); return this; }
        public Properties requiresCorrectToolForDrops() { requiresTool = true; return this; }
        public Properties noOcclusion() { noOcclusion = true; return this; }
        public Properties noCollision() { return this; }
        public Properties setId(Object key) { return this; }
        public Properties mapColor(Object c) { return this; }
        public Properties instrument(Object i) { return this; }
        public Properties pushReaction(Object p) { return this; }
    }
}
