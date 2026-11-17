// GameSession: the fight in the End. The first time the player arrives the Ender Dragon is there, with an end crystal on top of each of the ten pillars; the crystals heal it; when it is
// dead the exit portal opens at the middle of the island, with the dragon egg above it. (EnderDragonFight, in a simpler form: the phases of the dragon are in world/mob.cpp.)
#include "falcon/game/game_session.hpp"
#include "falcon/util/log.hpp"

#include "falcon/render/hand_renderer.hpp"
#include "falcon/world/nether_decoration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace falcon {
namespace {

const Mob* findDragon(const Mobs& mobs) {
    for (const Mob& m : mobs.mobs()) {
        if (std::string(m.species->id) == "ender_dragon") return &m;
    }
    return nullptr;
}

}  // namespace

void GameSession::startDragonFight() {
    if (!mobSim_ || dragonStarted_ || dragonDead_ || dimension_ != 2) return;
    dragonStarted_ = true;
    if (findDragon(*mobSim_) != nullptr) return;  // (it was saved with the End: it is there already)
    if (Mob* dragon = mobSim_->spawn("ender_dragon", 0.5, 96.0, 0.5, true)) dragon->yaw = dragon->yawo = 180.0f;
    for (const EndSpike& spike : endSpikes(info_.seed)) mobSim_->spawn("end_crystal", spike.centerX + 0.5, static_cast<double>(spike.height) + 1.0, spike.centerZ + 0.5, true);
    falcon::log::line("the Ender Dragon and its ten crystals are in the End\n");
}

// EndPodiumFeature: the bedrock dish of the exit portal at the middle of the island, a column of bedrock with torches, and (once the dragon is dead) the portal in the dish.
void GameSession::makeExitPortal(bool active) {
    const auto bedrock = registry_->find("bedrock"), endStone = registry_->find("end_stone"), portal = registry_->find("end_portal"), torch = registry_->find("wall_torch");
    if (!bedrock || !endStone || !portal) return;
    int y0 = 80;
    while (y0 > 30 && renderer_.level().block(0, y0, 0) == Block::Air) --y0;  // the top of the island at its middle
    ++y0;
    portalEdit_ = true;
    for (int dx = -4; dx <= 4; ++dx) {
        for (int dz = -4; dz <= 4; ++dz) {
            const double distance = std::sqrt(static_cast<double>(dx * dx + dz * dz));
            if (distance > 3.5) continue;
            for (int dy = -1; dy <= 6; ++dy) {
                Block put = Block::Air;
                if (dy < 0) put = distance <= 2.5 ? *bedrock : *endStone;
                else if (dy > 0) put = Block::Air;
                else if (distance > 2.5) put = *bedrock;
                else put = active ? *portal : Block::Air;
                setBlock(dx, y0 + dy, dz, put);
            }
        }
    }
    for (int i = 0; i < 3; ++i) setBlock(0, y0 + i, 0, *bedrock);  // the column in the middle
    if (torch) {
        const char* facing[4] = {"north", "south", "west", "east"};
        const int offset[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int i = 0; i < 4; ++i) setBlock(offset[i][0], y0 + 2, offset[i][1], registry_->with(*torch, "facing", facing[i]));
    }
    if (active) {
        if (const auto egg = registry_->find("dragon_egg")) setBlock(0, y0 + 3, 0, *egg);
    }
    portalEdit_ = false;
}

void GameSession::tickDragonFight() {
    if (!mobSim_ || dimension_ != 2 || !dragonStarted_) return;
    if (dragonDead_) return;
    if (findDragon(*mobSim_) == nullptr) {  // it is gone: the fight is won
        dragonDead_ = true;
        makeExitPortal(true);
        playWorldSound("entity.ender_dragon.death", SoundSource::Hostile, 5.0f, 1.0f);
        falcon::log::line("the Ender Dragon is dead: the exit portal is open\n");
    }
}

// The dragon's health bar at the top of the screen (BossHealthOverlay): its name over a strip of 182 pixels, pink, filled as far as its health.
void GameSession::drawBossBar(GuiGraphics& g, int guiWidth, int) {
    if (!mobSim_ || dimension_ != 2) return;
    const Mob* dragon = findDragon(*mobSim_);
    if (dragon == nullptr || dragon->dead()) return;
    const int x = (guiWidth - 182) / 2, y = 12;
    const std::string name = services_.language.get("entity.minecraft.ender_dragon");
    g.text(name, static_cast<float>((guiWidth - g.textWidth(name)) / 2), static_cast<float>(y - 9), 0xFFFFFFFFu, true);
    g.blitSprite("boss_bar/pink_background", x, y, 182, 5);
    const int filled = static_cast<int>(182.0f * std::clamp(dragon->health / dragon->species->health, 0.0f, 1.0f));
    if (filled > 0) {
        g.pushScissor(x, y, x + filled, y + 5);
        g.blitSprite("boss_bar/pink_progress", x, y, 182, 5);
        g.popScissor();
    }
}

}  // namespace falcon
