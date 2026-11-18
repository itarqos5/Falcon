package net.fabricmc.fabric.api.client.networking.v1;

public final class ClientPlayNetworking {
    private ClientPlayNetworking() {}

    public interface PlayPayloadHandler<T> { void receive(T payload, Context context); }
    public interface Context {}

    public static <T> boolean registerGlobalReceiver(Object type, PlayPayloadHandler<T> handler) { return true; }
    public static boolean canSend(Object type) { return false; }
    public static void send(Object payload) {}
}
