package net.minecraft.client.gui.components.events;

import java.util.List;

public interface ContainerEventHandler extends GuiEventListener {
    List<? extends GuiEventListener> children();
    GuiEventListener getFocused();
    void setFocused(GuiEventListener focused);
}
