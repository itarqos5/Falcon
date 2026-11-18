package net.fabricmc.fabric.api.client.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.client.Minecraft;

public final class ClientLifecycleEvents {
    private ClientLifecycleEvents() {}

    public static final Event<ClientStarted> CLIENT_STARTED = EventFactory.createArrayBacked(ClientStarted.class, callbacks -> client -> { for (ClientStarted c : callbacks) c.onClientStarted(client); });
    public static final Event<ClientStopping> CLIENT_STOPPING = EventFactory.createArrayBacked(ClientStopping.class, callbacks -> client -> { for (ClientStopping c : callbacks) c.onClientStopping(client); });

    public interface ClientStarted { void onClientStarted(Minecraft client); }
    public interface ClientStopping { void onClientStopping(Minecraft client); }
}
