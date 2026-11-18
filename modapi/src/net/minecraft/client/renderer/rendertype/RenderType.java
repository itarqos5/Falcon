package net.minecraft.client.renderer.rendertype;

import com.mojang.renderpearl.api.pipeline.RenderPipeline;

public class RenderType {
    private final String name;
    public final RenderSetup state;

    protected RenderType(String name, RenderSetup state) { this.name = name; this.state = state; }

    // (the game keeps this private: mods reach it through a mixin invoker)
    private static RenderType create(String name, RenderSetup setup) { return new RenderType(name, setup); }
    public static RenderType create(String name, RenderPipeline pipeline) { return new RenderType(name, RenderSetup.builder(pipeline).createRenderSetup()); }

    public String name() { return name; }
    public RenderPipeline pipeline() { return state.pipeline(); }
    public RenderSetup getRenderSetup() { return state; }
    public String toString() { return name; }
}
