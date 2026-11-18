package net.minecraft.client.renderer.rendertype;

public final class LayeringTransform {
    public static final LayeringTransform NO_LAYERING = new LayeringTransform("none");
    public static final LayeringTransform VIEW_OFFSET_Z_LAYERING = new LayeringTransform("view_offset_z");
    private final String name;

    private LayeringTransform(String name) { this.name = name; }
    public String name() { return name; }
}
