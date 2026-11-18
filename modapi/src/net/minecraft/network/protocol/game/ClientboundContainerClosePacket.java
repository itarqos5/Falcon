package net.minecraft.network.protocol.game;

public class ClientboundContainerClosePacket {
    private final int containerId;

    public ClientboundContainerClosePacket(int containerId) {
        this.containerId = containerId;
    }

    public int getContainerId() { return containerId; }
}
