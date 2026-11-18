package net.minecraft.client.gui.components;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

public abstract class AbstractWidget implements Renderable, GuiEventListener {
    public int x;
    public int y;
    public int width;
    public int height;
    public boolean active = true;
    public boolean visible = true;
    protected boolean isHovered;
    protected boolean focused;
    private Component message;
    private Tooltip tooltip;

    public AbstractWidget(int x, int y, int width, int height, Component message) {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
        this.message = message != null ? message : Component.empty();
    }

    public int getX() { return x; }
    public void setX(int x) { this.x = x; }
    public int getY() { return y; }
    public void setY(int y) { this.y = y; }
    public int getWidth() { return width; }
    public void setWidth(int width) { this.width = width; }
    public int getHeight() { return height; }
    public void setHeight(int height) { this.height = height; }

    public Component getMessage() { return message; }
    public void setMessage(Component message) { this.message = message != null ? message : Component.empty(); }

    public Tooltip getTooltip() { return tooltip; }
    public void setTooltip(Tooltip tooltip) { this.tooltip = tooltip; }

    public boolean isHovered() { return isHovered; }
    public void setHovered(boolean hovered) { isHovered = hovered; }

    @Override
    public boolean isFocused() { return focused; }
    @Override
    public void setFocused(boolean focused) { this.focused = focused; }

    @Override
    public boolean isMouseOver(double mouseX, double mouseY) {
        return visible && mouseX >= x && mouseX < x + width && mouseY >= y && mouseY < y + height;
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        if (!active || !visible) return false;
        if (isMouseOver(event.x(), event.y())) {
            onClick(event.x(), event.y());
            return true;
        }
        return false;
    }

    @Override
    public boolean mouseReleased(MouseButtonEvent event) {
        if (!active || !visible) return false;
        onRelease(event.x(), event.y());
        return false;
    }

    @Override
    public boolean mouseDragged(MouseButtonEvent event, double dx, double dy) {
        return false;
    }

    @Override
    public boolean keyPressed(KeyEvent event) {
        return false;
    }

    public void onClick(double mouseX, double mouseY) {}
    public void onRelease(double mouseX, double mouseY) {}

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        if (!visible) return;
        isHovered = mouseX >= x && mouseX < x + width && mouseY >= y && mouseY < y + height;
        extractWidgetRenderState(graphics, mouseX, mouseY, partialTick);
    }

    public void extractWidgetRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {}
}
