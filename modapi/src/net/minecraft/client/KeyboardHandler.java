package net.minecraft.client;

public class KeyboardHandler {
    private String clipboard = "";

    public String getClipboard() { return clipboard; }
    public void setClipboard(String text) { clipboard = text == null ? "" : text; }
}
