package com.mojang.blaze3d.platform;

import net.falcon.Host;

public class Window {
    public int getGuiScaledWidth() { return (int) Host.window()[0]; }
    public int getGuiScaledHeight() { return (int) Host.window()[1]; }
    public int getWidth() { return (int) Host.window()[2]; }
    public int getHeight() { return (int) Host.window()[3]; }
    public int getScreenWidth() { return getWidth(); }
    public int getScreenHeight() { return getHeight(); }
    public int getGuiScale() { return (int) Host.window()[4]; }
    public long handle() { return 1L; }
    public String getPlatform() { return "windows"; }
    public boolean isFullscreen() { return false; }
}
