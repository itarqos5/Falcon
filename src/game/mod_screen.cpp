#include "falcon/game/mod_screen.hpp"

#include "falcon/gfx/texture_manager.hpp"
#include "falcon/jvm/mod_runtime.hpp"
#include "falcon/util/log.hpp"

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace falcon {

namespace {

void renderDrawCmds(GuiGraphics& g, const std::vector<jvm::DrawCmd>& cmds) {
    const auto round = [](float v) { return static_cast<int>(std::lround(v)); };
    static std::set<std::string> missing;
    int scissors = 0;
    for (const jvm::DrawCmd& c : cmds) {
        switch (c.kind) {
            case jvm::DrawCmd::Kind::Fill:
                g.fill(round(c.x0), round(c.y0), round(c.x1), round(c.y1), c.color);
                break;
            case jvm::DrawCmd::Kind::Gradient: {
                const int y0 = round(c.y0), y1 = round(c.y1);
                for (int y = y0; y < y1 && y - y0 < 800; ++y) {
                    const float t = y1 > y0 ? static_cast<float>(y - y0) / static_cast<float>(y1 - y0) : 0.0f;
                    std::uint32_t argb = 0;
                    for (int shift = 24; shift >= 0; shift -= 8) {
                        const float a = static_cast<float>((c.color >> shift) & 0xFFu), b = static_cast<float>((c.color2 >> shift) & 0xFFu);
                        argb |= static_cast<std::uint32_t>(a + (b - a) * t) << shift;
                    }
                    g.fill(round(c.x0), y, round(c.x1), y + 1, argb);
                }
                break;
            }
            case jvm::DrawCmd::Kind::Text: {
                float x = c.x0;
                if (c.centered) x -= static_cast<float>(g.textWidth(c.text)) / 2.0f;
                g.text(c.text, x, c.y0, c.color, c.shadow);
                break;
            }
            case jvm::DrawCmd::Kind::Blit: {
                if (missing.count(c.text) != 0) break;
                try {
                    if (c.text.rfind("sprite:", 0) == 0) {
                        std::string id = c.text.substr(7);
                        if (id.rfind("minecraft:", 0) == 0) id.erase(0, 10);
                        g.blitSprite(id, round(c.x0), round(c.y0), round(c.x1 - c.x0), round(c.y1 - c.y0));
                    } else {
                        std::string key = c.text;
                        const std::size_t colon = key.find(':');
                        std::string ns = colon == std::string::npos ? "minecraft" : key.substr(0, colon);
                        std::string path = colon == std::string::npos ? key : key.substr(colon + 1);
                        if (path.rfind("textures/", 0) == 0) path.erase(0, 9);
                        key = ns == "minecraft" ? path : ns + ":" + path;
                        const Texture2D& texture = g.textures().get(key);
                        g.blit(texture, round(c.x0), round(c.y0), c.u, c.v, round(c.x1 - c.x0), round(c.y1 - c.y0),
                               static_cast<int>(c.regionW), static_cast<int>(c.regionH), c.texW, c.texH, 0xFFFFFFFFu);
                    }
                } catch (const std::exception&) {
                    missing.insert(c.text);
                }
                break;
            }
            case jvm::DrawCmd::Kind::Item:
                break;
            case jvm::DrawCmd::Kind::ScissorOn:
                g.enableScissor(round(c.x0), round(c.y0), round(c.x1), round(c.y1));
                ++scissors;
                break;
            case jvm::DrawCmd::Kind::ScissorOff:
                if (scissors > 0) {
                    g.disableScissor();
                    --scissors;
                }
                break;
        }
    }
    while (scissors-- > 0) g.disableScissor();
}

}  // namespace

ModScreen::ModScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator), services_(services) {}

ModScreen::~ModScreen() = default;

void ModScreen::init() {
    falcon::log::line("[mod_screen] init (width=%d, height=%d)\n", width_, height_);
    if (services_.mods && services_.mods->hasActiveScreen()) {
        services_.mods->screenResize(width_, height_);
    }
}

void ModScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    if (!services_.mods || !services_.mods->hasActiveScreen()) {
        falcon::log::line("[mod_screen] render: popping (hasActiveScreen=%d)\n", services_.mods ? services_.mods->hasActiveScreen() : 0);
        navigator_.pop();
        return;
    }

    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;

    // Dark backdrop behind the screen
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);

    // Mod screen tick & render
    services_.mods->screenTick();
    const auto cmds = services_.mods->renderScreen(mouseX, mouseY, time.deltaTicks);
    renderDrawCmds(graphics, cmds);

    Screen::render(graphics, mouseX, mouseY, time);
}

bool ModScreen::mouseClicked(double x, double y, int button) {
    lastMouseX_ = x;
    lastMouseY_ = y;
    if (services_.mods && services_.mods->hasActiveScreen()) {
        if (services_.mods->screenMouseClicked(x, y, button)) {
            if (!services_.mods->hasActiveScreen()) navigator_.pop();
            return true;
        }
    }
    return Screen::mouseClicked(x, y, button);
}

void ModScreen::mouseReleased(double x, double y, int button) {
    lastMouseX_ = x;
    lastMouseY_ = y;
    if (services_.mods && services_.mods->hasActiveScreen()) {
        services_.mods->screenMouseReleased(x, y, button);
        if (!services_.mods->hasActiveScreen()) navigator_.pop();
    }
    Screen::mouseReleased(x, y, button);
}

void ModScreen::mouseDragged(double x, double y) {
    const double dx = x - lastMouseX_;
    const double dy = y - lastMouseY_;
    lastMouseX_ = x;
    lastMouseY_ = y;
    if (services_.mods && services_.mods->hasActiveScreen()) {
        services_.mods->screenMouseDragged(x, y, 0, dx, dy);
        if (!services_.mods->hasActiveScreen()) navigator_.pop();
    }
    Screen::mouseDragged(x, y);
}

bool ModScreen::mouseScrolled(double x, double y, double dy) {
    if (services_.mods && services_.mods->hasActiveScreen()) {
        if (services_.mods->screenMouseScrolled(x, y, 0.0, dy)) {
            if (!services_.mods->hasActiveScreen()) navigator_.pop();
            return true;
        }
    }
    return Screen::mouseScrolled(x, y, dy);
}

bool ModScreen::keyPressed(int key, int mods) {
    constexpr int kEscape = 256;
    falcon::log::line("[mod_screen] keyPressed: key=%d mods=%d\n", key, mods);
    if (services_.mods && services_.mods->hasActiveScreen()) {
        if (services_.mods->screenKeyPressed(key, 0, mods)) {
            if (!services_.mods->hasActiveScreen()) {
                falcon::log::line("[mod_screen] screenKeyPressed closed screen -> popping\n");
                navigator_.pop();
            }
            return true;
        }
    }
    if (key == kEscape) {
        onClose();
        return true;
    }
    return Screen::keyPressed(key, mods);
}

void ModScreen::keyReleased(int key, int mods) {
    if (services_.mods && services_.mods->hasActiveScreen()) {
        services_.mods->screenKeyReleased(key, 0, mods);
        if (!services_.mods->hasActiveScreen()) {
            falcon::log::line("[mod_screen] screenKeyReleased closed screen -> popping\n");
            navigator_.pop();
        }
    }
    Screen::keyReleased(key, mods);
}

void ModScreen::charTyped(unsigned int codePoint) {
    if (services_.mods && services_.mods->hasActiveScreen()) {
        if (services_.mods->screenCharTyped(static_cast<int>(codePoint), 0)) {
            if (!services_.mods->hasActiveScreen()) {
                falcon::log::line("[mod_screen] screenCharTyped closed screen -> popping\n");
                navigator_.pop();
            }
            return;
        }
    }
    Screen::charTyped(codePoint);
}

void ModScreen::onClose() {
    falcon::log::line("[mod_screen] onClose\n");
    if (services_.mods && services_.mods->hasActiveScreen()) {
        services_.mods->screenClose();
    }
    navigator_.pop();
}

}  // namespace falcon
