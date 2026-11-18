package net.minecraft.network.protocol.game;

public class ServerboundContainerClosePacket {
    private final int containerId;

    public ServerboundContainerClosePacket(int containerId) {
        this.containerId = containerId;
    }

    public int getContainerId() { return containerId; }
}
