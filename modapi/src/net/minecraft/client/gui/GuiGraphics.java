package net.minecraft.client.gui;

import net.minecraft.network.chat.Component;

// The older name of what mods draw with (and its older method names): the same thing as GuiGraphicsExtractor.
public class GuiGraphics extends GuiGraphicsExtractor {
    public int drawString(Font font, String text, int x, int y, int argb) { text(font, text, x, y, argb, true); return font.width(text); }
    public int drawString(Font font, String text, int x, int y, int argb, boolean shadow) { text(font, text, x, y, argb, shadow); return font.width(text); }
    public int drawString(Font font, Component text, int x, int y, int argb) { text(font, text.getString(), x, y, argb, true); return font.width(text); }
    public void drawCenteredString(Font font, String text, int x, int y, int argb) { centeredText(font, text, x, y, argb); }
    public void drawCenteredString(Font font, Component text, int x, int y, int argb) { centeredText(font, text.getString(), x, y, argb); }
    public void hLine(int x0, int x1, int y, int argb) { horizontalLine(x0, x1, y, argb); }
    public void vLine(int x, int y0, int y1, int argb) { verticalLine(x, y0, y1, argb); }
    public void renderOutline(int x, int y, int w, int h, int argb) { outline(x, y, w, h, argb); }
    public void renderItem(net.minecraft.world.item.ItemStack stack, int x, int y) { item(stack, x, y); }
}
