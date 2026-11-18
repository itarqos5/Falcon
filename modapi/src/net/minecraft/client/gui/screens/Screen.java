package net.minecraft.client.gui.screens;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.network.chat.Component;

public class Screen {
    public int width, height;
    protected Minecraft minecraft = Minecraft.getInstance();
    protected Font font = minecraft.font;
    protected final Component title;

    protected Screen(Component title) { this.title = title; }

    public Component getTitle() { return title; }
    protected void init() {}
    public void onClose() { minecraft.setScreen(null); }
}
