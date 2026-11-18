package net.minecraft.client.renderer;

import com.mojang.renderpearl.api.pipeline.BlendFunction;
import com.mojang.renderpearl.api.pipeline.ColorTargetState;
import com.mojang.renderpearl.api.pipeline.CompareOp;
import com.mojang.renderpearl.api.pipeline.DepthStencilState;
import com.mojang.renderpearl.api.pipeline.PrimitiveTopology;
import com.mojang.renderpearl.api.pipeline.RenderPipeline;
import java.util.Optional;

public final class RenderPipelines {
    private RenderPipelines() {}

    public static final RenderPipeline GUI_TEXTURED = RenderPipeline.builder().withLocation("pipeline/gui_textured").withPrimitiveTopology(PrimitiveTopology.QUADS)
        .withDepthStencilState(new DepthStencilState(CompareOp.ALWAYS_PASS, false, 0.0F, 0.0F)).withColorTargetState(new ColorTargetState(Optional.of(BlendFunction.TRANSLUCENT), ColorTargetState.DEFAULT.format(), 15)).build();
    public static final RenderPipeline GUI_TEXTURED_PREMULTIPLIED_ALPHA = RenderPipeline.builder().withLocation("pipeline/gui_textured_premultiplied_alpha").withPrimitiveTopology(PrimitiveTopology.QUADS)
        .withDepthStencilState(new DepthStencilState(CompareOp.ALWAYS_PASS, false, 0.0F, 0.0F)).withColorTargetState(new ColorTargetState(Optional.of(BlendFunction.TRANSLUCENT), ColorTargetState.DEFAULT.format(), 15)).build();
    public static final RenderPipeline.Snippet MATRICES_PROJECTION_SNIPPET = b -> {};
    public static final RenderPipeline.Snippet FOG_SNIPPET = b -> {};
    public static final RenderPipeline.Snippet DEBUG_FILLED_SNIPPET = b -> {};
    public static final RenderPipeline.Snippet LINES_SNIPPET = b -> {};
}
