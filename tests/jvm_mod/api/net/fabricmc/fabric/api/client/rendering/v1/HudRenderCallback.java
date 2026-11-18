package net.fabricmc.fabric.api.client.rendering.v1;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.GuiGraphics;
public interface HudRenderCallback {
    Event<HudRenderCallback> EVENT = null;
    void onHudRender(GuiGraphics graphics, DeltaTracker delta);
}
