package net.minecraft.world.level.block.state;
import java.util.function.ToIntFunction;
import net.minecraft.world.level.block.SoundType;
public class BlockBehaviour {
    public static class Properties {
        public static Properties of() { return null; }
        public Properties strength(float hardness, float resistance) { return this; }
        public Properties strength(float both) { return this; }
        public Properties sound(SoundType sound) { return this; }
        public Properties lightLevel(ToIntFunction<Object> light) { return this; }
        public Properties requiresCorrectToolForDrops() { return this; }
    }
}
