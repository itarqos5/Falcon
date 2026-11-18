package net.minecraft.client;

import com.mojang.authlib.GameProfile;
import com.mojang.blaze3d.platform.Window;
import net.falcon.Host;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.server.IntegratedServer;
import net.minecraft.resources.Identifier;

// The client as the mods see it. The host fills it in (refresh) before it calls into the mods: the player and the level are there while a world is open.
public class Minecraft {
    public static final Identifier DEFAULT_FONT = Identifier.withDefaultNamespace("default");
    private static final Minecraft INSTANCE = new Minecraft();
    public LocalPlayer player;
    public ClientLevel level;
    public final Options options = new Options();
    public final Font font = new Font();
    public final Gui gui = new Gui(this);
    public final GameRenderer gameRenderer = new GameRenderer();
    public final Window window = new Window();
    public Screen screen;
    public final User user = new User();
    public final KeyboardHandler keyboardHandler = new KeyboardHandler();
    public final MouseHandler mouseHandler = new MouseHandler();
    public net.minecraft.client.multiplayer.MultiPlayerGameMode gameMode = new net.minecraft.client.multiplayer.MultiPlayerGameMode();
    public net.minecraft.world.phys.HitResult hitResult;
    private final TextureManager textureManager = new TextureManager();
    private ClientPacketListener connection;
    private final LocalPlayer localPlayer = new LocalPlayer(this);
    private final ClientLevel clientLevel = new ClientLevel(this);
    private final java.util.List<Runnable> scheduled = new java.util.ArrayList<>();

    public static Minecraft getInstance() { return INSTANCE; }

    public void tick() {
        refresh();
        if (!scheduled.isEmpty()) {
            java.util.List<Runnable> run = new java.util.ArrayList<>(scheduled);
            scheduled.clear();
            for (Runnable r : run) r.run();
        }
    }

    public void refresh() {
        String dimension = Host.dimension();
        if (dimension.isEmpty()) {
            player = null;
            level = null;
            connection = null;
            return;
        }
        localPlayer.refresh();
        clientLevel.refresh(dimension);
        player = localPlayer;
        level = clientLevel;
        if (connection == null) connection = new ClientPacketListener(this);
        hitResult = clientLevel.pickFromCamera(5.0);
    }

    public Window getWindow() { return window; }
    public User getUser() { return user; }
    public GameProfile getGameProfile() { return new GameProfile(user.getProfileId(), user.getName()); }
    public Font font() { return font; }
    public Options options() { return options; }
    public ClientPacketListener getConnection() { return connection; }
    public TextureManager getTextureManager() { return textureManager; }
    public net.minecraft.server.packs.resources.ResourceManager getResourceManager() { return new net.minecraft.server.packs.resources.ResourceManager(); }
    public net.minecraft.client.resources.SkinManager getSkinManager() { return new net.minecraft.client.resources.SkinManager(); }
    public net.minecraft.client.sounds.SoundManager getSoundManager() { return new net.minecraft.client.sounds.SoundManager(); }
    public net.minecraft.server.Services services() { return new net.minecraft.server.Services(); }
    public ServerData getCurrentServer() { return Host.worldKind() == 0 || Host.dimension().isEmpty() ? null : new ServerData(Host.worldName(), Host.worldName(), Host.worldKind() == 2); }
    public boolean isLocalServer() { return Host.worldKind() == 0 && !Host.dimension().isEmpty(); }
    public boolean hasSingleplayerServer() { return isLocalServer(); }
    public boolean isSingleplayer() { return isLocalServer(); }
    public IntegratedServer getSingleplayerServer() { return isLocalServer() ? new IntegratedServer() : null; }
    public boolean isPaused() { return false; }
    public boolean isGameLoadFinished() { return true; }
    public boolean isSameThread() { return true; }
    public boolean isWindowActive() { return true; }
    public int getFps() { return 60; }
    public void execute(Runnable r) { r.run(); }
    public void schedule(Runnable r) { scheduled.add(r); }
    public void setScreen(Screen s) {
        if (screen != null && screen != s) {
            screen.removed();
        }
        screen = s;
        net.falcon.Screens.opened(s);
    }
    public void setScreenAndShow(Screen s) { setScreen(s); }
    public boolean isRunning() { return true; }
    public net.minecraft.client.renderer.entity.EntityRenderDispatcher getEntityRenderDispatcher() { return null; }
    public java.io.File gameDirectory = new java.io.File(Host.gameDir());
}
