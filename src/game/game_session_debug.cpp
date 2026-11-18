#include "game_session_internal.hpp"

#include "falcon/platform/clipboard.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/world/chunk.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

// The F3 chords that are about looking at the world: F3 + B (hitboxes), F3 + G (chunk borders), F3 + A (reload the chunks), F3 + D (clear the chat), F3 + C (copy the location), F3 + Q (the list).
// What the lines are, and their colours and widths, are from the 26.3 classes ChunkBorderRenderer and EntityHitboxDebugRenderer.

namespace falcon {

namespace {

using Line = WorldRenderer::DebugLine;

struct Colour {
    float r, g, b, a;
};
constexpr Colour kRed{1.0f, 0.0f, 0.0f, 1.0f}, kGreen{0.0f, 1.0f, 0.0f, 1.0f}, kBlue{0.0f, 0.0f, 1.0f, 1.0f}, kYellow{1.0f, 1.0f, 0.0f, 1.0f};
constexpr Colour kCellBorder{0.0f, 155.0f / 255.0f, 155.0f / 255.0f, 1.0f};  // ChunkBorderRenderer.CELL_BORDER
constexpr Colour kMajorLines{0.25f, 0.25f, 1.0f, 1.0f};                      // ChunkBorderRenderer.MAJOR_LINES
constexpr Colour kFaintRed{1.0f, 0.0f, 0.0f, 0.5f};                          // the corners of the chunks round the player's

void line(std::vector<Line>& out, double ax, double ay, double az, double bx, double by, double bz, const Colour& c, float width, bool onTop = false) {
    out.push_back({ax, ay, az, bx, by, bz, c.r, c.g, c.b, c.a, width, onTop});
}

// Gizmos.cuboid: the twelve edges of a box.
void cuboid(std::vector<Line>& out, double x0, double y0, double z0, double x1, double y1, double z1, const Colour& c, float width, bool onTop = false) {
    for (const double y : {y0, y1}) {
        line(out, x0, y, z0, x1, y, z0, c, width, onTop);
        line(out, x1, y, z0, x1, y, z1, c, width, onTop);
        line(out, x1, y, z1, x0, y, z1, c, width, onTop);
        line(out, x0, y, z1, x0, y, z0, c, width, onTop);
    }
    for (const double x : {x0, x1}) {
        for (const double z : {z0, z1}) line(out, x, y0, z, x, y1, z, c, width, onTop);
    }
}

// Gizmos.arrow: a line with a head of two short strokes.
void arrow(std::vector<Line>& out, const std::array<double, 3>& from, const std::array<double, 3>& to, const Colour& c) {
    line(out, from[0], from[1], from[2], to[0], to[1], to[2], c, 2.0f);
    const double dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < 1.0e-4) return;
    // Two strokes back from the tip, a quarter of the length, turned out to either side of the arrow's plane.
    const double ux = dx / length, uy = dy / length, uz = dz / length;
    double sx = -uz, sy = 0.0, sz = ux;  // (a side vector: square to the arrow and to up)
    const double sideLength = std::sqrt(sx * sx + sz * sz);
    if (sideLength < 1.0e-4) sx = 1.0, sz = 0.0;
    else sx /= sideLength, sz /= sideLength;
    const double back = std::min(0.25, length * 0.25), spread = back * 0.5;
    for (const double side : {-1.0, 1.0}) {
        line(out, to[0], to[1], to[2], to[0] - ux * back + sx * spread * side, to[1] - uy * back + sy * spread * side, to[2] - uz * back + sz * spread * side, c, 2.0f);
    }
}

// EntityHitboxDebugRenderer.showHitboxes: the box (green), a dot where the entity is, the line at its eyes (red), where it looks (blue, two blocks) and where it is going (yellow).
void hitbox(std::vector<Line>& out, double x, double y, double z, double width, double height, double eyeHeight, const std::array<double, 3>* look, double vx, double vy, double vz) {
    const double h = width * 0.5;
    cuboid(out, x - h, y, z - h, x + h, y + height, z + h, kGreen, 2.0f);
    line(out, x - 0.02, y, z, x + 0.02, y, z, kGreen, 2.0f);  // the dot
    cuboid(out, x - h, y + eyeHeight - 0.01, z - h, x + h, y + eyeHeight + 0.01, z + h, kRed, 2.0f);
    if (look != nullptr) arrow(out, {x, y + eyeHeight, z}, {x + (*look)[0] * 2.0, y + eyeHeight + (*look)[1] * 2.0, z + (*look)[2] * 2.0}, kBlue);
    if (vx != 0.0 || vy != 0.0 || vz != 0.0) arrow(out, {x, y + height * 0.5, z}, {x + vx, y + height * 0.5 + vy, z + vz}, kYellow);
}

}  // namespace

std::vector<WorldRenderer::DebugLine> GameSession::debugGizmos() {
    std::vector<Line> out;
    if (showChunkBorders_) {
        // ChunkBorderRenderer, round the chunk the player stands in (the section is the one their feet are in).
        const double minY = kMinY, maxY = kMaxY;  // (the level's min and max y, plus one)
        const int sectionX = chunkOf(static_cast<int>(std::floor(player_.x))), sectionZ = chunkOf(static_cast<int>(std::floor(player_.z)));
        const int sectionY = sectionOf(static_cast<int>(std::floor(player_.y)));
        const double x0 = sectionX * 16.0, z0 = sectionZ * 16.0;
        for (int i = -16; i <= 32; i += 16) {  // the corners of the chunks round it: tall faint red lines
            for (int j = -16; j <= 32; j += 16) line(out, x0 + i, minY, z0 + j, x0 + i, maxY, z0 + j, kFaintRed, 4.0f);
        }
        for (int i = 2; i < 16; i += 2) {  // the cell lines on the four sides, every second one in another colour
            const Colour& c = i % 4 == 0 ? kCellBorder : kYellow;
            line(out, x0 + i, minY, z0, x0 + i, maxY, z0, c, 1.0f);
            line(out, x0 + i, minY, z0 + 16, x0 + i, maxY, z0 + 16, c, 1.0f);
        }
        for (int i = 2; i < 16; i += 2) {
            const Colour& c = i % 4 == 0 ? kCellBorder : kYellow;
            line(out, x0, minY, z0 + i, x0, maxY, z0 + i, c, 1.0f);
            line(out, x0 + 16, minY, z0 + i, x0 + 16, maxY, z0 + i, c, 1.0f);
        }
        for (int y = kMinY; y <= kMaxY; y += 2) {  // rings round the chunk every two blocks
            const Colour& c = y % 8 == 0 ? kCellBorder : kYellow;
            line(out, x0, y, z0, x0, y, z0 + 16, c, 1.0f);
            line(out, x0, y, z0 + 16, x0 + 16, y, z0 + 16, c, 1.0f);
            line(out, x0 + 16, y, z0 + 16, x0 + 16, y, z0, c, 1.0f);
            line(out, x0 + 16, y, z0, x0, y, z0, c, 1.0f);
        }
        for (int i = 0; i <= 16; i += 16) {  // the chunk's own four corners
            for (int j = 0; j <= 16; j += 16) line(out, x0 + i, minY, z0 + j, x0 + i, maxY, z0 + j, kMajorLines, 4.0f);
        }
        const double y0 = kMinY + sectionY * 16.0;  // the section: a box, seen through the world
        cuboid(out, x0, y0, z0, x0 + 16, y0 + 16, z0 + 16, kMajorLines, 1.0f, true);
        for (int y = kMinY; y <= kMaxY; y += 16) {  // and the layers of sections
            line(out, x0, y, z0, x0, y, z0 + 16, kMajorLines, 4.0f);
            line(out, x0, y, z0 + 16, x0 + 16, y, z0 + 16, kMajorLines, 4.0f);
            line(out, x0 + 16, y, z0 + 16, x0 + 16, y, z0, kMajorLines, 4.0f);
            line(out, x0 + 16, y, z0, x0, y, z0, kMajorLines, 4.0f);
        }
    }
    if (showHitboxes_) {
        const double t = partialTick_;
        const auto lerp = [t](double from, double to) { return from + (to - from) * t; };
        const auto lookOf = [](float yaw, float pitch) { return Look{yaw, pitch}.forward(); };
        if (mobSim_) {
            for (const Mob& m : mobSim_->mobs()) {
                const std::array<double, 3> look = lookOf(m.yaw + m.headYaw, m.headPitch);
                hitbox(out, lerp(m.xo, m.x), lerp(m.yo, m.y), lerp(m.zo, m.z), m.width, m.height, static_cast<double>(m.height) * 0.85, &look, m.vx, m.vy, m.vz);
            }
        }
        for (const ItemEntity& e : itemEntities_.items()) {
            hitbox(out, lerp(e.xo, e.x), lerp(e.yo, e.y), lerp(e.zo, e.z), ItemEntity::kSize, ItemEntity::kSize, ItemEntity::kSize * 0.85, nullptr, e.vx, e.vy, e.vz);
        }
        if (remote_) {
            for (const auto& [id, p] : remote_->players()) {
                const std::array<double, 3> look = lookOf(p.headYaw, p.pitch);
                hitbox(out, lerp(p.xo, p.x), lerp(p.yo, p.y), lerp(p.zo, p.z), 0.6, 1.8, 1.62, &look, p.vx, p.vy, p.vz);
            }
        }
        if (perspective_ != 0 && info_.gameType != 3) {  // the player's own box shows from outside (the camera entity is left out of the first person view)
            const std::array<double, 3> p = player_.position(partialTick_);
            const Aabb box = player_.boundingBox();
            const std::array<double, 3> look = lookOf(player_.yaw, player_.pitch);
            hitbox(out, p[0], p[1], p[2], box.maxX - box.minX, box.maxY - box.minY, static_cast<double>(player_.eyeHeight()), &look, player_.vx, player_.vy, player_.vz);
        }
    }
    return out;
}

void GameSession::debugMessage(const std::string& text) {
    addChatMessage("§e§l" + services_.language.get("debug.prefix") + "§r " + text);
}

// The chords of F3 that do not need anything of the screen: called for every key pressed while F3 is held (a chord used keeps F3 from toggling the debug screen when it is let go).
bool GameSession::debugChord(int key) {
    const auto& lang = services_.language;
    const bool before = debugCombo_;
    debugCombo_ = false;
    if (keyIs("key.debug.showHitboxes", key)) {
        debugCombo_ = true;
        showHitboxes_ = !showHitboxes_;
        debugMessage(lang.get(showHitboxes_ ? "debug.show_hitboxes.on" : "debug.show_hitboxes.off"));
    }
    if (keyIs("key.debug.showChunkBorders", key)) {
        debugCombo_ = true;
        showChunkBorders_ = !showChunkBorders_;
        debugMessage(lang.get(showChunkBorders_ ? "debug.chunk_boundaries.on" : "debug.chunk_boundaries.off"));
    }
    if (keyIs("key.debug.clearChat", key)) {
        debugCombo_ = true;
        chat_.clear();
    }
    if (keyIs("key.debug.reloadChunk", key)) {
        debugCombo_ = true;
        renderer_.reloadChunks();
        debugMessage(lang.get("debug.reload_chunks.message"));
    }
    if (keyIs("key.debug.copyLocation", key)) {
        debugCombo_ = true;
        // The /tp command that puts a player back here (KeyboardHandler.copyRecreateCommand: the dimension, the position to two places, the way they look).
        static constexpr const char* kDimensions[3] = {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"};
        const char* dimension = kDimensions[std::clamp(dimension_, 0, 2)];
        setClipboardText(format("/execute in %s run tp @s %.2f %.2f %.2f %.2f %.2f", dimension, player_.x, player_.y, player_.z, static_cast<double>(player_.yaw), static_cast<double>(player_.pitch)));
        debugMessage(lang.get("debug.copy_location.message"));
    }
    if (key == GLFW_KEY_Q) {  // F3 + Q: the list
        debugCombo_ = true;
        debugMessage(lang.get("debug.help.message"));
        for (const char* help : {"debug.reload_chunks.help", "debug.show_hitboxes.help", "debug.chunk_boundaries.help", "debug.clear_chat.help", "debug.copy_location.help", "debug.advanced_tooltips.help",
                                 "debug.creative_spectator.help", "debug.gamemodes.help", "debug.help.help"}) {
            addChatMessage(lang.get(help));
        }
    }
    const bool used = debugCombo_;
    debugCombo_ = before || used;
    return used;
}

}  // namespace falcon
