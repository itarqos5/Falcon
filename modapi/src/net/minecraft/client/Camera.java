package net.minecraft.client;

import net.falcon.Host;
import net.minecraft.world.phys.Vec3;

public class Camera {
    public Vec3 position() { double[] c = Host.camera(); return new Vec3(c[0], c[1], c[2]); }
    public float yRot() { return (float) Host.camera()[3]; }
    public float xRot() { return (float) Host.camera()[4]; }
    public float getFov() { return (float) Host.camera()[5]; }
    public net.minecraft.core.BlockPos blockPosition() { return net.minecraft.core.BlockPos.containing(position()); }
}
