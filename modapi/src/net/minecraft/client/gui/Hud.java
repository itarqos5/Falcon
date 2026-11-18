package net.minecraft.client.gui;

import net.falcon.Host;
import net.minecraft.client.gui.components.ChatComponent;
import net.minecraft.network.chat.Component;

public class Hud {
    private final ChatComponent chat = new ChatComponent();

    public ChatComponent getChat() { return chat; }
    public void setOverlayMessage(Component message, boolean animate) { Host.overlay(message.getString()); }
}
