package net.fabricmc.fabric.api.client.command.v2;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;

public interface FabricClientCommandSource {
    void sendFeedback(Component message);
    void sendError(Component message);
    Minecraft getClient();
    LocalPlayer getPlayer();
}
