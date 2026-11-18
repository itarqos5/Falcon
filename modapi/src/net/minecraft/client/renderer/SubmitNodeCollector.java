package net.minecraft.client.renderer;

import com.mojang.blaze3d.vertex.BufferBuilder;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.renderpearl.api.pipeline.PrimitiveTopology;
import net.falcon.Host;
import net.minecraft.client.renderer.rendertype.RenderType;

// What the mods submit to be drawn in the level: custom geometry (a renderer that gets a vertex consumer), which the client draws after the entities.
public class SubmitNodeCollector {
    public interface CustomGeometryRenderer { void render(PoseStack.Pose pose, com.mojang.blaze3d.vertex.VertexConsumer consumer); }

    private final BufferBuilder builder = new BufferBuilder();

    public void submitCustomGeometry(PoseStack poseStack, RenderType type, CustomGeometryRenderer renderer) {
        builder.clear();
        renderer.render(poseStack.last(), builder);
        if (builder.vertexCount() == 0) return;
        com.mojang.renderpearl.api.pipeline.RenderPipeline pipeline = type.pipeline();
        PrimitiveTopology t = pipeline.getPrimitiveTopology();
        int topology = (t == PrimitiveTopology.LINES || t == PrimitiveTopology.LINE_STRIP || t == PrimitiveTopology.DEBUG_LINES || t == PrimitiveTopology.DEBUG_LINE_STRIP) ? 0 : (t == PrimitiveTopology.QUADS ? 2 : 1);
        Host.submit(builder.data(), builder.vertexCount(), topology, pipeline.hostState(), builder.lineWidth(), "");
    }

    public void flush() {}
}
