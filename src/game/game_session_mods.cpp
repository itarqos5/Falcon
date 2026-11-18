#include "game_session_internal.hpp"

#include "falcon/game/item_render.hpp"
#include "falcon/util/log.hpp"

#include <cmath>
#include <cstdlib>
#include <set>

// The mods of the mods folder, running in the VM (jvm/mod_runtime.hpp). The client starts them before anything else (so that their blocks, items and keys are in the game's tables when the
// world opens); here every game tick the handlers they registered for the client tick are run again, every frame their HUDs are run and what they drew is drawn, and a key a mod asked for
// is passed on. What a handler says to the player goes into the chat. What a mod's Java methods would do to the world is not run.

namespace falcon {
namespace {

bool modsDisabled() {
    static const bool disabled = std::getenv("FALCON_NO_MODS") != nullptr;  // (to play without them)
    return disabled;
}

}  // namespace

void GameSession::tickMods() {
    jvm::ModHost* host = services_.mods.get();
    if (modsDisabled() || remote_ || host == nullptr || host->mods().empty()) return;
    jvm::HostState state;
    state.x = player_.x;
    state.y = player_.y;
    state.z = player_.z;
    state.yaw = player_.yaw;
    state.pitch = player_.pitch;
    state.food = player_.foodLevel;
    state.textWidth = [this](const std::string& text) { return services_.font.width(text); };
    state.translate = [this](const std::string& key) { return services_.language.get(key); };
    host->setHost(state);
    for (const jvm::ModHost::Said& said : host->tick()) addChatMessage("§7[" + said.mod + "]§r " + said.text);
}

void GameSession::modKeyEvent(const KeyId& key, bool pressed) {
    jvm::ModHost* host = services_.mods.get();
    if (modsDisabled() || host == nullptr || host->mods().empty()) return;
    for (std::size_t i = kKeyMappingCount; i < totalKeyMappingCount(); ++i) {
        if (services_.options.keyBindings.key(i) == key) host->keyEvent(keyMappingAt(i).name, pressed);
    }
}

// What the mods' HUDs drew this frame, drawn with the client's own graphics: rectangles, text, pictures and items, in the places the mods gave them.
void GameSession::renderModHud(GuiGraphics& g, int guiWidth, int guiHeight) {
    jvm::ModHost* host = services_.mods.get();
    if (modsDisabled() || host == nullptr || host->mods().empty()) return;
    jvm::HostState state;
    state.x = player_.x;
    state.y = player_.y;
    state.z = player_.z;
    state.yaw = player_.yaw;
    state.pitch = player_.pitch;
    state.food = player_.foodLevel;
    state.guiWidth = guiWidth;
    state.guiHeight = guiHeight;
    state.textWidth = [this](const std::string& text) { return services_.font.width(text); };
    state.translate = [this](const std::string& key) { return services_.language.get(key); };
    host->setHost(state);

    const auto round = [](float v) { return static_cast<int>(std::lround(v)); };
    static std::set<std::string> missing;  // (a picture a mod names that is not there is looked for once)
    int scissors = 0;
    for (const jvm::DrawCmd& c : host->renderHud()) {
        switch (c.kind) {
            case jvm::DrawCmd::Kind::Fill:
                g.fill(round(c.x0), round(c.y0), round(c.x1), round(c.y1), c.color);
                break;
            case jvm::DrawCmd::Kind::Gradient: {  // top to bottom, a row at a time
                const int y0 = round(c.y0), y1 = round(c.y1);
                for (int y = y0; y < y1 && y - y0 < 600; ++y) {
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
                    } else {  // "examplemod:textures/gui/icon.png": the texture manager's key leaves out "textures/"
                        std::string key = c.text;
                        const std::size_t colon = key.find(':');
                        std::string ns = colon == std::string::npos ? "minecraft" : key.substr(0, colon);
                        std::string path = colon == std::string::npos ? key : key.substr(colon + 1);
                        if (path.rfind("textures/", 0) == 0) path.erase(0, 9);
                        key = ns == "minecraft" ? path : ns + ":" + path;
                        const Texture2D& texture = g.textures().get(key);
                        g.blit(texture, round(c.x0), round(c.y0), c.u, c.v, round(c.x1 - c.x0), round(c.y1 - c.y0), static_cast<int>(c.regionW), static_cast<int>(c.regionH), c.texW, c.texH, 0xFFFFFFFFu);
                    }
                } catch (const std::exception&) {
                    missing.insert(c.text);
                }
                break;
            }
            case jvm::DrawCmd::Kind::Item: {
                if (const auto item = itemRegistry().find(c.text)) drawItemStack(g, itemIcons_, ItemStack(*item, 1), round(c.x0), round(c.y0));
                break;
            }
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

}  // namespace falcon
