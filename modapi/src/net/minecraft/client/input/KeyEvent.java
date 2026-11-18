package net.minecraft.client.input;

public record KeyEvent(int key, int scancode, int modifiers) {
    public boolean hasShiftDown() { return (modifiers & 1) != 0; }
    public boolean hasControlDown() { return (modifiers & 2) != 0; }
    public boolean hasAltDown() { return (modifiers & 4) != 0; }
}
