package net.minecraft.network;

public class Connection {
    public boolean isMemoryConnection() { return net.falcon.Host.worldKind() == 0; }
    public boolean isConnected() { return true; }
}
