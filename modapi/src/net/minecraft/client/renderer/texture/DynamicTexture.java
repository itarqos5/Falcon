package net.minecraft.client.renderer.texture;

import com.mojang.blaze3d.platform.NativeImage;
import java.util.function.Supplier;

public class DynamicTexture extends AbstractTexture {
    private NativeImage pixels;

    public DynamicTexture(Supplier<String> name, NativeImage pixels) { this.pixels = pixels; }
    public NativeImage getPixels() { return pixels; }
    public void upload() {}
    @Override public void close() { if (pixels != null) pixels.close(); pixels = null; }
}
