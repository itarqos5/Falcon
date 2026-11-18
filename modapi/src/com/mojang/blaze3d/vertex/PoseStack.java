package com.mojang.blaze3d.vertex;

import java.util.ArrayList;
import java.util.List;
import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.joml.Vector3f;

public class PoseStack {
    private final List<Pose> stack = new ArrayList<>();

    public PoseStack() { stack.add(new Pose(new Matrix4f(), new Matrix3f())); }

    public void pushPose() { stack.add(last().copy()); }
    public void popPose() { stack.remove(stack.size() - 1); }
    public Pose last() { return stack.get(stack.size() - 1); }
    public boolean isEmpty() { return stack.size() == 1; }
    public void translate(double x, double y, double z) { last().pose.translate((float) x, (float) y, (float) z); }
    public void translate(float x, float y, float z) { last().pose.translate(x, y, z); }
    public void scale(float x, float y, float z) { last().pose.scale(x, y, z); }
    public void mulPose(Quaternionf q) { last().pose.rotate(q); }
    public void setIdentity() { last().pose.identity(); }

    public static final class Pose {
        final Matrix4f pose;
        final Matrix3f normal;

        Pose(Matrix4f pose, Matrix3f normal) { this.pose = pose; this.normal = normal; }

        public Matrix4f pose() { return pose; }
        public Matrix3f normal() { return normal; }
        public Pose copy() { return new Pose(new Matrix4f(pose), new Matrix3f()); }
        public Vector3f transformPosition(float x, float y, float z, Vector3f dest) { return pose.transformPosition(x, y, z, dest); }
    }
}
