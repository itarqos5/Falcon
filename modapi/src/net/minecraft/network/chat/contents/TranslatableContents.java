package net.minecraft.network.chat.contents;

import java.util.Optional;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.ComponentContents;
import net.minecraft.network.chat.FormattedText;
import net.minecraft.network.chat.Style;

public final class TranslatableContents implements ComponentContents {
    private final String key;
    private final String fallback;
    private final Object[] args;

    public TranslatableContents(String key, String fallback, Object[] args) { this.key = key; this.fallback = fallback; this.args = args; }

    public String getKey() { return key; }
    public String getFallback() { return fallback; }
    public Object[] getArgs() { return args; }

    // The text the key stands for in the player's language, with %s and %1$s filled in.
    public String resolve() {
        String pattern = net.falcon.Host.translate(key);
        if (pattern == null || pattern.equals(key)) pattern = fallback != null ? fallback : key;
        StringBuilder out = new StringBuilder();
        int next = 0;
        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '%' && i + 1 < pattern.length()) {
                char n = pattern.charAt(i + 1);
                if (n == '%') { out.append('%'); i++; continue; }
                if (n == 's') { out.append(arg(next++)); i++; continue; }
                if (n >= '1' && n <= '9' && i + 2 < pattern.length() && pattern.charAt(i + 2) == '$' && i + 3 < pattern.length() && pattern.charAt(i + 3) == 's') { out.append(arg(n - '1')); i += 3; continue; }
            }
            out.append(c);
        }
        return out.toString();
    }

    private String arg(int i) {
        if (args == null || i >= args.length) return "";
        Object a = args[i];
        return a instanceof Component c ? c.getString() : String.valueOf(a);
    }

    @Override public <T> Optional<T> visit(FormattedText.StyledContentConsumer<T> consumer, Style style) { return consumer.accept(style, resolve()); }
    @Override public <T> Optional<T> visit(FormattedText.ContentConsumer<T> consumer) { return consumer.accept(resolve()); }
    @Override public String toString() { return "translation{key='" + key + "'}"; }
}
