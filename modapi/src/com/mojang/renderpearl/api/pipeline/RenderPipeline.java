package com.mojang.renderpearl.api.pipeline;

import com.mojang.renderpearl.api.vertex.VertexFormat;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

public class RenderPipeline {
    private final String location;
    private final String vertexShader, fragmentShader;
    private final PrimitiveTopology topology;
    private final DepthStencilState depthStencil;
    private final ColorTargetState colorTarget;
    private final boolean cull;
    private final VertexFormat vertexFormat;
    private final List<BindGroupLayout> layouts;

    private RenderPipeline(Builder b) {
        location = b.location; vertexShader = b.vertexShader; fragmentShader = b.fragmentShader; topology = b.topology;
        depthStencil = b.depthStencil; colorTarget = b.colorTarget; cull = b.cull; vertexFormat = b.vertexFormat; layouts = b.layouts;
    }

    public static Builder builder(Snippet... snippets) {
        Builder b = new Builder();
        for (Snippet s : snippets) s.apply(b);
        return b;
    }

    public String getLocation() { return location; }
    public String location() { return location; }
    public PrimitiveTopology getPrimitiveTopology() { return topology; }
    public DepthStencilState getDepthStencilState() { return depthStencil; }
    public ColorTargetState getColorTargetState() { return colorTarget; }
    public boolean isCull() { return cull; }
    public VertexFormat getVertexFormat() { return vertexFormat; }
    public String getVertexShader() { return vertexShader; }
    public String getFragmentShader() { return fragmentShader; }

    // How the host draws what is submitted with this pipeline: bit 0 depth test, 1 depth write, 2 blend, 3 cull, 4 line strip.
    public int hostState() {
        int s = 0;
        if (depthStencil.depthTest() != CompareOp.ALWAYS_PASS) s |= 1;
        if (depthStencil.writeDepth()) s |= 2;
        if (colorTarget.blendFunction().isPresent()) s |= 4;
        if (cull) s |= 8;
        if (topology == PrimitiveTopology.LINE_STRIP || topology == PrimitiveTopology.DEBUG_LINE_STRIP) s |= 16;
        return s;
    }

    @FunctionalInterface
    public interface Snippet { void apply(Builder builder); }

    public static class Builder {
        String location = "pipeline", vertexShader = "", fragmentShader = "";
        PrimitiveTopology topology = PrimitiveTopology.QUADS;
        DepthStencilState depthStencil = DepthStencilState.DEFAULT;
        ColorTargetState colorTarget = ColorTargetState.DEFAULT;
        boolean cull = true;
        VertexFormat vertexFormat = com.mojang.blaze3d.vertex.DefaultVertexFormat.POSITION_COLOR;
        List<BindGroupLayout> layouts = new ArrayList<>();

        public Builder withLocation(String l) { location = l; return this; }
        public Builder withVertexShader(String s) { vertexShader = s; return this; }
        public Builder withFragmentShader(String s) { fragmentShader = s; return this; }
        public Builder withPrimitiveTopology(PrimitiveTopology t) { topology = t; return this; }
        public Builder withDepthStencilState(DepthStencilState s) { depthStencil = s; return this; }
        public Builder withDepthStencilState(Optional<DepthStencilState> s) { if (s.isPresent()) depthStencil = s.get(); return this; }
        public Builder withColorTargetState(ColorTargetState s) { colorTarget = s; return this; }
        public Builder withCull(boolean c) { cull = c; return this; }
        public Builder withVertexBinding(int binding, VertexFormat format) { vertexFormat = format; return this; }
        public Builder withBindGroupLayout(BindGroupLayout l) { layouts.add(l); return this; }
        public RenderPipeline build() { return new RenderPipeline(this); }
    }
}
