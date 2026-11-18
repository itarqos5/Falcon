package net.fabricmc.fabric.api.client.networking.v1;

import net.minecraft.network.protocol.common.custom.CustomPacketPayload;

public final class ClientPlayNetworking {
    private ClientPlayNetworking() {}

    public interface PlayPayloadHandler<T extends CustomPacketPayload> { void receive(T payload, Context context); }
    public interface Context {}

    public static <T extends CustomPacketPayload> boolean registerGlobalReceiver(CustomPacketPayload.Type<T> type, PlayPayloadHandler<T> handler) { return true; }
    public static boolean canSend(CustomPacketPayload.Type<?> type) { return false; }
    public static void send(CustomPacketPayload payload) {}
}
