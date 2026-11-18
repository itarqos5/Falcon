package net.minecraft.client.renderer.rendertype;

import com.mojang.renderpearl.api.pipeline.RenderPipeline;
import java.util.HashMap;
import java.util.Map;

public class RenderSetup {
    final RenderPipeline pipeline;
    LayeringTransform layering = LayeringTransform.NO_LAYERING;
    boolean solidModelPhase = false;
    public Map<String, Object> textures = new HashMap<>();

    RenderSetup(RenderPipeline pipeline) { this.pipeline = pipeline; }

    public static RenderSetupBuilder builder(RenderPipeline pipeline) { return new RenderSetupBuilder(pipeline); }
    public RenderPipeline pipeline() { return pipeline; }

    public static class RenderSetupBuilder {
        private final RenderSetup setup;

        RenderSetupBuilder(RenderPipeline pipeline) { setup = new RenderSetup(pipeline); }

        public RenderSetupBuilder setLayeringTransform(LayeringTransform t) { setup.layering = t; return this; }
        public RenderSetupBuilder withForcedSolidModelPhase() { setup.solidModelPhase = true; return this; }
        public RenderSetupBuilder withTexture(String name, net.minecraft.resources.Identifier texture) { setup.textures.put(name, texture); return this; }
        public RenderSetup createRenderSetup() { return setup; }
    }
}
