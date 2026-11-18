package net.fabricmc.fabric.api.client.command.v2;

import com.mojang.brigadier.CommandDispatcher;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;

public interface ClientCommandRegistrationCallback {
    Event<ClientCommandRegistrationCallback> EVENT = EventFactory.createArrayBacked(ClientCommandRegistrationCallback.class, callbacks -> (dispatcher, registryAccess) -> { for (ClientCommandRegistrationCallback c : callbacks) c.register(dispatcher, registryAccess); });

    void register(CommandDispatcher<FabricClientCommandSource> dispatcher, Object registryAccess);
}
