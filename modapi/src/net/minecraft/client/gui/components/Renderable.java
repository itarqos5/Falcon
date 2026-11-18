package net.minecraft.client.gui.components;

import net.minecraft.client.gui.GuiGraphicsExtractor;

public interface Renderable {
    void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick);
    default void render(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        extractRenderState(graphics, mouseX, mouseY, partialTick);
    }
}
