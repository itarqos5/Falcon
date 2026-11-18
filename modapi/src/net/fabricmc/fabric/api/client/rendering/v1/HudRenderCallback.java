package net.fabricmc.fabric.api.client.rendering.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.GuiGraphicsExtractor;

public interface HudRenderCallback {
    Event<HudRenderCallback> EVENT = EventFactory.createArrayBacked(HudRenderCallback.class, callbacks -> (graphics, delta) -> { for (HudRenderCallback c : callbacks) c.onHudRender(graphics, delta); });

    void onHudRender(GuiGraphicsExtractor graphics, DeltaTracker tickCounter);
}
