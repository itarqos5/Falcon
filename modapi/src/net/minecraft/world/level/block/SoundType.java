package net.minecraft.world.level.block;

public class SoundType {
    public final String name;

    private SoundType(String name) { this.name = name; }

    public static final SoundType STONE = new SoundType("stone");
    public static final SoundType WOOD = new SoundType("wood");
    public static final SoundType GRAVEL = new SoundType("gravel");
    public static final SoundType GRASS = new SoundType("grass");
    public static final SoundType METAL = new SoundType("metal");
    public static final SoundType GLASS = new SoundType("glass");
    public static final SoundType WOOL = new SoundType("wool");
    public static final SoundType SAND = new SoundType("sand");
    public static final SoundType SNOW = new SoundType("snow");
    public static final SoundType AMETHYST_BLOCK = new SoundType("amethyst_block");
    public static final SoundType COPPER = new SoundType("copper");
    public static final SoundType BONE_BLOCK = new SoundType("bone_block");
    public static final SoundType DEEPSLATE = new SoundType("deepslate");
}
