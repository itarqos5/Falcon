package net.minecraft.client.gui.screens;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.events.ContainerEventHandler;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.components.Renderable;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

public class Screen implements ContainerEventHandler, Renderable {
    public int width, height;
    protected Minecraft minecraft = Minecraft.getInstance();
    protected Font font = minecraft != null ? minecraft.font : null;
    protected final Component title;

    private final List<GuiEventListener> children = new ArrayList<>();
    private final List<Renderable> renderables = new ArrayList<>();
    private GuiEventListener focused;

    protected Screen(Component title) {
        this.title = title != null ? title : Component.empty();
    }

    public Component getTitle() { return title; }

    public void init(int width, int height) {
        this.width = width;
        this.height = height;
        this.children.clear();
        this.renderables.clear();
        this.focused = null;
        init();
    }

    protected void init() {}

    public void added() {}
    public void removed() {}
    public void tick() {}

    public void resize(int width, int height) {
        init(width, height);
    }

    public void onClose() {
        if (minecraft != null) {
            minecraft.setScreen(null);
        }
    }

    protected <T extends GuiEventListener & Renderable> T addRenderableWidget(T widget) {
        this.renderables.add(widget);
        this.children.add(widget);
        return widget;
    }

    protected <T extends Renderable> T addRenderableOnly(T renderable) {
        this.renderables.add(renderable);
        return renderable;
    }

    protected <T extends GuiEventListener> T addWidget(T listener) {
        this.children.add(listener);
        return listener;
    }

    public void removeWidget(GuiEventListener listener) {
        if (listener instanceof Renderable) {
            this.renderables.remove((Renderable) listener);
        }
        this.children.remove(listener);
    }

    @Override
    public List<? extends GuiEventListener> children() {
        return children;
    }

    @Override
    public GuiEventListener getFocused() {
        return focused;
    }

    @Override
    public void setFocused(GuiEventListener focused) {
        this.focused = focused;
    }

    public void renderBackground(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        graphics.fillGradient(0, 0, width, height, 0xC0101010, 0xD0101010);
    }

    public void render(net.minecraft.client.gui.GuiGraphics graphics, int mouseX, int mouseY, float partialTick) {
        extractRenderState(graphics != null ? graphics.extractor() : null, mouseX, mouseY, partialTick);
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        renderBackground(graphics, mouseX, mouseY, partialTick);
        for (Renderable r : renderables) {
            r.extractRenderState(graphics, mouseX, mouseY, partialTick);
        }
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        for (GuiEventListener child : children) {
            if (child.mouseClicked(event, doubleClick)) {
                setFocused(child);
                return true;
            }
        }
        return false;
    }

    @Override
    public boolean mouseReleased(MouseButtonEvent event) {
        for (GuiEventListener child : children) {
            if (child.mouseReleased(event)) return true;
        }
        return false;
    }

    @Override
    public boolean mouseDragged(MouseButtonEvent event, double dx, double dy) {
        if (focused != null && focused.mouseDragged(event, dx, dy)) return true;
        return false;
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double horizontal, double vertical) {
        for (GuiEventListener child : children) {
            if (child.mouseScrolled(mouseX, mouseY, horizontal, vertical)) return true;
        }
        return false;
    }

    @Override
    public boolean keyPressed(KeyEvent event) {
        if (focused != null && focused.keyPressed(event)) return true;
        if (event.key() == 256) { // GLFW_KEY_ESCAPE
            onClose();
            return true;
        }
        return false;
    }

    @Override
    public boolean keyReleased(KeyEvent event) {
        if (focused != null && focused.keyReleased(event)) return true;
        return false;
    }

    @Override
    public boolean charTyped(CharacterEvent event) {
        if (focused != null && focused.charTyped(event)) return true;
        return false;
    }
}
