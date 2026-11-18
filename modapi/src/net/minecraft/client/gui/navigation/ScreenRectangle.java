package net.minecraft.client.gui.navigation;

public record ScreenRectangle(int left, int top, int width, int height) {
    public int right() { return left + width; }
    public int bottom() { return top + height; }
    public boolean containsPoint(int x, int y) { return x >= left && x < right() && y >= top && y < bottom(); }
}
