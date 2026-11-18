package net.minecraft.client.gui.components;
import net.minecraft.network.chat.Component;
public class Button extends AbstractWidget {
    public interface OnPress { void onPress(Button button); }
    public static Builder builder(Component message, OnPress onPress) { return null; }
    public static class Builder {
        public Builder bounds(int x, int y, int width, int height) { return this; }
        public Builder pos(int x, int y) { return this; }
        public Builder width(int width) { return this; }
        public Button build() { return null; }
    }
}
