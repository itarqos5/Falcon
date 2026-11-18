package net.minecraft.client.renderer;

import com.mojang.renderpearl.api.pipeline.BindGroupLayout;

public final class BindGroupLayouts {
    private BindGroupLayouts() {}

    public static final BindGroupLayout DYNAMIC_TRANSFORMS = new BindGroupLayout("dynamic_transforms");
    public static final BindGroupLayout FOG = new BindGroupLayout("fog");
    public static final BindGroupLayout GLOBALS = new BindGroupLayout("globals");
    public static final BindGroupLayout PROJECTION = new BindGroupLayout("projection");
}
