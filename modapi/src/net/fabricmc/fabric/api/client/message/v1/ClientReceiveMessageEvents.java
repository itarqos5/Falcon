package net.fabricmc.fabric.api.client.message.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.network.chat.Component;

public final class ClientReceiveMessageEvents {
    private ClientReceiveMessageEvents() {}

    public static final Event<Game> GAME = EventFactory.createArrayBacked(Game.class, callbacks -> (message, overlay) -> { for (Game c : callbacks) c.onReceiveGameMessage(message, overlay); });
    public static final Event<Chat> CHAT = EventFactory.createArrayBacked(Chat.class, callbacks -> (message, signedMessage, sender, params, receptionTimestamp) -> { for (Chat c : callbacks) c.onReceiveChatMessage(message, signedMessage, sender, params, receptionTimestamp); });
    public static final Event<AllowGame> ALLOW_GAME = EventFactory.createArrayBacked(AllowGame.class, callbacks -> (message, overlay) -> { for (AllowGame c : callbacks) { if (!c.allowReceiveGameMessage(message, overlay)) return false; } return true; });
    public static final Event<AllowChat> ALLOW_CHAT = EventFactory.createArrayBacked(AllowChat.class, callbacks -> (message, signedMessage, sender, params, receptionTimestamp) -> { for (AllowChat c : callbacks) { if (!c.allowReceiveChatMessage(message, signedMessage, sender, params, receptionTimestamp)) return false; } return true; });
    public static final Event<ModifyGame> MODIFY_GAME = EventFactory.createArrayBacked(ModifyGame.class, callbacks -> (message, overlay) -> { for (ModifyGame c : callbacks) { message = c.modifyReceivedGameMessage(message, overlay); if (message == null) return null; } return message; });

    public interface Game { void onReceiveGameMessage(Component message, boolean overlay); }
    public interface Chat { void onReceiveChatMessage(Component message, Object signedMessage, Object sender, Object params, java.time.Instant receptionTimestamp); }
    public interface AllowGame { boolean allowReceiveGameMessage(Component message, boolean overlay); }
    public interface AllowChat { boolean allowReceiveChatMessage(Component message, Object signedMessage, Object sender, Object params, java.time.Instant receptionTimestamp); }
    public interface ModifyGame { Component modifyReceivedGameMessage(Component message, boolean overlay); }
}
