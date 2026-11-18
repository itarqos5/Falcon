package com.mojang.blaze3d.vertex;

import org.joml.Vector3f;

public interface VertexConsumer {
    VertexConsumer addVertex(float x, float y, float z);
    VertexConsumer setColor(int r, int g, int b, int a);
    VertexConsumer setColor(int argb);
    VertexConsumer setUv(float u, float v);
    VertexConsumer setUv1(int u, int v);
    VertexConsumer setUv2(int u, int v);
    VertexConsumer setNormal(float x, float y, float z);
    VertexConsumer setLineWidth(float width);

    default VertexConsumer setUv3(float u, float v) { return this; }

    default VertexConsumer addVertex(PoseStack.Pose pose, float x, float y, float z) {
        Vector3f p = pose.transformPosition(x, y, z, new Vector3f());
        return addVertex(p.x, p.y, p.z);
    }

    default VertexConsumer addVertex(PoseStack.Pose pose, Vector3f v) { return addVertex(pose, v.x, v.y, v.z); }

    default VertexConsumer setNormal(PoseStack.Pose pose, float x, float y, float z) { return setNormal(x, y, z); }
}
