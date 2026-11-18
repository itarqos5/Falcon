package net.minecraft.client.player;

import net.falcon.Host;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.network.chat.Component;
import net.minecraft.world.inventory.AbstractContainerMenu;
import net.minecraft.world.inventory.InventoryMenu;

public class LocalPlayer extends AbstractClientPlayer {
    public ClientPacketListener connection;
    public ClientInput input = new ClientInput();
    public AbstractContainerMenu containerMenu = new AbstractContainerMenu();
    public InventoryMenu inventoryMenu = new InventoryMenu();

    public LocalPlayer(Minecraft minecraft) { this.minecraft = minecraft; }

    // x, y, z, yaw, pitch, eye height, health, food, flags
    public void refresh() {
        double[] p = Host.player();
        x = p[0]; y = p[1]; z = p[2];
        yRot = (float) p[3]; xRot = (float) p[4];
        eyeHeight = p[5];
        health = (float) p[6];
        int flags = (int) p[8];
        sneaking = (flags & 1) != 0;
        onGround = (flags & 2) != 0;
        inWater = (flags & 4) != 0;
        creative = (flags & 8) != 0;
        spectator = (flags & 16) != 0;
        alive = (flags & 32) != 0;
        name = Host.playerName();
        String u = Host.playerUuid();
        if (!u.isEmpty()) uuid = java.util.UUID.fromString(u);
        connection = minecraft.getConnection();
    }

    public void sendOverlayMessage(Component message) { Host.overlay(message.getString()); }
    public void displayClientMessage(Component message, boolean actionBar) { if (actionBar) Host.overlay(message.getString()); else Host.chat(message.getString()); }
    public void sendSystemMessage(Component message) { Host.chat(message.getString()); }
    public void playSound(net.minecraft.sounds.SoundEvent sound, float volume, float pitch) {}
    public net.minecraft.server.permissions.PermissionSet permissions() { return new net.minecraft.server.permissions.PermissionSet(); }
}
