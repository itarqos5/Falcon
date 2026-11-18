package net.minecraft.client.gui.components;

import java.util.function.Consumer;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

public class EditBox extends AbstractWidget {
    protected final Font font;
    private String value = "";
    private int maxLength = 32;
    private boolean bordered = true;
    private boolean editable = true;
    private boolean centered = false;
    private int cursorPosition = 0;
    private int highlightPos = 0;
    private int textColor = 0xE0E0E0;
    private int textColorUneditable = 0x707070;
    private Component hint;
    private String suggestion;
    private Consumer<String> responder;

    public EditBox(Font font, int x, int y, int width, int height, Component message) {
        super(x, y, width, height, message);
        this.font = font;
    }

    public String getValue() { return value; }
    public void setValue(String value) {
        if (value == null) value = "";
        if (value.length() > maxLength) {
            value = value.substring(0, maxLength);
        }
        if (!this.value.equals(value)) {
            this.value = value;
            setCursorPosition(this.value.length());
            setHighlightPos(this.cursorPosition);
            onValueChange(value);
        }
    }

    private void onValueChange(String newValue) {
        if (responder != null) {
            responder.accept(newValue);
        }
    }

    public void setResponder(Consumer<String> responder) {
        this.responder = responder;
    }

    public int getCursorPosition() { return cursorPosition; }
    public void setCursorPosition(int pos) {
        this.cursorPosition = Math.max(0, Math.min(pos, value.length()));
    }

    public void moveCursorToEnd(boolean select) {
        setCursorPosition(value.length());
        if (!select) {
            setHighlightPos(cursorPosition);
        }
    }

    public void setHighlightPos(int pos) {
        this.highlightPos = Math.max(0, Math.min(pos, value.length()));
    }

    public int getInnerWidth() {
        return bordered ? width - 8 : width;
    }

    public boolean isBordered() { return bordered; }
    public void setBordered(boolean bordered) { this.bordered = bordered; }

    public void setCentered(boolean centered) { this.centered = centered; }

    public boolean isEditable() { return editable; }
    public void setEditable(boolean editable) { this.editable = editable; }

    public int getMaxLength() { return maxLength; }
    public void setMaxLength(int maxLength) {
        this.maxLength = maxLength;
        if (value.length() > maxLength) {
            setValue(value.substring(0, maxLength));
        }
    }

    public void setHint(Component hint) { this.hint = hint; }
    public void setSuggestion(String suggestion) { this.suggestion = suggestion; }
    public void setTextColor(int color) { this.textColor = color; }
    public void setTextColorUneditable(int color) { this.textColorUneditable = color; }

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        if (!active || !visible) return false;
        boolean wasFocused = isFocused();
        boolean clickedIn = isMouseOver(event.x(), event.y());
        setFocused(clickedIn);
        if (clickedIn && editable) {
            return true;
        }
        return false;
    }

    @Override
    public void extractWidgetRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        if (bordered) {
            int borderColor = isFocused() ? 0xFFFFFFFF : 0xFFA0A0A0;
            graphics.fill(x - 1, y - 1, x + width + 1, y + height + 1, borderColor);
            graphics.fill(x, y, x + width, y + height, 0xFF000000);
        }
        int textX = bordered ? x + 4 : x;
        int textY = bordered ? y + (height - 8) / 2 : y;
        int color = editable ? textColor : textColorUneditable;
        if (value.isEmpty() && hint != null && !isFocused()) {
            graphics.text(font, hint, textX, textY, 0xFF808080);
        } else {
            graphics.text(font, value, textX, textY, color);
        }
    }
}
