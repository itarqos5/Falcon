package net.falcon;

import com.mojang.blaze3d.vertex.PoseStack;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElement;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderEvents;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.network.chat.Component;
import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import net.fabricmc.fabric.api.client.command.v2.ClientCommandRegistrationCallback;
import net.fabricmc.fabric.api.client.command.v2.FabricClientCommandSource;
import net.minecraft.client.player.LocalPlayer;

// What the client does to the mods, each in its own call: the host asks for these by name (src/jvm/mod_runtime.cpp).
public final class Events {
    private Events() {}

    private static CommandDispatcher<FabricClientCommandSource> dispatcher;
    private static final FabricClientCommandSource SOURCE = new FabricClientCommandSource() {
        @Override public void sendFeedback(Component message) { Host.chat(message.getString()); }
        @Override public void sendError(Component message) { Host.chat("§c" + message.getString()); }
        @Override public Minecraft getClient() { return Minecraft.getInstance(); }
        @Override public LocalPlayer getPlayer() { return Minecraft.getInstance().player; }
    };

    private static CommandDispatcher<FabricClientCommandSource> commands() {
        if (dispatcher == null) {
            dispatcher = new CommandDispatcher<>();
            ClientCommandRegistrationCallback.EVENT.invoker().register(dispatcher, null);
        }
        return dispatcher;
    }

    // Whether a mod has a client command of this name (the first word of what the player typed, without the slash).
    public static boolean hasCommand(String name) { return commands().literals().contains(name); }

    // Runs a client command; -1 when no command of the mod fits, else what the command answered.
    public static int command(String line) {
        Minecraft.getInstance().refresh();
        try {
            return commands().execute(line, SOURCE);
        } catch (CommandSyntaxException e) {
            Host.chat("§c" + e.getMessage());
            return 0;
        }
    }

    public static void clientStarted() { ClientLifecycleEvents.CLIENT_STARTED.invoker().onClientStarted(Minecraft.getInstance()); }
    public static void clientStopping() { ClientLifecycleEvents.CLIENT_STOPPING.invoker().onClientStopping(Minecraft.getInstance()); }
    public static void tick() { Minecraft mc = Minecraft.getInstance(); mc.tick(); ClientTickEvents.START_CLIENT_TICK.invoker().onStartTick(mc); ClientTickEvents.END_CLIENT_TICK.invoker().onEndTick(mc); }
    public static void join() { Minecraft mc = Minecraft.getInstance(); mc.refresh(); ClientPlayConnectionEvents.JOIN.invoker().onPlayReady(mc.getConnection(), null, mc); }
    public static void disconnect() { Minecraft mc = Minecraft.getInstance(); ClientPlayConnectionEvents.DISCONNECT.invoker().onPlayDisconnect(mc.getConnection(), mc); }

    public static void hud(float partialTick) {
        GuiGraphics graphics = Minecraft.getInstance().gui.extractor();
        graphics.begin();
        DeltaTracker delta = new DeltaTracker(partialTick);
        HudRenderCallback.EVENT.invoker().onHudRender(graphics, delta);
        for (HudElement e : HudElementRegistry.ELEMENTS) e.extractRenderState(graphics, delta);
        graphics.end();
    }

    // The level has been drawn up to the entities: the mods submit what they draw in it.
    public static void levelRender(float partialTick) {
        Minecraft mc = Minecraft.getInstance();
        Camera camera = mc.gameRenderer.getMainCamera();
        PoseStack pose = new PoseStack();
        SubmitNodeCollector collector = new SubmitNodeCollector();
        LevelRenderContext context = new LevelRenderContext() {
            @Override public PoseStack poseStack() { return pose; }
            @Override public SubmitNodeCollector submitNodeCollector() { return collector; }
            @Override public Camera camera() { return camera; }
            @Override public float tickDelta() { return partialTick; }
        };
        LevelRenderEvents.AFTER_ENTITIES.invoker().afterEntities(context);
        LevelRenderEvents.COLLECT_SUBMITS.invoker().collectSubmits(context);
        LevelRenderEvents.AFTER_TRANSLUCENT.invoker().afterTranslucent(context);
        LevelRenderEvents.END_MAIN.invoker().endMain(context);
        collector.flush();
    }

    // A message came in; false when a mod does not let it through.
    public static boolean gameMessage(String text, boolean overlay) {
        Component message = Component.literal(text);
        if (!net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents.ALLOW_GAME.invoker().allowReceiveGameMessage(message, overlay)) return false;
        net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents.GAME.invoker().onReceiveGameMessage(message, overlay);
        return true;
    }

    public static boolean chatMessage(String text, String sender) {
        Component message = Component.literal(text);
        if (!net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents.ALLOW_CHAT.invoker().allowReceiveChatMessage(message, null, null, null, java.time.Instant.now())) return false;
        net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents.CHAT.invoker().onReceiveChatMessage(message, null, null, null, java.time.Instant.now());
        return true;
    }
}
