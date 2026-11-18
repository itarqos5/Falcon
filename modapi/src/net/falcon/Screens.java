package net.falcon;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;

// Screens a mod opens (Minecraft.setScreen): the host shows them.
public final class Screens {
    private static Screen activeScreen;

    private Screens() {}

    public static void opened(Screen screen) {
        activeScreen = screen;
        if (screen != null) {
            double[] win = Host.window();
            int gw = (int) win[0];
            int gh = (int) win[1];
            if (gw <= 0 || gh <= 0) {
                gw = 427;
                gh = 240;
            }
            screen.init(gw, gh);
            screen.added();
            Host.openScreen(true, screen.getTitle() != null ? screen.getTitle().getString() : "");
        } else {
            Host.openScreen(false, "");
        }
    }

    public static Screen active() {
        return activeScreen;
    }

    public static void render(int mouseX, int mouseY, float partialTick) {
        if (activeScreen != null) {
            GuiGraphics g = Minecraft.getInstance().gui.extractor();
            g.begin();
            activeScreen.render(g, mouseX, mouseY, partialTick);
            g.end();
        }
    }

    public static void resize(int width, int height) {
        if (activeScreen != null) {
            activeScreen.resize(width, height);
        }
    }

    public static void tick() {
        if (activeScreen != null) {
            activeScreen.tick();
        }
    }

    public static boolean mouseClicked(double x, double y, int button, boolean doubleClick) {
        if (activeScreen != null) {
            return activeScreen.mouseClicked(new MouseButtonEvent(x, y, button), doubleClick);
        }
        return false;
    }

    public static boolean mouseReleased(double x, double y, int button) {
        if (activeScreen != null) {
            return activeScreen.mouseReleased(new MouseButtonEvent(x, y, button));
        }
        return false;
    }

    public static boolean mouseDragged(double x, double y, int button, double dx, double dy) {
        if (activeScreen != null) {
            return activeScreen.mouseDragged(new MouseButtonEvent(x, y, button), dx, dy);
        }
        return false;
    }

    public static boolean mouseScrolled(double mouseX, double mouseY, double horizontal, double vertical) {
        if (activeScreen != null) {
            return activeScreen.mouseScrolled(mouseX, mouseY, horizontal, vertical);
        }
        return false;
    }

    public static boolean keyPressed(int key, int scancode, int modifiers) {
        if (activeScreen != null) {
            return activeScreen.keyPressed(new KeyEvent(key, scancode, modifiers));
        }
        return false;
    }

    public static boolean keyReleased(int key, int scancode, int modifiers) {
        if (activeScreen != null) {
            return activeScreen.keyReleased(new KeyEvent(key, scancode, modifiers));
        }
        return false;
    }

    public static boolean charTyped(int codePoint, int modifiers) {
        if (activeScreen != null) {
            return activeScreen.charTyped(new CharacterEvent(codePoint, modifiers));
        }
        return false;
    }

    public static void close() {
        if (activeScreen != null) {
            Screen s = activeScreen;
            activeScreen = null;
            s.removed();
            s.onClose();
        }
        if (activeScreen == null) {
            Host.openScreen(false, "");
        }
    }
}
