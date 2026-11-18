package com.mojang.blaze3d.platform;

import java.io.InputStream;

// A picture in memory: ABGR pixels, as the game keeps them.
public class NativeImage implements AutoCloseable {
    private final int width, height;
    private final int[] pixels;

    public NativeImage(int width, int height, boolean clear) { this.width = width; this.height = height; this.pixels = new int[Math.max(0, width * height)]; }

    public static NativeImage read(InputStream in) throws java.io.IOException { return read(in.readAllBytes()); }
    public static NativeImage read(byte[] bytes) throws java.io.IOException {
        int[] size = net.falcon.Host.decodeImageSize(bytes);
        if (size == null || size[0] <= 0) throw new java.io.IOException("not a picture");
        NativeImage image = new NativeImage(size[0], size[1], false);
        net.falcon.Host.decodeImage(bytes, image.pixels);
        return image;
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public int getPixel(int x, int y) { return pixels[y * width + x]; }
    public void setPixelABGR(int x, int y, int abgr) { pixels[y * width + x] = abgr; }
    public byte getLuminanceOrAlpha(int x, int y) { return (byte) (pixels[y * width + x] >>> 24); }
    @Override public void close() {}
}
