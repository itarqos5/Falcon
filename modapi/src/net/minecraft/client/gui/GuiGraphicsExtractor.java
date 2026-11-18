package net.minecraft.client.gui;

import com.mojang.renderpearl.api.pipeline.RenderPipeline;
import java.util.List;
import net.falcon.Host;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;
import net.minecraft.world.item.ItemStack;
import org.joml.Matrix3x2fStack;

// What a mod draws on the screen: every call is told to the client (Host.draw*), which draws it with its own graphics, at the places the mod's pose (translate, scale) puts it.
public class GuiGraphicsExtractor {
    protected Matrix3x2fStack pose = new Matrix3x2fStack(32);

    public void begin() { pose().clear(); }
    public void end() {}

    public Matrix3x2fStack pose() {
        if (pose == null) pose = new Matrix3x2fStack(32);
        return pose;
    }
    public int guiWidth() { return (int) Host.window()[0]; }
    public int guiHeight() { return (int) Host.window()[1]; }

    private float tx(float x, float y) { return pose().transformX(x, y); }
    private float ty(float x, float y) { return pose().transformY(x, y); }

    public void fill(int x0, int y0, int x1, int y1, int argb) { Host.drawFill(tx(x0, y0), ty(x0, y0), tx(x1, y1), ty(x1, y1), argb); }
    public void fillGradient(int x0, int y0, int x1, int y1, int argb0, int argb1) { Host.drawGradient(tx(x0, y0), ty(x0, y0), tx(x1, y1), ty(x1, y1), argb0, argb1); }
    public void horizontalLine(int x0, int x1, int y, int argb) { fill(x0, y, x1 + 1, y + 1, argb); }
    public void verticalLine(int x, int y0, int y1, int argb) { fill(x, y0, x + 1, y1 + 1, argb); }
    public void outline(int x, int y, int w, int h, int argb) { fill(x, y, x + w, y + 1, argb); fill(x, y + h - 1, x + w, y + h, argb); fill(x, y + 1, x + 1, y + h - 1, argb); fill(x + w - 1, y + 1, x + w, y + h - 1, argb); }

    public void text(Font font, String text, int x, int y, int argb, boolean shadow) { Host.drawText(text, tx(x, y), ty(x, y), argb, shadow, false, pose().m00); }
    public void text(Font font, Component text, int x, int y, int argb, boolean shadow) { text(font, text.getString(), x, y, argb, shadow); }
    public void text(Font font, FormattedCharSequence text, int x, int y, int argb, boolean shadow) { text(font, text.text(), x, y, argb, shadow); }
    public void text(Font font, String text, int x, int y, int argb) { text(font, text, x, y, argb, true); }
    public void text(Font font, Component text, int x, int y, int argb) { text(font, text.getString(), x, y, argb, true); }
    public void centeredText(Font font, String text, int x, int y, int argb) { Host.drawText(text, tx(x, y), ty(x, y), argb, true, true, pose().m00); }
    public void centeredText(Font font, Component text, int x, int y, int argb) { centeredText(font, text.getString(), x, y, argb); }
    public void centeredText(Font font, FormattedCharSequence text, int x, int y, int argb) { centeredText(font, text.text(), x, y, argb); }

    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int texW, int texH) { blit(pipeline, texture, x, y, u, v, w, h, w, h, texW, texH); }
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int texW, int texH, int argb) { blit(pipeline, texture, x, y, u, v, w, h, w, h, texW, texH, argb); }
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int regionW, int regionH, int texW, int texH) { blit(pipeline, texture, x, y, u, v, w, h, regionW, regionH, texW, texH, -1); }
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int regionW, int regionH, int texW, int texH, int argb) {
        Host.drawTexture(texture.toString(), tx(x, y), ty(x, y), tx(x + w, y + h), ty(x + w, y + h), u, v, regionW, regionH, texW, texH, argb);
    }
    public void blitSprite(RenderPipeline pipeline, Identifier sprite, int x, int y, int w, int h) { Host.drawSprite(sprite.toString(), tx(x, y), ty(x, y), tx(x + w, y + h), ty(x + w, y + h), -1); }

    public void item(ItemStack stack, int x, int y) { if (!stack.isEmpty()) Host.drawItem(net.minecraft.core.registries.BuiltInRegistries.ITEM.getKey(stack.getItem()).toString(), tx(x, y), ty(x, y)); }
    public void enableScissor(int x0, int y0, int x1, int y1) { Host.scissor(true, tx(x0, y0), ty(x0, y0), tx(x1, y1), ty(x1, y1)); }
    public void disableScissor() { Host.scissor(false, 0, 0, 0, 0); }

    public void setTooltipForNextFrame(Component text, int x, int y) {}
    public void setComponentTooltipForNextFrame(Font font, List<Component> lines, int x, int y) {}
}
