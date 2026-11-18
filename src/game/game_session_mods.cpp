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
    host->setHost(modHostState(modGuiWidth_, modGuiHeight_));
    for (const jvm::ModHost::Said& said : host->tick()) addChatMessage("§7[" + said.mod + "]§r " + said.text);
}

// What the mods may ask the client: where the player and the camera are, how big the window is, which world this is, what block is where, the language and the font.
jvm::HostState GameSession::modHostState(int guiWidth, int guiHeight) const {
    jvm::HostState state;
    state.x = player_.x;
    state.y = player_.y;
    state.z = player_.z;
    state.yaw = player_.yaw;
    state.pitch = player_.pitch;
    state.food = player_.foodLevel;
    const Camera cam = camera();
    state.camX = cam.x;
    state.camY = cam.y;
    state.camZ = cam.z;
    state.camYaw = cam.look.yaw;
    state.camPitch = cam.look.pitch;
    state.camFov = cam.fov;
    state.eyeHeight = cam.y - player_.y;
    state.playerFlags = (player_.sneaking ? 1 : 0) | (player_.onGround ? 2 : 0) | (player_.inWater ? 4 : 0) | (info_.gameType == 1 ? 8 : 0) | (info_.gameType == 3 ? 16 : 0) | (dead_ ? 0 : 32);
    state.guiWidth = guiWidth;
    state.guiHeight = guiHeight;
    state.framebufferWidth = modFramebufferWidth_;
    state.framebufferHeight = modFramebufferHeight_;
    state.guiScale = guiWidth > 0 ? std::max(1, modFramebufferWidth_ / guiWidth) : 2;
    state.gameTime = static_cast<double>(playerAgeTicks_);
    state.dimension = dimension_ == 1 ? "minecraft:the_nether" : dimension_ == 2 ? "minecraft:the_end" : "minecraft:overworld";
    state.worldName = info_.name.empty() ? info_.folder : info_.name;
    state.worldKind = remote_ ? 1 : 0;
    state.playerName = services_.playerName;
    state.playerUuid = services_.playerUuid;
    state.textWidth = [this](const std::string& text) { return services_.font.width(text); };
    state.translate = [this](const std::string& key) { return services_.language.get(key); };
    state.blockAt = [this](int x, int y, int z) {
        const Block b = renderer_.level().block(x, y, z);
        const std::string& name = blockRegistry().type(b).name;
        return name.find(':') == std::string::npos ? "minecraft:" + name : name;
    };
    state.blockFlags = [this](int x, int y, int z) {
        const StateInfo& info = blockRegistry().state(renderer_.level().block(x, y, z));
        return 1 | (info.has(kStateOpaqueCube) ? 2 : 0) | (info.has(kStateFullCollision) ? 4 : 0);
    };
    return state;
}

// What the mods drew in the level, after the entities: their pipelines' vertices, drawn by the world renderer.
void GameSession::renderModLevel(const Camera& camera, int framebufferWidth, int framebufferHeight) {
    jvm::ModHost* host = services_.mods.get();
    if (modsDisabled() || host == nullptr || host->mods().empty()) return;
    modFramebufferWidth_ = framebufferWidth;
    modFramebufferHeight_ = framebufferHeight;
    host->setHost(modHostState(modGuiWidth_, modGuiHeight_));
    renderer_.drawModGeometry(camera, framebufferWidth, framebufferHeight, host->renderLevel(static_cast<float>(partialTick_)));
}

// Fabric's client commands: a line a mod has a command for is answered by the mod, before the server or the game's own commands hear of it.
bool GameSession::runModCommand(const std::string& line) {
    jvm::ModHost* host = services_.mods.get();
    if (modsDisabled() || host == nullptr || host->mods().empty()) return false;
    host->setHost(modHostState(modGuiWidth_, modGuiHeight_));
    if (!host->runCommand(line)) return false;
    for (const jvm::ModHost::Said& said : host->takeSaid()) addChatMessage(said.text);
    return true;
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
    modGuiWidth_ = guiWidth;
    modGuiHeight_ = guiHeight;
    host->setHost(modHostState(guiWidth, guiHeight));

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
