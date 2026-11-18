package net.minecraft.client;

public class MouseHandler {
    public double xpos() { return net.falcon.Host.window()[5]; }
    public double ypos() { return net.falcon.Host.window()[6]; }
    public boolean isMouseGrabbed() { return true; }
}
