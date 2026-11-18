package net.minecraft.network.protocol.game;

import net.minecraft.network.chat.Component;
import net.minecraft.world.inventory.MenuType;

public class ClientboundOpenScreenPacket {
    private final int containerId;
    private final MenuType<?> type;
    private final Component title;

    public ClientboundOpenScreenPacket(int containerId, MenuType<?> type, Component title) {
        this.containerId = containerId;
        this.type = type;
        this.title = title;
    }

    public int getContainerId() { return containerId; }
    public MenuType<?> getType() { return type; }
    public Component getTitle() { return title; }
}
