package net.minecraft.client.multiplayer;

import net.minecraft.network.chat.Component;

public class ServerData {
    public String name;
    public String ip;
    public Component motd = Component.empty();
    private final boolean lan;

    public ServerData(String name, String ip, boolean lan) { this.name = name; this.ip = ip; this.lan = lan; }
    public boolean isLan() { return lan; }
    public boolean isRealm() { return false; }
}
