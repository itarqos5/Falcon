package net.minecraft.network.chat;

public interface HoverEvent {
    String action();

    record ShowText(Component value) implements HoverEvent { public String action() { return "show_text"; } }
}
