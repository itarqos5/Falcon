package net.minecraft.client.gui.components;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

public abstract class AbstractSliderButton extends AbstractWidget {
    protected double value;
    private boolean canChangeValue;

    public AbstractSliderButton(int x, int y, int width, int height, Component message, double value) {
        super(x, y, width, height, message);
        this.value = Math.max(0.0, Math.min(1.0, value));
    }

    protected abstract void updateMessage();
    protected abstract void applyValue();

    protected void setValueFromMouse(double mouseX) {
        setValue((mouseX - (this.x + 4)) / (this.width - 8));
    }

    private void setValue(double value) {
        double d = this.value;
        this.value = Math.max(0.0, Math.min(1.0, value));
        if (d != this.value) {
            applyValue();
        }
        updateMessage();
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        if (!active || !visible) return false;
        if (isMouseOver(event.x(), event.y())) {
            setValueFromMouse(event.x());
            canChangeValue = true;
            return true;
        }
        return false;
    }

    @Override
    public boolean mouseDragged(MouseButtonEvent event, double dx, double dy) {
        if (canChangeValue) {
            setValueFromMouse(event.x());
            return true;
        }
        return false;
    }

    @Override
    public boolean mouseReleased(MouseButtonEvent event) {
        canChangeValue = false;
        return super.mouseReleased(event);
    }

    @Override
    public boolean keyPressed(KeyEvent event) {
        return false;
    }

    @Override
    public void extractWidgetRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        graphics.fill(x, y, x + width, y + height, 0xFF333333);
        int thumbX = (int) (x + value * (width - 8));
        graphics.fill(thumbX, y, thumbX + 8, y + height, isHovered ? 0xFF888888 : 0xFF666666);
        net.minecraft.client.Minecraft mc = net.minecraft.client.Minecraft.getInstance();
        if (mc != null && mc.font != null) {
            graphics.centeredText(mc.font, getMessage(), x + width / 2, y + (height - 8) / 2, 0xFFFFFFFF);
        }
    }
}
