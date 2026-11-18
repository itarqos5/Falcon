package net.fabricmc.fabric.api.client.screen.v1;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
public final class ScreenEvents {
    public interface AfterInit { void afterInit(Minecraft client, Screen screen, int scaledWidth, int scaledHeight); }
    public static final Event<AfterInit> AFTER_INIT = null;
}
