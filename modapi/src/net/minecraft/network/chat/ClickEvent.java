package net.minecraft.network.chat;

public interface ClickEvent {
    String action();

    record RunCommand(String command) implements ClickEvent { public String action() { return "run_command"; } }
    record SuggestCommand(String command) implements ClickEvent { public String action() { return "suggest_command"; } }
    record OpenUrl(java.net.URI uri) implements ClickEvent { public String action() { return "open_url"; } }
    record CopyToClipboard(String value) implements ClickEvent { public String action() { return "copy_to_clipboard"; } }
}
