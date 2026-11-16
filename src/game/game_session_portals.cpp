// GameSession: the Nether portal (PortalShape, NetherPortalBlock, PortalForcer in a simpler form), the way into the End, and the arrival in a new dimension.
#include "falcon/game/game_session.hpp"

#include "falcon/render/hand_renderer.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/world/mob.hpp"
#include "falcon/world/player.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace falcon {
namespace {

int fl(double v) { return static_cast<int>(std::floor(v)); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The shape of a portal

bool GameSession::isBlockNamed(Block b, const char* name) const {
    if (b == Block::Air) return std::string(name) == "air";
    const StateInfo& info = registry_->state(b);
    return registry_->type(static_cast<int>(info.block)).name == name;
}

// PortalShape.isEmpty: air, fire or portal.
bool GameSession::portalEmpty(Block b) const { return b == Block::Air || registry_->isAir(b) || isBlockNamed(b, "fire") || isBlockNamed(b, "nether_portal"); }

// A frame for a portal in the plane of `axis` (0: x, 1: z) round the empty cell (x, y, z): its lowest left cell, its width and height in cells of the opening; false if there is none.
bool GameSession::findPortalFrame(int x, int y, int z, int axis, PortalFrame& out) const {
    const Level& level = renderer_.level();
    const int dx = axis == 0 ? 1 : 0, dz = axis == 0 ? 0 : 1;
    const auto empty = [&](int px, int py, int pz) { return portalEmpty(level.block(px, py, pz)); };
    const auto frameBlock = [&](int px, int py, int pz) { return isBlockNamed(level.block(px, py, pz), "obsidian"); };
    if (!empty(x, y, z)) return false;
    int y0 = y;
    for (int i = 0; i < 21 && empty(x, y0 - 1, z); ++i) --y0;
    if (!frameBlock(x, y0 - 1, z)) return false;
    // The left end: cells that are empty with frame under them, then a frame block.
    int left = 0;
    while (left < 21 && empty(x - (left + 1) * dx, y0, z - (left + 1) * dz) && frameBlock(x - (left + 1) * dx, y0 - 1, z - (left + 1) * dz)) ++left;
    if (!frameBlock(x - (left + 1) * dx, y0, z - (left + 1) * dz)) return false;
    const int lx = x - left * dx, lz = z - left * dz;
    int width = 1;
    while (width < 22 && empty(lx + width * dx, y0, lz + width * dz) && frameBlock(lx + width * dx, y0 - 1, lz + width * dz)) ++width;
    if (width < 2 || width > 21 || !frameBlock(lx + width * dx, y0, lz + width * dz)) return false;
    // The height: each row is empty between two frame blocks, up to a row that is frame all the way across.
    int height = 0;
    for (; height < 22; ++height) {
        const int ry = y0 + height;
        if (!frameBlock(lx - dx, ry, lz - dz) || !frameBlock(lx + width * dx, ry, lz + width * dz)) return false;
        bool allFrame = true, allEmpty = true;
        for (int i = 0; i < width; ++i) {
            const bool f = frameBlock(lx + i * dx, ry, lz + i * dz), e = empty(lx + i * dx, ry, lz + i * dz);
            allFrame = allFrame && f;
            allEmpty = allEmpty && e;
        }
        if (allFrame) break;
        if (!allEmpty) return false;
    }
    if (height < 3 || height > 21) return false;
    out = {lx, y0, lz, axis, width, height};
    return true;
}

// FireBlock.onPlace -> PortalShape.findEmptyPortalShape(...).createPortalBlocks: fire inside an empty frame fills it with portal.
bool GameSession::tryLightPortal(int x, int y, int z) {
    if (remote_ || dimension_ == 2 || !gameRuleOn("allow_entering_nether_using_portals")) return false;
    const auto portal = registry_->find("nether_portal");
    if (!portal) return false;
    for (int axis = 0; axis < 2; ++axis) {
        PortalFrame f;
        if (!findPortalFrame(x, y, z, axis, f)) continue;
        // (a frame that already holds portal is lit)
        bool anyPortal = false;
        const int dx = axis == 0 ? 1 : 0, dz = axis == 0 ? 0 : 1;
        for (int i = 0; i < f.width && !anyPortal; ++i) {
            for (int j = 0; j < f.height && !anyPortal; ++j) anyPortal = isBlockNamed(renderer_.level().block(f.x + i * dx, f.y + j, f.z + i * dz), "nether_portal");
        }
        if (anyPortal) continue;
        const Block lit = registry_->with(*portal, "axis", axis == 0 ? "x" : "z");
        portalEdit_ = true;
        for (int i = 0; i < f.width; ++i) {
            for (int j = 0; j < f.height; ++j) setBlock(f.x + i * dx, f.y + j, f.z + i * dz, lit);
        }
        portalEdit_ = false;
        const std::array<double, 3> at = {f.x + 0.5 * f.width * dx + 0.5, f.y + 0.5, f.z + 0.5 * f.width * dz + 0.5};
        playWorldSound("block.portal.ambient", SoundSource::Blocks, 1.0f, 1.0f, &at);
        return true;
    }
    return false;
}

// NetherPortalBlock.updateShape: a portal block whose frame was broken (a neighbour in its plane that is neither frame nor portal) turns to air, and its neighbours look again.
void GameSession::checkPortalsAround(int x, int y, int z) {
    if (remote_ || portalEdit_) return;
    std::vector<std::array<int, 3>> pending{{x, y, z}};
    portalEdit_ = true;
    for (int guard = 0; !pending.empty() && guard < 1024; ++guard) {
        const std::array<int, 3> c = pending.back();
        pending.pop_back();
        static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        for (const auto& d : kDir) {
            const int nx = c[0] + d[0], ny = c[1] + d[1], nz = c[2] + d[2];
            const Block here = renderer_.level().block(nx, ny, nz);
            if (!isBlockNamed(here, "nether_portal")) continue;
            const bool alongX = registry_->get(here, "axis") == "x";
            bool ok = true;
            const int steps[4][3] = {{0, -1, 0}, {0, 1, 0}, {alongX ? -1 : 0, 0, alongX ? 0 : -1}, {alongX ? 1 : 0, 0, alongX ? 0 : 1}};
            for (const auto& s : steps) {
                const Block other = renderer_.level().block(nx + s[0], ny + s[1], nz + s[2]);
                ok = ok && (isBlockNamed(other, "obsidian") || (isBlockNamed(other, "nether_portal") && registry_->get(other, "axis") == registry_->get(here, "axis")));
            }
            if (!ok) {
                setBlock(nx, ny, nz, Block::Air);
                pending.push_back({nx, ny, nz});
            }
        }
    }
    portalEdit_ = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Standing in a portal

bool GameSession::playerInsideBlock(const char* name) const {
    const Aabb box = player_.boundingBox();
    for (int bx = fl(box.minX); bx <= fl(box.maxX); ++bx) {
        for (int by = fl(box.minY); by <= fl(box.maxY); ++by) {
            for (int bz = fl(box.minZ); bz <= fl(box.maxZ); ++bz) {
                if (isBlockNamed(renderer_.level().block(bx, by, bz), name)) return true;
            }
        }
    }
    return false;
}

// Entity.handlePortal: four seconds in a portal (at once in creative) and the player is taken through; the purple fades in meanwhile and out again once they step out.
void GameSession::tickPortal() {
    if (remote_ || !ready_ || dead_) return;
    const bool inNether = playerInsideBlock("nether_portal");
    const bool inEnd = playerInsideBlock("end_portal");
    const int delay = creative() ? std::atoi(gameRules_.count("players_nether_portal_creative_delay") ? gameRules_.at("players_nether_portal_creative_delay").c_str() : "0")
                                 : std::atoi(gameRules_.count("players_nether_portal_default_delay") ? gameRules_.at("players_nether_portal_default_delay").c_str() : "80");
    if (inNether || inEnd) {
        if (portalLocked_) {  // (just arrived through a portal: it does not work again until the player has stepped out)
            portalTicks_ = 0;
        } else {
            if (portalTicks_ == 0) playWorldSound("block.portal.trigger", SoundSource::Players, 0.25f, randomFloat() * 0.4f + 0.8f);
            ++portalTicks_;
            if (inEnd || portalTicks_ >= std::max(1, delay)) {
                if (inEnd) {
                    if (dimension_ == 2) {  // the way home: the fight is won, the exit portal
                        if (!dragonDead_) return;
                        returnFromEnd();
                    } else {
                        travelTo(2, 100.5, 49.0, 0.5, false);
                    }
                } else {
                    const int target = dimension_ == 1 ? 0 : 1;
                    const double scale = target == 1 ? 1.0 / 8.0 : 8.0;
                    const double nx = std::clamp(player_.x * scale, -2.9999984e7, 2.9999984e7), nz = std::clamp(player_.z * scale, -2.9999984e7, 2.9999984e7);
                    travelTo(target, nx, std::clamp(player_.y, target == 1 ? 33.0 : static_cast<double>(kMinY) + 5.0, target == 1 ? 118.0 : 300.0), nz, true);
                    playWorldSound("block.portal.travel", SoundSource::Players, 0.25f, randomFloat() * 0.4f + 0.8f);
                }
            }
        }
    } else {
        portalLocked_ = false;
        portalTicks_ = std::max(0, portalTicks_ - 4);
    }
    portalOverlayO_ = portalOverlay_;
    const float target = inNether && !portalLocked_ ? std::min(1.0f, static_cast<float>(portalTicks_) / 80.0f) : 0.0f;
    portalOverlay_ = target > portalOverlay_ ? target : std::max(target, portalOverlay_ - 0.05f);
}

// ---------------------------------------------------------------------------------------------------------------
// Arriving

// The first free place at (or near, within five blocks) where the player was put: out of the rock they might stand in.
void GameSession::liftOutOfBlocks() {
    for (int i = 0; i < 120 && aabbCollides(renderer_.level(), player_.boundingBox()); ++i) player_.setPosition(player_.x, player_.y + 1.0, player_.z);
}

// PortalForcer.findClosestPortal: the portal block nearest to the point, within the radius (the Nether's destination: 16 blocks, the Overworld's 128).
bool GameSession::findClosestPortal(double x, double z, int radius, PortalFrame& out) const {
    const Level& level = renderer_.level();
    const int cx0 = chunkOf(fl(x) - radius), cx1 = chunkOf(fl(x) + radius), cz0 = chunkOf(fl(z) - radius), cz1 = chunkOf(fl(z) + radius);
    const auto portal = registry_->findBlock("nether_portal");
    if (!portal) return false;
    double best = 1.0e18;
    std::array<int, 3> found{0, 0, 0};
    bool any = false;
    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const Chunk* chunk = level.chunk({cx, cz});
            if (chunk == nullptr) continue;
            for (int sy = 0; sy < kSectionCount; ++sy) {
                const SectionPtr& section = chunk->sections[static_cast<std::size_t>(sy)];
                if (!section || section->isUniform()) continue;
                bool maybe = section->packing == Packing::Wide;
                for (const Block b : section->palette) maybe = maybe || registry_->state(b).block == *portal;
                if (!maybe) continue;
                for (int ly = 0; ly < 16; ++ly) {
                    for (int lz = 0; lz < 16; ++lz) {
                        for (int lx = 0; lx < 16; ++lx) {
                            if (registry_->state(section->at(lx, ly, lz)).block != *portal) continue;
                            const double dx = cx * 16 + lx + 0.5 - x, dz = cz * 16 + lz + 0.5 - z;
                            const double d = dx * dx + dz * dz;
                            const int y = kMinY + sy * 16 + ly;
                            if (d < best || (d == best && y < found[1])) {
                                best = d;
                                found = {cx * 16 + lx, y, cz * 16 + lz};
                                any = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (!any || best > static_cast<double>(radius) * radius) return false;
    // The frame it belongs to: from the lowest portal block of its column, along its axis.
    const Block b = level.block(found[0], found[1], found[2]);
    const int axis = registry_->get(b, "axis") == "x" ? 0 : 1;
    int y = found[1];
    while (isBlockNamed(level.block(found[0], y - 1, found[2]), "nether_portal")) --y;
    return findPortalFrame(found[0], y, found[2], axis, out);
}

// PortalForcer.createPortal: a frame of obsidian four wide and five high with a platform of obsidian under and in front of it, at the best open place near the point.
void GameSession::createPortalAt(double x, double yHint, double z, int dimension, PortalFrame& out) {
    const Level& level = renderer_.level();
    const int bx = fl(x), bz = fl(z);
    const int top = dimension == 1 ? 118 : std::min(kMaxY - 6, 300), bottom = dimension == 1 ? 33 : kMinY + 6;
    int bestX = bx, bestY = std::clamp(fl(yHint), bottom, top), bestZ = bz;
    double best = 1.0e18;
    bool found = false;
    const auto openAt = [&](int px, int py, int pz) {  // room for the frame (4 wide along x, 5 high) with ground under it
        for (int a = -1; a <= 2; ++a) {
            for (int h = 0; h <= 3; ++h) {
                const Block b = level.block(px + a, py + h, pz);
                if (mobBlocks(b) || mobHazard(b)) return false;
            }
        }
        for (int a = -1; a <= 2; ++a) {
            if (!mobBlocks(level.block(px + a, py - 1, pz))) return false;
        }
        return true;
    };
    for (int dz = -16; dz <= 16; ++dz) {
        for (int dx = -16; dx <= 16; ++dx) {
            const int px = bx + dx, pz = bz + dz;
            if (!level.isLoaded(px, pz)) continue;
            for (int py = top; py >= bottom; --py) {
                if (!openAt(px, py, pz)) continue;
                const double score = static_cast<double>(dx * dx + dz * dz) * 4.0 + std::abs(py - yHint);
                if (score < best) {
                    best = score;
                    bestX = px;
                    bestY = py;
                    bestZ = pz;
                    found = true;
                }
                break;  // (the highest place in a column that has room: the lower ones are caves)
            }
        }
    }
    if (!found) bestY = std::clamp(fl(yHint), bottom + 1, top - 1);
    const auto obsidian = registry_->find("obsidian");
    const auto portal = registry_->find("nether_portal");
    if (!obsidian || !portal) return;
    portalEdit_ = true;
    const Block lit = registry_->with(*portal, "axis", "x");
    for (int a = -1; a <= 2; ++a) {
        for (int h = -1; h <= 3; ++h) {
            const bool frame = a == -1 || a == 2 || h == -1 || h == 3;
            setBlock(bestX + a, bestY + h, bestZ, frame ? *obsidian : lit);
        }
    }
    // The platform: obsidian in front of the portal and behind it, and the room above it cleared (so the player does not arrive in a wall).
    for (int a = -1; a <= 2; ++a) {
        for (const int side : {-1, 1}) {
            setBlock(bestX + a, bestY - 1, bestZ + side, *obsidian);
            for (int h = 0; h <= 3; ++h) setBlock(bestX + a, bestY + h, bestZ + side, Block::Air);
        }
    }
    portalEdit_ = false;
    out = {bestX, bestY, bestZ, 0, 2, 3};
    out.x = bestX;
}

void GameSession::arriveByPortal(double x, double y, double z) {
    PortalFrame frame;
    const int radius = dimension_ == 1 ? 16 : 128;
    if (!findClosestPortal(x, z, radius, frame)) createPortalAt(x, y, z, dimension_, frame);
    const int dx = frame.axis == 0 ? 1 : 0, dz = frame.axis == 0 ? 0 : 1;
    // In the middle of the opening, at its bottom (PortalShape.getRelativePosition), facing out of it.
    player_.setPosition(frame.x + 0.5 + (frame.width - 1) * 0.5 * dx, static_cast<double>(frame.y), frame.z + 0.5 + (frame.width - 1) * 0.5 * dz);
    if (frame.axis == 0) player_.yaw = player_.yawO = 0.0f;  // (along z)
    else player_.yaw = player_.yawO = 90.0f;
    player_.vx = player_.vy = player_.vz = 0.0;
    liftOutOfBlocks();
    portalLocked_ = true;
}

// ServerLevel.makeObsidianPlatform: an obsidian floor of five by five with room above it, at the End's spawn point, and the player on it facing west.
void GameSession::placeEndPlatform() {
    const auto obsidian = registry_->find("obsidian");
    if (!obsidian) return;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -1; dy <= 2; ++dy) setBlock(100 + dx, 48 + dy, dz, dy < 0 ? *obsidian : Block::Air);
        }
    }
    player_.setPosition(100.5, 49.0, 0.5);
    player_.yaw = player_.yawO = 90.0f;
    player_.pitch = player_.pitchO = 0.0f;
    player_.vx = player_.vy = player_.vz = 0.0;
    portalLocked_ = true;
}

// Out of the End by its exit portal: the Overworld, at the spawn (or where the player's bed is).
void GameSession::returnFromEnd() {
    travelTo(0, spawn_[0], spawn_[1], spawn_[2], false);
}

// The purple that fills the screen in a nether portal (Gui.renderPortalOverlay): the portal's own picture, stretched over the screen, stronger the longer the player stands there.
void GameSession::drawPortalOverlay(GuiGraphics& g, int guiWidth, int guiHeight) {
    const float strength = portalOverlay(static_cast<float>(partialTick_));
    if (strength <= 0.001f || guiHeight <= 0) return;
    const Texture2D& tex = g.textures().get("block/nether_portal.png");
    if (tex.width() <= 0) return;
    float alpha = std::min(1.0f, strength);
    if (alpha < 1.0f) alpha = alpha * alpha * 0.8f + 0.2f;
    const std::uint32_t color = (static_cast<std::uint32_t>(alpha * 255.0f) << 24) | 0x00FFFFFFu;
    g.blit(tex, 0, 0, 0.0f, 0.0f, guiWidth, guiHeight, tex.width(), tex.width(), tex.width(), tex.height(), color);
}


}  // namespace falcon
