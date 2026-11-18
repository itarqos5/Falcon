package net.minecraft.world.entity;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

public class Entity {
    protected double x, y, z;
    protected float yRot, xRot;
    protected double eyeHeight = 1.62;
    protected boolean sneaking, onGround, inWater;
    protected net.minecraft.client.Minecraft minecraft;

    public double getX() { return x; }
    public double getY() { return y; }
    public double getZ() { return z; }
    public float getYRot() { return yRot; }
    public float getXRot() { return xRot; }
    public double getEyeY() { return y + eyeHeight; }
    public Vec3 position() { return new Vec3(x, y, z); }
    public BlockPos blockPosition() { return BlockPos.containing(x, y, z); }
    public Vec3 getEyePosition() { return new Vec3(x, y + eyeHeight, z); }
    public Vec3 getEyePosition(float partial) { return getEyePosition(); }
    public Vec3 getViewVector(float partial) { return Vec3.directionFromRotation(xRot, yRot); }
    public Direction getDirection() { return Direction.fromYRot(yRot); }
    public boolean isShiftKeyDown() { return sneaking; }
    public boolean onGround() { return onGround; }
    public boolean isInWater() { return inWater; }
    public AABB getBoundingBox() { return new AABB(x - 0.3, y, z - 0.3, x + 0.3, y + 1.8, z + 0.3); }
    public Level level() { return minecraft == null ? null : minecraft.level; }
    public net.minecraft.world.entity.Pose getPose() { return sneaking ? Pose.CROUCHING : Pose.STANDING; }
    public java.util.UUID getUUID() { return java.util.UUID.fromString("00000000-0000-0000-0000-000000000000"); }
}
