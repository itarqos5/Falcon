package net.minecraft.client.gui.components;

import net.falcon.Host;
import net.minecraft.network.chat.Component;

public class ChatComponent {
    public void addMessage(Component message) { Host.chat(message.getString()); }
    public void addClientSystemMessage(Component message) { Host.chat(message.getString()); }
    public void clearMessages(boolean history) {}
}
