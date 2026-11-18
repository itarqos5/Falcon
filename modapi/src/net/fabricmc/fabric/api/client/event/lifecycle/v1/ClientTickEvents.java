package net.fabricmc.fabric.api.client.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;

public final class ClientTickEvents {
    private ClientTickEvents() {}

    public static final Event<StartTick> START_CLIENT_TICK = EventFactory.createArrayBacked(StartTick.class, callbacks -> client -> { for (StartTick c : callbacks) c.onStartTick(client); });
    public static final Event<EndTick> END_CLIENT_TICK = EventFactory.createArrayBacked(EndTick.class, callbacks -> client -> { for (EndTick c : callbacks) c.onEndTick(client); });
    public static final Event<StartWorldTick> START_WORLD_TICK = EventFactory.createArrayBacked(StartWorldTick.class, callbacks -> world -> { for (StartWorldTick c : callbacks) c.onStartTick(world); });
    public static final Event<EndWorldTick> END_WORLD_TICK = EventFactory.createArrayBacked(EndWorldTick.class, callbacks -> world -> { for (EndWorldTick c : callbacks) c.onEndTick(world); });

    public interface StartTick { void onStartTick(Minecraft client); }
    public interface EndTick { void onEndTick(Minecraft client); }
    public interface StartWorldTick { void onStartTick(ClientLevel world); }
    public interface EndWorldTick { void onEndTick(ClientLevel world); }
}
