package net.minecraft.client.multiplayer;

import com.mojang.authlib.GameProfile;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import net.falcon.Host;
import net.minecraft.client.Minecraft;
import net.minecraft.network.Connection;
import net.minecraft.network.protocol.Packet;

public class ClientPacketListener {
    private final Minecraft minecraft;
    private final Connection connection = new Connection();

    public ClientPacketListener(Minecraft minecraft) { this.minecraft = minecraft; }

    public Connection getConnection() { return connection; }

    public Collection<PlayerInfo> getOnlinePlayers() {
        List<PlayerInfo> list = new ArrayList<>();
        list.add(new PlayerInfo(new GameProfile(minecraft.getUser().getProfileId(), minecraft.getUser().getName()), false));
        return list;
    }

    public PlayerInfo getPlayerInfo(String name) { for (PlayerInfo i : getOnlinePlayers()) { if (i.getProfile().name().equals(name)) return i; } return null; }
    public PlayerInfo getPlayerInfo(UUID id) { for (PlayerInfo i : getOnlinePlayers()) { if (i.getProfile().id().equals(id)) return i; } return null; }
    public void send(Packet<?> packet) {}
    public void sendChat(String text) { Host.sendChat(text); }
    public void sendCommand(String command) { Host.sendCommand(command); }
}
