package net.minecraft.world.level;

import net.minecraft.world.phys.Vec3;

public record ClipContext(Vec3 from, Vec3 to) {
    public enum Block { COLLIDER, OUTLINE, VISUAL, FALLDAMAGE_RESETTING }
    public enum Fluid { NONE, SOURCE_ONLY, ANY, WATER }

    public ClipContext(Vec3 from, Vec3 to, Block block, Fluid fluid, net.minecraft.world.entity.Entity entity) { this(from, to); }
}
