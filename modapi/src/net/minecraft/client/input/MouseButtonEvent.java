package net.minecraft.client.input;

public record MouseButtonEvent(double x, double y, MouseButtonInfo buttonInfo) {
    public MouseButtonEvent(double x, double y, int button) { this(x, y, new MouseButtonInfo(button, 0)); }
    public int button() { return buttonInfo.button(); }
}
