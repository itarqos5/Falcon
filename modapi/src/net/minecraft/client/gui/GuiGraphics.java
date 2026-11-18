package net.minecraft.client.gui;

import com.mojang.renderpearl.api.pipeline.RenderPipeline;
import java.util.List;
import net.minecraft.client.renderer.state.gui.GuiRenderState;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;
import net.minecraft.world.item.ItemStack;
import org.joml.Matrix3x2fStack;

// The graphics context mods draw with: extends GuiGraphicsExtractor so it can be passed where either is expected,
// and optionally wraps an underlying extractor (for 1.20/26.x compatibility layers like wWaypoints).
public class GuiGraphics extends GuiGraphicsExtractor {
    private final GuiGraphicsExtractor delegate;

    public GuiGraphics() {
        this.delegate = null;
    }

    public GuiGraphics(GuiGraphicsExtractor delegate) {
        this.delegate = delegate;
    }

    public GuiGraphicsExtractor extractor() {
        return delegate != null ? delegate : this;
    }

    @Override
    public Matrix3x2fStack pose() {
        return delegate != null ? delegate.pose() : super.pose();
    }

    @Override
    public int guiWidth() {
        return delegate != null ? delegate.guiWidth() : super.guiWidth();
    }

    @Override
    public int guiHeight() {
        return delegate != null ? delegate.guiHeight() : super.guiHeight();
    }

    public GuiRenderState guiRenderState() {
        return new GuiRenderState();
    }

    @Override
    public void fill(int x0, int y0, int x1, int y1, int argb) {
        if (delegate != null) delegate.fill(x0, y0, x1, y1, argb);
        else super.fill(x0, y0, x1, y1, argb);
    }

    @Override
    public void fillGradient(int x0, int y0, int x1, int y1, int argb0, int argb1) {
        if (delegate != null) delegate.fillGradient(x0, y0, x1, y1, argb0, argb1);
        else super.fillGradient(x0, y0, x1, y1, argb0, argb1);
    }

    @Override
    public void horizontalLine(int x0, int x1, int y, int argb) {
        if (delegate != null) delegate.horizontalLine(x0, x1, y, argb);
        else super.horizontalLine(x0, x1, y, argb);
    }

    @Override
    public void verticalLine(int x, int y0, int y1, int argb) {
        if (delegate != null) delegate.verticalLine(x, y0, y1, argb);
        else super.verticalLine(x, y0, y1, argb);
    }

    @Override
    public void outline(int x, int y, int w, int h, int argb) {
        if (delegate != null) delegate.outline(x, y, w, h, argb);
        else super.outline(x, y, w, h, argb);
    }

    public void renderOutline(int x, int y, int w, int h, int argb) {
        outline(x, y, w, h, argb);
    }

    public void hLine(int x0, int x1, int y, int argb) {
        horizontalLine(x0, x1, y, argb);
    }

    public void vLine(int x, int y0, int y1, int argb) {
        verticalLine(x, y0, y1, argb);
    }

    @Override
    public void enableScissor(int x0, int y0, int x1, int y1) {
        if (delegate != null) delegate.enableScissor(x0, y0, x1, y1);
        else super.enableScissor(x0, y0, x1, y1);
    }

    @Override
    public void disableScissor() {
        if (delegate != null) delegate.disableScissor();
        else super.disableScissor();
    }

    @Override
    public void text(Font font, String text, int x, int y, int argb, boolean shadow) {
        if (delegate != null) delegate.text(font, text, x, y, argb, shadow);
        else super.text(font, text, x, y, argb, shadow);
    }

    @Override
    public void text(Font font, Component text, int x, int y, int argb, boolean shadow) {
        if (delegate != null) delegate.text(font, text, x, y, argb, shadow);
        else super.text(font, text, x, y, argb, shadow);
    }

    @Override
    public void text(Font font, FormattedCharSequence text, int x, int y, int argb, boolean shadow) {
        if (delegate != null) delegate.text(font, text, x, y, argb, shadow);
        else super.text(font, text, x, y, argb, shadow);
    }

    @Override
    public void text(Font font, String text, int x, int y, int argb) {
        if (delegate != null) delegate.text(font, text, x, y, argb);
        else super.text(font, text, x, y, argb);
    }

    @Override
    public void text(Font font, Component text, int x, int y, int argb) {
        if (delegate != null) delegate.text(font, text, x, y, argb);
        else super.text(font, text, x, y, argb);
    }

    @Override
    public void centeredText(Font font, String text, int x, int y, int argb) {
        if (delegate != null) delegate.centeredText(font, text, x, y, argb);
        else super.centeredText(font, text, x, y, argb);
    }

    @Override
    public void centeredText(Font font, Component text, int x, int y, int argb) {
        if (delegate != null) delegate.centeredText(font, text, x, y, argb);
        else super.centeredText(font, text, x, y, argb);
    }

    @Override
    public void centeredText(Font font, FormattedCharSequence text, int x, int y, int argb) {
        if (delegate != null) delegate.centeredText(font, text, x, y, argb);
        else super.centeredText(font, text, x, y, argb);
    }

    public int drawString(Font font, Component text, int x, int y, int argb, boolean shadow) {
        text(font, text, x, y, argb, shadow);
        return font != null && text != null ? font.width(text.getString()) : 0;
    }

    public int drawString(Font font, FormattedCharSequence text, int x, int y, int argb, boolean shadow) {
        text(font, text, x, y, argb, shadow);
        return font != null && text != null ? font.width(text.text()) : 0;
    }

    public int drawString(Font font, String text, int x, int y, int argb, boolean shadow) {
        text(font, text, x, y, argb, shadow);
        return font != null && text != null ? font.width(text) : 0;
    }

    public int drawString(Font font, String text, int x, int y, int argb) {
        return drawString(font, text, x, y, argb, true);
    }

    public int drawString(Font font, Component text, int x, int y, int argb) {
        return drawString(font, text, x, y, argb, true);
    }

    public int drawCenteredString(Font font, Component text, int x, int y, int argb) {
        centeredText(font, text, x, y, argb);
        return x;
    }

    public int drawCenteredString(Font font, FormattedCharSequence text, int x, int y, int argb) {
        centeredText(font, text, x, y, argb);
        return x;
    }

    public int drawCenteredString(Font font, String text, int x, int y, int argb) {
        centeredText(font, text, x, y, argb);
        return x;
    }

    @Override
    public void setTooltipForNextFrame(Component text, int x, int y) {
        if (delegate != null) delegate.setTooltipForNextFrame(text, x, y);
        else super.setTooltipForNextFrame(text, x, y);
    }

    @Override
    public void setComponentTooltipForNextFrame(Font font, List<Component> lines, int x, int y) {
        if (delegate != null) delegate.setComponentTooltipForNextFrame(font, lines, x, y);
        else super.setComponentTooltipForNextFrame(font, lines, x, y);
    }

    @Override
    public void item(ItemStack stack, int x, int y) {
        if (delegate != null) delegate.item(stack, x, y);
        else super.item(stack, x, y);
    }

    public void renderItem(ItemStack stack, int x, int y) {
        item(stack, x, y);
    }

    @Override
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int texW, int texH, int argb) {
        if (delegate != null) delegate.blit(pipeline, texture, x, y, u, v, w, h, texW, texH, argb);
        else super.blit(pipeline, texture, x, y, u, v, w, h, texW, texH, argb);
    }

    @Override
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int texW, int texH) {
        blit(pipeline, texture, x, y, u, v, w, h, texW, texH, -1);
    }

    @Override
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int regionW, int regionH, int texW, int texH) {
        if (delegate != null) delegate.blit(pipeline, texture, x, y, u, v, w, h, regionW, regionH, texW, texH);
        else super.blit(pipeline, texture, x, y, u, v, w, h, regionW, regionH, texW, texH);
    }

    @Override
    public void blit(RenderPipeline pipeline, Identifier texture, int x, int y, float u, float v, int w, int h, int regionW, int regionH, int texW, int texH, int argb) {
        if (delegate != null) delegate.blit(pipeline, texture, x, y, u, v, w, h, regionW, regionH, texW, texH, argb);
        else super.blit(pipeline, texture, x, y, u, v, w, h, regionW, regionH, texW, texH, argb);
    }

    @Override
    public void blitSprite(RenderPipeline pipeline, Identifier sprite, int x, int y, int w, int h) {
        if (delegate != null) delegate.blitSprite(pipeline, sprite, x, y, w, h);
        else super.blitSprite(pipeline, sprite, x, y, w, h);
    }
}
