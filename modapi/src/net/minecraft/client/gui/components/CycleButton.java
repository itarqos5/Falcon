package net.minecraft.client.gui.components;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.function.Function;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.network.chat.Component;

public class CycleButton<T> extends AbstractWidget {
    @FunctionalInterface
    public interface OnValueChange<T> {
        void onValueChange(CycleButton<T> button, T value);
    }

    private final Function<T, Component> valueStringifier;
    private final List<T> values;
    private int index;
    private final OnValueChange<T> onValueChange;
    private final boolean displayOnlyValue;

    protected CycleButton(int x, int y, int width, int height, Component message, Function<T, Component> valueStringifier, List<T> values, int defaultIndex, OnValueChange<T> onValueChange, boolean displayOnlyValue) {
        super(x, y, width, height, message);
        this.valueStringifier = valueStringifier;
        this.values = values;
        this.index = defaultIndex;
        this.onValueChange = onValueChange;
        this.displayOnlyValue = displayOnlyValue;
        updateWidgetText();
    }

    public static <T> Builder<T> builder(Function<T, Component> valueStringifier, T initialValue) {
        return new Builder<>(valueStringifier, initialValue);
    }

    public T getValue() {
        return values.isEmpty() ? null : values.get(index);
    }

    public void setValue(T value) {
        int i = values.indexOf(value);
        if (i >= 0 && i != index) {
            index = i;
            updateWidgetText();
            if (onValueChange != null) {
                onValueChange.onValueChange(this, getValue());
            }
        }
    }

    private void updateWidgetText() {
        T val = getValue();
        Component valComp = val != null ? valueStringifier.apply(val) : Component.empty();
        if (displayOnlyValue) {
            setMessage(valComp);
        } else {
            setMessage(Component.literal(getMessage().getString() + ": ").append(valComp));
        }
    }

    public void cycle() {
        if (values.isEmpty()) return;
        index = (index + 1) % values.size();
        updateWidgetText();
        if (onValueChange != null) {
            onValueChange.onValueChange(this, getValue());
        }
    }

    @Override
    public void onClick(double mouseX, double mouseY) {
        cycle();
    }

    @Override
    public void extractWidgetRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        int bgColor = !active ? 0xFF555555 : (isHovered ? 0xFF888888 : 0xFF333333);
        graphics.fill(x, y, x + width, y + height, bgColor);
        graphics.outline(x, y, width, height, isFocused() ? 0xFFFFFFFF : 0xFF000000);
        net.minecraft.client.Minecraft mc = net.minecraft.client.Minecraft.getInstance();
        if (mc != null && mc.font != null) {
            graphics.centeredText(mc.font, getMessage(), x + width / 2, y + (height - 8) / 2, 0xFFFFFFFF);
        }
    }

    public static class Builder<T> {
        private final Function<T, Component> valueStringifier;
        private final T initialValue;
        private List<T> values = new ArrayList<>();
        private boolean displayOnlyValue;
        private Tooltip tooltip;

        public Builder(Function<T, Component> valueStringifier, T initialValue) {
            this.valueStringifier = valueStringifier;
            this.initialValue = initialValue;
        }

        public Builder<T> withValues(Collection<T> values) {
            this.values = new ArrayList<>(values);
            return this;
        }

        @SafeVarargs
        public final Builder<T> withValues(T... values) {
            this.values = Arrays.asList(values);
            return this;
        }

        public Builder<T> displayOnlyValue() {
            this.displayOnlyValue = true;
            return this;
        }

        public Builder<T> tooltip(Tooltip tooltip) {
            this.tooltip = tooltip;
            return this;
        }

        public CycleButton<T> create(int x, int y, int width, int height, Component message, OnValueChange<T> onValueChange) {
            int defaultIdx = Math.max(0, values.indexOf(initialValue));
            CycleButton<T> btn = new CycleButton<>(x, y, width, height, message, valueStringifier, values, defaultIdx, onValueChange, displayOnlyValue);
            if (tooltip != null) btn.setTooltip(tooltip);
            return btn;
        }
    }
}
