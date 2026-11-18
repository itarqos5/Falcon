package net.minecraft.network.protocol.game;

public class ClientboundContainerSetContentPacket {
    private final int containerId;

    public ClientboundContainerSetContentPacket(int containerId) {
        this.containerId = containerId;
    }

    public int containerId() { return containerId; }
}
