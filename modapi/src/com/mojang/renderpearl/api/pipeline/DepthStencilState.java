package com.mojang.renderpearl.api.pipeline;

public record DepthStencilState(CompareOp depthTest, boolean writeDepth, float depthBiasScaleFactor, float depthBiasConstant) {
    public static final DepthStencilState DEFAULT = new DepthStencilState(CompareOp.LESS_THAN_OR_EQUAL, true, 0.0F, 0.0F);
}
