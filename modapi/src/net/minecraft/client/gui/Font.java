package net.minecraft.client.gui;

import java.util.ArrayList;
import java.util.List;
import net.falcon.Host;
import net.minecraft.client.gui.navigation.ScreenRectangle;
import net.minecraft.network.chat.FormattedText;
import net.minecraft.util.FormattedCharSequence;

public class Font {
    public final int lineHeight = 9;

    public int width(String text) { return text == null ? 0 : Host.textWidth(text); }
    public int width(FormattedText text) { return width(text.getString()); }
    public int width(FormattedCharSequence text) { return width(text.text()); }

    public String plainSubstrByWidth(String text, int maxWidth) {
        int w = 0;
        int i = 0;
        for (; i < text.length(); i++) {
            int cw = width(text.substring(i, i + 1));
            if (w + cw > maxWidth) break;
            w += cw;
        }
        return text.substring(0, i);
    }

    public String plainSubstrByWidth(String text, int maxWidth, boolean fromEnd) {
        if (!fromEnd) return plainSubstrByWidth(text, maxWidth);
        int w = 0;
        int i = text.length();
        for (; i > 0; i--) {
            int cw = width(text.substring(i - 1, i));
            if (w + cw > maxWidth) break;
            w += cw;
        }
        return text.substring(i);
    }

    // Wraps a text to a width: one entry per line, broken at spaces.
    public List<FormattedCharSequence> split(FormattedText text, int maxWidth) {
        List<FormattedCharSequence> lines = new ArrayList<>();
        String all = text.getString();
        for (String paragraph : all.split("\n", -1)) {
            StringBuilder line = new StringBuilder();
            for (String word : paragraph.split(" ", -1)) {
                String candidate = line.length() == 0 ? word : line + " " + word;
                if (line.length() > 0 && width(candidate) > maxWidth) {
                    lines.add(FormattedCharSequence.of(line.toString()));
                    line = new StringBuilder(word);
                } else {
                    line = new StringBuilder(candidate);
                }
            }
            lines.add(FormattedCharSequence.of(line.toString()));
        }
        return lines;
    }

    public PreparedText prepareText(String text, float x, float y, int color, boolean shadow, int background) { return new PreparedText(text, x, y, width(text), lineHeight); }

    public static class PreparedText {
        private final String text;
        private final ScreenRectangle bounds;

        PreparedText(String text, float x, float y, int w, int h) { this.text = text; this.bounds = new ScreenRectangle((int) x, (int) y, w, h); }
        public ScreenRectangle bounds() { return bounds; }
    }
}
