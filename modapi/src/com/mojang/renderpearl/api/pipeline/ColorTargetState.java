package com.mojang.renderpearl.api.pipeline;

import com.mojang.renderpearl.api.GpuFormat;
import java.util.Optional;

public record ColorTargetState(Optional<BlendFunction> blendFunction, GpuFormat format, int writeMask) {
    public static final ColorTargetState DEFAULT = new ColorTargetState(Optional.empty(), GpuFormat.RGBA8_UNORM, 15);
}
