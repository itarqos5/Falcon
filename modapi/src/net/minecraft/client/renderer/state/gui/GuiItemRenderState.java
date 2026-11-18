package net.minecraft.client.renderer.state.gui;

import net.minecraft.client.renderer.item.TrackingItemStackRenderState;

public class GuiItemRenderState {
    private final TrackingItemStackRenderState itemStackRenderState = new TrackingItemStackRenderState();

    public TrackingItemStackRenderState itemStackRenderState() {
        return itemStackRenderState;
    }
}
