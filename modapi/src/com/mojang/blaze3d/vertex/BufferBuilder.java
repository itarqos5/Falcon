package com.mojang.blaze3d.vertex;

// What a mod's vertices are collected into, nine floats each: x y z r g b a u v. The client draws them (Host.submit).
public class BufferBuilder implements VertexConsumer {
    private float[] data = new float[9 * 64];
    private int count = 0;
    private float lineWidth = 1.0F;

    public int vertexCount() { return count; }
    public float[] data() { return data; }
    public float lineWidth() { return lineWidth; }
    public void clear() { count = 0; }

    @Override
    public VertexConsumer addVertex(float x, float y, float z) {
        if ((count + 1) * 9 > data.length) {
            float[] bigger = new float[data.length * 2];
            for (int i = 0; i < count * 9; i++) bigger[i] = data[i];
            data = bigger;
        }
        int o = count * 9;
        data[o] = x; data[o + 1] = y; data[o + 2] = z;
        data[o + 3] = 1.0F; data[o + 4] = 1.0F; data[o + 5] = 1.0F; data[o + 6] = 1.0F; data[o + 7] = 0.0F; data[o + 8] = 0.0F;
        count++;
        return this;
    }

    @Override public VertexConsumer setColor(int r, int g, int b, int a) { int o = (count - 1) * 9; data[o + 3] = r / 255.0F; data[o + 4] = g / 255.0F; data[o + 5] = b / 255.0F; data[o + 6] = a / 255.0F; return this; }
    @Override public VertexConsumer setColor(int argb) { return setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, (argb >>> 24) & 255); }
    @Override public VertexConsumer setUv(float u, float v) { int o = (count - 1) * 9; data[o + 7] = u; data[o + 8] = v; return this; }
    @Override public VertexConsumer setUv1(int u, int v) { return this; }
    @Override public VertexConsumer setUv2(int u, int v) { return this; }
    @Override public VertexConsumer setNormal(float x, float y, float z) { return this; }
    @Override public VertexConsumer setLineWidth(float width) { lineWidth = width; return this; }
}
