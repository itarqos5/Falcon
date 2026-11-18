package net.minecraft.client.gui.components;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.network.chat.Component;

public class Button extends AbstractWidget {
    protected final OnPress onPress;

    @FunctionalInterface
    public interface OnPress {
        void onPress(Button button);
    }

    protected Button(int x, int y, int width, int height, Component message, OnPress onPress) {
        super(x, y, width, height, message);
        this.onPress = onPress != null ? onPress : b -> {};
    }

    public static Builder builder(Component message, OnPress onPress) {
        return new Builder(message, onPress);
    }

    @Override
    public void onClick(double mouseX, double mouseY) {
        onPress.onPress(this);
    }

    @Override
    public void extractWidgetRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        // Draw basic button background and centered text
        int bgColor = !active ? 0xFF555555 : (isHovered ? 0xFF888888 : 0xFF333333);
        graphics.fill(x, y, x + width, y + height, bgColor);
        graphics.outline(x, y, width, height, isFocused() ? 0xFFFFFFFF : 0xFF000000);
        net.minecraft.client.Minecraft mc = net.minecraft.client.Minecraft.getInstance();
        if (mc != null && mc.font != null) {
            int textColor = !active ? 0xFFA0A0A0 : 0xFFFFFFFF;
            graphics.centeredText(mc.font, getMessage(), x + width / 2, y + (height - 8) / 2, textColor);
        }
    }

    public static class Builder {
        private final Component message;
        private final OnPress onPress;
        private int x;
        private int y;
        private int width = 150;
        private int height = 20;
        private Tooltip tooltip;

        public Builder(Component message, OnPress onPress) {
            this.message = message;
            this.onPress = onPress;
        }

        public Builder bounds(int x, int y, int width, int height) {
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
            return this;
        }

        public Builder pos(int x, int y) {
            this.x = x;
            this.y = y;
            return this;
        }

        public Builder size(int width, int height) {
            this.width = width;
            this.height = height;
            return this;
        }

        public Builder tooltip(Tooltip tooltip) {
            this.tooltip = tooltip;
            return this;
        }

        public Button build() {
            Button btn = new Button(x, y, width, height, message, onPress);
            if (tooltip != null) btn.setTooltip(tooltip);
            return btn;
        }
    }
}
