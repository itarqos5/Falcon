package net.minecraft.client.gui.components.events;

import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;

public interface GuiEventListener {
    default boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) { return false; }
    default boolean mouseReleased(MouseButtonEvent event) { return false; }
    default boolean mouseDragged(MouseButtonEvent event, double dx, double dy) { return false; }
    default boolean mouseScrolled(double mouseX, double mouseY, double horizontal, double vertical) { return false; }
    default boolean keyPressed(KeyEvent event) { return false; }
    default boolean keyReleased(KeyEvent event) { return false; }
    default boolean charTyped(CharacterEvent event) { return false; }
    default boolean isMouseOver(double mouseX, double mouseY) { return false; }
    default void setFocused(boolean focused) {}
    default boolean isFocused() { return false; }
}
