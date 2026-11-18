package net.fabricmc.fabric.api.client.networking.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;

public final class ClientPlayConnectionEvents {
    private ClientPlayConnectionEvents() {}

    public static final Event<Init> INIT = EventFactory.createArrayBacked(Init.class, callbacks -> (handler, client) -> { for (Init c : callbacks) c.onPlayInit(handler, client); });
    public static final Event<Join> JOIN = EventFactory.createArrayBacked(Join.class, callbacks -> (handler, sender, client) -> { for (Join c : callbacks) c.onPlayReady(handler, sender, client); });
    public static final Event<Disconnect> DISCONNECT = EventFactory.createArrayBacked(Disconnect.class, callbacks -> (handler, client) -> { for (Disconnect c : callbacks) c.onPlayDisconnect(handler, client); });

    public interface Init { void onPlayInit(ClientPacketListener handler, Minecraft client); }
    public interface Join { void onPlayReady(ClientPacketListener handler, PacketSender sender, Minecraft client); }
    public interface Disconnect { void onPlayDisconnect(ClientPacketListener handler, Minecraft client); }

    public interface PacketSender {}
}
