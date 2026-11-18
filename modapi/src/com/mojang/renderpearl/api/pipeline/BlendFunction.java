package com.mojang.renderpearl.api.pipeline;

public final class BlendFunction {
    public static final BlendFunction TRANSLUCENT = new BlendFunction("translucent");
    public static final BlendFunction ADDITIVE = new BlendFunction("additive");
    public static final BlendFunction LIGHTNING = new BlendFunction("lightning");
    public static final BlendFunction ENTITY_OUTLINE_BLIT = new BlendFunction("entity_outline_blit");
    private final String name;

    private BlendFunction(String name) { this.name = name; }
    public String name() { return name; }
}
