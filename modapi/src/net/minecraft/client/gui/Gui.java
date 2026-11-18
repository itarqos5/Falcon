package net.minecraft.client.gui;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;

public class Gui {
    public final Hud hud = new Hud();
    private final Minecraft minecraft;
    private final GuiGraphics extractor = new GuiGraphics();

    public Gui(Minecraft minecraft) { this.minecraft = minecraft; }

    public Screen screen() { return minecraft.screen; }
    public GuiGraphics extractor() { return extractor; }
}
