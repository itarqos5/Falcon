#include "falcon/game/game_session.hpp"
#include "game_session_internal.hpp"

#include "falcon/game/block_placement.hpp"
#include "falcon/world/block_outlines.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/game/menu_background.hpp"
#include "falcon/version.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/recipes.hpp"
#include "falcon/world/water_fog.hpp"
#include "falcon/worldgen/underground.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <thread>

#include <GLFW/glfw3.h>

// GameSession: drawing the world, the HUD, the debug screen and the loading screen.

namespace falcon {

// The mobs of a singleplayer world and what they shoot, drawn from where they were a tick ago to where they are.
void GameSession::renderLocalMobs(const std::array<double, 3>& camPos) {
    if (!mobSim_ || !mobs_ || remote_) return;
    const float partial = static_cast<float>(partialTick_);
    const auto wrap = [](float d) {
        d = std::fmod(d, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    const Matrix4f viewProj = renderer_.viewProjection();
    for (const Mob& m : mobSim_->mobs()) {
        if (!MobRenderer::supports(m.species->id)) continue;
        MobPose pose;
        pose.position = {m.xo + (m.x - m.xo) * partialTick_, m.yo + (m.y - m.yo) * partialTick_, m.zo + (m.z - m.zo) * partialTick_};
        pose.bodyYaw = m.yawo + wrap(m.yaw - m.yawo) * partial;
        pose.headYaw = m.headYaw;
        pose.headPitch = m.headPitch;
        pose.walkPosition = m.limbPos - m.limbSpeed * (1.0f - partial);
        pose.walkSpeed = std::min(1.0f, m.limbSpeedO + (m.limbSpeed - m.limbSpeedO) * partial);
        pose.ageTicks = static_cast<double>(m.age) + partialTick_;
        pose.hurt = m.hurtTime > 0 || m.dead() ? (static_cast<float>(m.hurtTime) - partial) / 10.0f : 0.0f;
        if (m.dead()) pose.hurt = 1.0f;
        pose.death = m.dead() ? static_cast<float>(m.deathTime) + partial + 1.0f : 0.0f;
        pose.swell = m.fuse > 0 ? (static_cast<float>(m.fuse) + (m.fuseDir > 0 ? partial : -partial)) / 28.0f : 0.0f;
        int sky = 15, block = 0;
        renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 1.0)), static_cast<int>(std::floor(pose.position[2])), sky, block);
        pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
        mobs_->render(viewProj, camPos, m.species->id, pose);
    }
    std::vector<ItemEntity> fireballs;  // (a fireball is drawn as the item of the fire charge, a sprite: the game has a model of its own)
    for (const Projectile& p : mobSim_->projectiles()) {
        if (p.kind == Projectile::Kind::Arrow) {
            MobPose pose;
            pose.position = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
            const double horizontal = std::sqrt(p.vx * p.vx + p.vz * p.vz);
            pose.bodyYaw = static_cast<float>(std::atan2(p.vx, p.vz) * 180.0 / 3.14159265358979);
            pose.headPitch = static_cast<float>(std::atan2(p.vy, horizontal) * 180.0 / 3.14159265358979);
            int sky = 15, block = 0;
            renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1])), static_cast<int>(std::floor(pose.position[2])), sky, block);
            pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
            if (p.stuck > 0) pose.bodyYaw = pose.headPitch = 0.0f;  // (an arrow in a block keeps where it was pointing: the velocity is gone, so it stands up; hide that)
            if (p.stuck == 0) mobs_->render(viewProj, camPos, "arrow", pose);
        } else {
            const auto charge = itemRegistry().find("fire_charge");
            if (!charge) continue;
            ItemEntity e;
            e.stack = ItemStack(*charge, 1);
            e.x = p.x;
            e.y = p.y - 0.12;
            e.z = p.z;
            e.xo = p.xo;
            e.yo = p.yo - 0.12;
            e.zo = p.zo;
            e.age = p.age;
            e.pickupDelay = ItemEntity::kNeverPickUp;
            fireballs.push_back(std::move(e));
        }
    }
    if (!fireballs.empty()) hand_.renderGroundItems(renderer_, viewProj, camPos, fireballs, partialTick_);
}

void GameSession::renderWorld(int framebufferWidth, int framebufferHeight) {
    const Camera cam = camera();
    // A torch (or any light-giving block) in the hand lights the blocks round the player while it is held.
    // (the main hand or the offhand: either one lights the way)
    const int carried = ready_ && info_.gameType != 3 ? std::max(carriedLightLevel(inventory_.selectedItem()), carriedLightLevel(inventory_.offhand())) : 0;
    renderer_.setCarriedLight(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)), carried);
    // Under water the sky is gone and the world fades into the biome's water fog (see water_fog.hpp).
    const bool eyesInWater = ready_ && player_.isUnderWater();
    if (eyesInWater) {
        const std::string_view biome = activeGenerator() ? activeGenerator()->biomeNameAt(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)))
                                                  : std::string_view();
        const WaterFog& fog = waterFogs_.forBiome(biome);
        const float vision = waterVision(waterVisionTicks_, true);
        renderer_.setUnderwater(true, underwaterFogColor(fog.color, vision), kWaterFogStart, underwaterFogEnd(fog.endFactor, vision));
    } else {
        renderer_.setUnderwater(false);
    }
    // The sky of the time of day (a tick and the part of it that has passed, while the clock runs).
    const double skyTime = static_cast<double>(dayTime_) + (timeFrozen_ || timePaused_ || !gameRuleOn("advance_time") ? 0.0 : partialTick_ * timeRate_);
    const daycycle::Sky skyNow = dimension_ != 0 ? dimensionSky(cam) : timeline_ ? timeline_->sky(skyTime) : daycycle::sky(skyTime);
    renderer_.setEnvironmentFog(dimension_ == 1 ? 10.0f : 1.0e9f, dimension_ == 1 ? 96.0f : 2.0e9f);
    renderer_.setSky(skyNow);
    setEntitySkyLight(skyNow.skyLightFactor, skyNow.skyLightColor, skyNow.ambient);
    renderer_.render(cam, framebufferWidth, framebufferHeight, renderDistance());
    if (ready_ && target_) renderer_.drawSelection(cam, framebufferWidth, framebufferHeight, target_->x, target_->y, target_->z);
    if (ready_ && digPos_ && destroyStage(digProgress_) >= 0) renderer_.drawBreakOverlay(cam, framebufferWidth, framebufferHeight, (*digPos_)[0], (*digPos_)[1], (*digPos_)[2], destroyStage(digProgress_));
    if (ready_) {
        const std::array<double, 3> camPos = {cam.x, cam.y, cam.z};
        hand_.renderGroundItems(renderer_, renderer_.viewProjection(), camPos, itemEntities_.items(), partialTick_);
        renderRemotePlayers(camPos);
        renderLocalMobs(camPos);
        renderTestMobs(camPos);
        if (physics_) hand_.renderBlockEntities(renderer_, renderer_.viewProjection(), physics_->fallingBlocks(), physics_->primedTnt(), camPos, partialTick_);
        if (perspective_ != 0 && body_ && info_.gameType != 3) {  // F5: the player seen from outside
            const float t = static_cast<float>(partialTick_);
            const auto wrap = [](float degrees) {
                float d = std::fmod(degrees, 360.0f);
                if (d >= 180.0f) d -= 360.0f;
                if (d < -180.0f) d += 360.0f;
                return d;
            };
            BodyPose pose;
            pose.position = player_.position(partialTick_);
            pose.bodyYaw = bodyYawOld_ + wrap(bodyYaw_ - bodyYawOld_) * t;
            pose.headYaw = wrap(player_.yaw - pose.bodyYaw);
            pose.headPitch = player_.pitch;
            pose.walkPosition = walkPosition_ - walkSpeed_ * (1.0f - t);
            pose.walkSpeed = std::min(1.0f, walkSpeedOld_ + (walkSpeed_ - walkSpeedOld_) * t);
            pose.attackProgress = hand_.swingProgress(partialTick_);
            pose.crouching = player_.sneaking;
            pose.ageTicks = playerAgeTicks_ + partialTick_;
            pose.parts = services_.options.modelParts;
            if (const char* skin = std::getenv("FALCON_BODY_SKIN")) pose.skin = std::atoi(skin);  // (tests: one of the default skins, 0-17)
            if (!localSkinHash_.empty()) {  // the account's own skin
                if (const auto texture = skinTextures_.find(localSkinHash_); texture != skinTextures_.end()) {
                    pose.customSkin = texture->second.get();
                    pose.customSlim = skinSlim_[localSkinHash_];
                }
            }
            pose.swimAmount = swimAmountOld_ + (swimAmount_ - swimAmountOld_) * t;
            pose.visuallySwimming = player_.isVisuallySwimming();
            if (player_.fallFlying) poseGliding(pose, static_cast<float>(player_.fallFlyTicks) + t, player_.yaw, player_.pitch, player_.vx, player_.vy, player_.vz);
            pose.cape = capeFor(localCapeHash_);
            if (pose.cape != nullptr) {
                const float flown = static_cast<float>(player_.fallFlyTicks) + t;
                const CapeAngles angles = capeAngles(cloak_, t, player_.xo, player_.x, player_.yo, player_.y, player_.zo, player_.z, bodyYawOld_, bodyYaw_, player_.fallFlying ? std::clamp(flown * flown / 100.0f, 0.0f, 1.0f) : 0.0f);
                pose.capeFlap = angles.flap;
                pose.capeLean = angles.lean;
                pose.capeLean2 = angles.lean2;
            }
            pose.elytra = !inventory_.item(Inventory::kChest).isEmpty() && inventory_.item(Inventory::kChest).type().name == "elytra";
            pose.elytraX = elytraXO_ + (elytraX_ - elytraXO_) * t;
            pose.elytraY = elytraYO_ + (elytraY_ - elytraYO_) * t;
            pose.elytraZ = elytraZO_ + (elytraZ_ - elytraZO_) * t;
            pose.inWater = player_.inWater;
            for (std::size_t i = 0; i < 4; ++i) {  // head, chest, legs, feet
                static constexpr int kSlots[4] = {Inventory::kHead, Inventory::kChest, Inventory::kLegs, Inventory::kFeet};
                const ItemStack& worn = inventory_.item(kSlots[i]);
                if (!worn.isEmpty() && worn.type().armorSlot != EquipSlot::None) pose.armor[i] = worn.type().name;
            }
            int sky = 15, block = 0;
            renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 1.0)), static_cast<int>(std::floor(pose.position[2])), sky, block);
            pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
            pose.rightArmPose = inventory_.selectedItem().isEmpty() ? ArmPose::Empty : ArmPose::Item;
            pose.leftArmPose = inventory_.offhand().isEmpty() ? ArmPose::Empty : ArmPose::Item;
            BodyHands hands;
            body_->render(renderer_.viewProjection(), camPos, pose, &hands);
            if (hands.valid) {
                hand_.renderItemInHand(renderer_, renderer_.viewProjection(), inventory_.selectedItem(), hands.right, false, sky, block);
                hand_.renderItemInHand(renderer_, renderer_.viewProjection(), inventory_.offhand(), hands.left, true, sky, block);
            }
        }
    }
    if (ready_ && perspective_ == 0 && !hudHidden_ && info_.gameType != 3) {  // the hand (F1 hides it, and spectators have none)
        HandFrame frame;
        frame.width = framebufferWidth;
        frame.height = framebufferHeight;
        frame.partialTick = partialTick_;
        frame.yaw = player_.yaw;
        frame.pitch = player_.pitch;
        frame.rightHanded = services_.options.mainHandRight;
        frame.sleeve = services_.options.modelParts[frame.rightHanded ? 3 : 2];  // right_sleeve, left_sleeve
        frame.brightness = static_cast<float>(services_.options.gamma);
        renderer_.lightAt(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)), frame.sky, frame.block);
        frame.block = std::max(frame.block, carried);  // the hand holding the torch is lit by it
        if (cam.bobX != 0.0f || cam.bobY != 0.0f || cam.bobRollDegrees != 0.0f || cam.bobPitchDegrees != 0.0f) {
            frame.bob = bobTransform(cam.bobX, cam.bobY, cam.bobRollDegrees, cam.bobPitchDegrees);
        }
        if (usingItem_ && useDuration_ > 0) {
            frame.useDuration = useDuration_;
            frame.useRemaining = static_cast<float>(useTicksLeft_) - static_cast<float>(partialTick_) + 1.0f;
        }
        hand_.render(renderer_, frame);
    }
    renderer_.endFrame();
}

std::vector<std::string> GameSession::debugLines() const {
    const RenderStats s = renderer_.stats();
    const auto eye = player_.eye();
    std::vector<std::string> lines;
    lines.push_back(format("Falcon " FALCON_VERSION_STRING " (Minecraft 26.3) - %s", info_.name.c_str()));
    lines.push_back(format("%.0f fps (%.1f ms)  cpu %.1f  gpu %.1f", fps_, frameMillis_, cpuMillis_, renderer_.gpuMillis()));
    lines.push_back("");
    const int bx = static_cast<int>(std::floor(player_.x));
    const int by = static_cast<int>(std::floor(player_.y));
    const int bz = static_cast<int>(std::floor(player_.z));
    if (services_.options.reducedDebugInfo) {  // "Reduced Debug Info": no position, direction or biome, only where in its chunk the player is
        lines.push_back(format("Chunk-relative: %d %d %d", bx & 15, by & 15, bz & 15));
    } else {
        lines.push_back(format("XYZ: %.3f / %.3f / %.3f", player_.x, player_.y, player_.z));
        lines.push_back(format("Block: %d %d %d   Chunk: %d %d in %d %d", bx, by, bz, bx & 15, bz & 15, chunkOf(bx), chunkOf(bz)));
        lines.push_back(format("Facing: %s (%.1f / %.1f)", facing(player_.yaw), static_cast<double>(player_.yaw), static_cast<double>(player_.pitch)));
    }
    if (activeGenerator() && !services_.options.reducedDebugInfo) {
        const std::string_view biome = activeGenerator()->biomeNameAt(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(player_.y)), static_cast<int>(std::floor(player_.z)));
        lines.push_back("Biome: " + std::string(biome));
    }
    lines.push_back(format("%s%s%s%s%s%s", player_.flying ? "flying " : "", player_.onGround ? "on ground " : "", player_.inWater ? "in water " : "",
                           player_.isSwimming() ? "swimming " : "", player_.sprinting ? "sprinting " : "", player_.sneaking ? "sneaking" : ""));
    lines.push_back(format("Speed: %.2f blocks/s   fall distance: %.1f", std::hypot(player_.x - player_.xo, player_.z - player_.zo) * 20.0, player_.fallDistance));
    lines.push_back("");
    lines.push_back(format("Render distance: %d  chunks: %d", renderDistance(), s.chunksLoaded));
    lines.push_back(format("Sections drawn: %d / %d", s.sectionsDrawn, s.sectionsLoaded));
    lines.push_back(format("Quads: %zu / %zu", s.quadsDrawn, s.quadsTotal));
    lines.push_back(format("Jobs waiting: %d  uploads: %d", s.jobsWaiting, s.uploadsThisFrame));
    lines.push_back(format("Water: %zu waiting (tick %lld)", fluids_.pending(), fluids_.gameTime()));
    if (mobSim_) lines.push_back(format("Mobs: %zu (%d hostile), shots: %zu", mobSim_->mobs().size(), mobSim_->count(MobKind::Hostile), mobSim_->projectiles().size()));
    lines.push_back(format("Time: %lld (day %lld), moon phase %d", dayTime_ % daycycle::kTicksPerDay, dayTime_ / daycycle::kTicksPerDay, daycycle::moonPhase(dayTime_)));
    lines.push_back(format("RAM: %.0f MB  block data %.1f MB", megabytes(processWorkingSetBytes()), megabytes(s.levelBytes)));
    lines.push_back(format("GPU: meshes %.1f MB, textures %.2f MB", megabytes(s.meshBytes), megabytes(s.textureBytes)));
    if (services_.options.lodsEnabled && renderer_.lodStats().tilesLoaded + renderer_.lodStats().tilesBuilding > 0) {
        const LodStats lod = renderer_.lodStats();
        lines.push_back(format("LODs: %d tiles drawn of %d (%d building), %d levels, %.1f MB", lod.tilesDrawn, lod.tilesLoaded, lod.tilesBuilding, lod.levels, lod.megabytes));
    }
    lines.push_back(format("Chunk cache: %zu chunks, %.1f MB", s.cachedChunks, megabytes(s.cacheBytes)));
    (void)eye;
    return lines;
}

// The graphics lines are read once (the OpenGL strings and the card's own memory never change); the rest is cheap.
void GameSession::refreshSystemLines() {
    if (systemMemory_.total == 0) systemMemory_ = systemMemory();
    if (!gpuLines_.empty()) return;
    const auto glString = [](GLenum name) {
        const char* text = reinterpret_cast<const char*>(gl::GetString(name));
        return std::string(text != nullptr ? text : "");
    };
    const std::string vendor = glString(gl::VENDOR), renderer = glString(gl::RENDERER), version = glString(gl::VERSION);
    gpuLines_.push_back(format("Display: %dx%d (%s)", framebufferWidth_, framebufferHeight_, vendor.c_str()));
    gpuLines_.push_back(renderer);
    if (const auto adapter = findGpuAdapter(renderer); adapter && adapter->dedicatedBytes > 0) gpuLines_.push_back("Video memory: " + formatByteSize(adapter->dedicatedBytes));
    gpuLines_.push_back(version.rfind("Vulkan", 0) == 0 ? version : "OpenGL " + version);
}

// The right-hand side (vanilla's too): the program, the PC, the screen and the card, and the block looked at.
std::vector<std::string> GameSession::debugLinesRight() const {
    std::vector<std::string> lines;
    const SystemInfo& pc = systemInfo();
    lines.push_back(format("C++20 (GCC %d.%d.%d), %d bit", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__, static_cast<int>(sizeof(void*) * 8)));
    if (systemMemory_.total > 0) {
        lines.push_back("System RAM: " + formatByteSize(systemMemory_.total) + " (" + formatByteSize(systemMemory_.available) + " free)");
    }
    lines.push_back("");
    if (!pc.cpu.empty()) lines.push_back(format("CPU: %dx %s", pc.logicalProcessors, pc.cpu.c_str()));
    lines.push_back("OS: " + pc.os);
    lines.push_back("");
    lines.insert(lines.end(), gpuLines_.begin(), gpuLines_.end());
    if (target_ && !services_.options.reducedDebugInfo) {
        const Block b = renderer_.level().block(target_->x, target_->y, target_->z);
        lines.push_back("");
        lines.push_back(format("Targeted Block: %d, %d, %d", target_->x, target_->y, target_->z));
        const std::string state = registry_->describe(b);  // "oak_log[axis=y]": the name, then one line a property
        const std::size_t open = state.find('[');
        lines.push_back("minecraft:" + state.substr(0, open));
        for (std::size_t at = open == std::string::npos ? state.size() : open + 1; at < state.size();) {
            std::size_t end = state.find_first_of(",]", at);
            if (end == std::string::npos) end = state.size();
            std::string property = state.substr(at, end - at);
            if (const std::size_t eq = property.find('='); eq != std::string::npos) property.replace(eq, 1, ": ");
            lines.push_back(property);
            at = end + 1;
        }
    }
    return lines;
}

// Draws the two columns, each line on its translucent strip. A right-hand line that would run into the left-hand one of its row is cut with
// "..." so the two columns never write over each other.
void GameSession::drawDebugScreen(GuiGraphics& g, int guiWidth) {
    refreshSystemLines();
    const std::vector<std::string> left = debugLines();
    std::vector<int> leftWidth(left.size(), 0);
    float y = 2.0f;
    for (std::size_t i = 0; i < left.size(); ++i, y += 10.0f) {
        if (left[i].empty()) continue;
        leftWidth[i] = g.textWidth(left[i]);
        g.fill(1, static_cast<int>(y) - 1, 2 + leftWidth[i] + 1, static_cast<int>(y) + 9, 0x90505050u);
        g.text(left[i], 2.0f, y, 0xFFE0E0E0u);
    }
    const std::vector<std::string> right = debugLinesRight();
    y = 2.0f;
    for (std::size_t i = 0; i < right.size(); ++i, y += 10.0f) {
        std::string line = right[i];
        if (line.empty()) continue;
        const int room = guiWidth - 4 - (i < leftWidth.size() && leftWidth[i] > 0 ? leftWidth[i] + 6 : 0);
        if (g.textWidth(line) > room) {
            while (line.size() > 1 && g.textWidth(line + "...") > room) line.pop_back();
            line += "...";
        }
        const int w = g.textWidth(line);
        const int x = guiWidth - 2 - w;
        g.fill(x - 1, static_cast<int>(y) - 1, x + w + 1, static_cast<int>(y) + 9, 0x90505050u);
        g.text(line, static_cast<float>(x), y, 0xFFE0E0E0u);
    }
}

void GameSession::renderLoading(GuiGraphics& g, int guiWidth, int guiHeight, float deltaTicks) {
    // LevelLoadingScreen, as the game draws it when a world opens (the "other" reason: not a portal): the panorama blurred by the
    // Menu Background Blur option with the menu tint on top; then, centred, a map of the chunks around the spawn with the text
    // above it and a 200 x 2 progress bar under the text.
    renderMenuBackground(g, services_.panorama, services_.options, guiWidth, guiHeight, deltaTicks);

    constexpr int kCell = 2;            // one chunk is a 2 x 2 square
    constexpr int kMapRadius = 5 + 11 + 1;  // max(5, 3) + ChunkLevel.RADIUS_AROUND_FULL_CHUNK (11) + 1: chunks either side of the middle
    constexpr int kMapCells = kMapRadius * 2 + 1;
    constexpr int kMapSize = kMapCells * kCell;
    constexpr int kLineHeight = 9;
    constexpr int kBarWidth = 200;
    const int cx = guiWidth / 2, cy = guiHeight / 2;

    // The chunk map: the colour of each chunk's status. Where nothing has been asked for yet it is black, like an unknown status.
    // (The game's colours for EMPTY, FEATURES, LIGHT and FULL: our chunks are made, lit and drawn in that order.)
    const auto stateColor = [](ChunkLoadState state) -> std::uint32_t {
        switch (state) {
            case ChunkLoadState::Queued: return 0xFF000000u | 5526612u;       // EMPTY
            case ChunkLoadState::Generated: return 0xFF000000u | 2213376u;    // FEATURES
            case ChunkLoadState::Lit: return 0xFF000000u | 16769184u;         // LIGHT
            case ChunkLoadState::Ready: return 0xFF000000u | 16777215u;       // FULL
            case ChunkLoadState::None: break;
        }
        return 0xFF000000u;
    };
    const int centerChunkX = chunkOf(static_cast<int>(std::floor(player_.x)));
    const int centerChunkZ = chunkOf(static_cast<int>(std::floor(player_.z)));
    const int left = cx - kMapSize / 2, top = cy - kMapSize / 2;
    for (int mx = 0; mx < kMapCells; ++mx) {
        for (int mz = 0; mz < kMapCells; ++mz) {
            const ChunkLoadState state = renderer_.chunkLoadState({centerChunkX + mx - kMapRadius, centerChunkZ + mz - kMapRadius});
            const int px = left + mx * kCell, py = top + mz * kCell;
            g.fill(px, py, px + kCell, py + kCell, stateColor(state));
        }
    }

    // The text sits above the map (map radius * 2 + three lines up from the middle); the bar right under the text.
    const std::string text = services_.language.get("multiplayer.downloadingTerrain");
    const int textY = cy - kMapRadius * 2 - kLineHeight * 3;
    g.text(text, static_cast<float>(cx - g.textWidth(text) / 2), static_cast<float>(textY), kWhite);
    const int barX = cx - kBarWidth / 2, barY = textY + kLineHeight + 3;
    g.fill(barX, barY, barX + kBarWidth, barY + 2, 0xFF000000u);
    g.fill(barX, barY, barX + static_cast<int>(std::lround(std::clamp(smoothedProgress_, 0.0f, 1.0f) * static_cast<float>(kBarWidth))), barY + 2, 0xFF00FF00u);
}

void GameSession::renderHud(GuiGraphics& g, int guiWidth, int guiHeight, bool chatFocused) {
    chatFont_ = &g.font();  // so a sent message can be word-wrapped
    if (!ready_) return;  // the loading screen (renderLoading) takes the place of the HUD until the world is ready

    drawFireOverlay(g, guiWidth, guiHeight);
    drawPortalOverlay(g, guiWidth, guiHeight);

    // F1 hides the crosshair and the hotbar (the debug screen stays, which is what people take screenshots with).
    if (!hudHidden_) {
        // Crosshair: 15x15 in the middle (only in first person).
        if (perspective_ == 0) g.blitSprite("hud/crosshair", (guiWidth - 15) / 2, (guiHeight - 15) / 2, 15, 15);
        if (info_.gameType != 3) drawHotbar(g, guiWidth, guiHeight);  // (a spectator has no hotbar: no inventory)
        if (survivalBars()) drawSurvivalBars(g, guiWidth, guiHeight);
        drawNameTags(g, guiWidth, guiHeight, camera());
    }
    drawPlayerList(g, guiWidth);

    if (showDebug_) drawDebugScreen(g, guiWidth);

    // The chat: faded on the HUD, full and scrollable when its screen is open (F1 hides it unless it is open).
    if (!hudHidden_ || chatFocused) chat_.render(g, guiWidth, guiHeight, services_.options, chatFocused);
}

// ScreenEffectRenderer.renderFire: while the player burns, two quads of the fire picture (block/fire_1, its frames played one a tick) stand half a block in front
// of the eyes, 0.3 below the middle and 0.24 either side, turned 10 degrees outwards, so the flames fill the lower part of the screen. They are put on the screen
// here by projecting the corners with the view's field of view; not seen in first person only when the camera is behind the player (F5).
void GameSession::drawFireOverlay(GuiGraphics& g, int guiWidth, int guiHeight) {
    if (burnTicks_ <= 0 || perspective_ != 0 || info_.gameType == 3 || guiHeight <= 0) return;
    const Texture2D& fire = g.textures().get("block/fire_1.png");
    if (fire.width() <= 0 || fire.height() < fire.width()) return;
    const int frames = fire.height() / fire.width();
    const int frame = static_cast<int>(static_cast<long long>(playerAgeTicks_ + partialTick_)) % frames;
    const double tanHalf = std::tan(static_cast<double>(services_.options.fov) * 0.5 * 3.14159265358979 / 180.0);
    const double aspect = static_cast<double>(guiWidth) / static_cast<double>(guiHeight);
    for (int i = 0; i < 2; ++i) {
        const double side = i * 2 - 1, angle = side * 10.0 * 3.14159265358979 / 180.0;
        const auto corner = [&](double x, double y, double& sx, double& sy) {  // (x, y) of the quad, z = -0.5, then turned and moved
            const double vx = x * std::cos(angle) - 0.5 * std::sin(angle) - side * 0.24;
            const double depth = std::max(0.05, x * std::sin(angle) + 0.5 * std::cos(angle));
            sx = (vx / (depth * tanHalf * aspect) * 0.5 + 0.5) * guiWidth;
            sy = (0.5 - (y - 0.3) / (depth * tanHalf) * 0.5) * guiHeight;
        };
        double left = 0, top = 0, right = 0, bottom = 0, unused = 0;
        corner(-0.5, 0.5, left, top);
        corner(0.5, -0.5, right, bottom);
        corner(0.5, 0.5, unused, top);
        if (right <= left || bottom <= top) continue;
        g.blit(fire, static_cast<int>(left), static_cast<int>(top), 0.0f, static_cast<float>(frame * fire.width()), static_cast<int>(right - left), static_cast<int>(bottom - top),
               fire.width(), fire.width(), fire.width(), fire.height(), 0xE5FFFFFFu);
    }
}

// Hud.extractItemHotbar and extractSelectedItemName: the 182x22 bar at the bottom middle with the nine hotbar stacks, the selected
// slot ringed, the offhand stack in its own little frame to the left, and the fading name of the held item above.
void GameSession::drawHotbar(GuiGraphics& g, int guiWidth, int guiHeight) {
    const HotbarLayout layout = computeHotbarLayout(guiWidth, guiHeight, survivalBars());  // (the name sits higher where the bars are)
    g.blitSprite("hud/hotbar", layout.left, layout.top, 182, 22);
    g.blitSprite("hud/hotbar_selection", layout.selectionX(inventory_.selected), layout.selectionY(), 24, 23);
    const ItemStack& offhand = inventory_.offhand();
    if (!offhand.isEmpty()) {
        g.blitSprite("hud/hotbar_offhand_left", layout.offhandSpriteX, layout.offhandSpriteY, 29, 24);
        drawItemStack(g, itemIcons_, offhand, layout.offhandItemX, layout.offhandItemY);
    }
    for (int slot = 0; slot < 9; ++slot) drawItemStack(g, itemIcons_, inventory_.item(slot), layout.slotX(slot), layout.slotY());

    const int alpha = highlight_.alpha();
    if (alpha > 0) {
        const ItemStack& stack = highlight_.stack();
        // An item with a name of its own (an anvil name) shows it in italics.
        const std::string name = (stack.customName.empty() ? "" : "\xC2\xA7o") + itemDisplayName(stack, services_.language);
        const int width = g.textWidth(name);
        const int x = (guiWidth - width) / 2;
        // textWithBackdrop: the box behind the text is the background colour of the "text background" option, which only reaches the HUD when it is set to
        // "Everywhere" (by default it is for the chat alone, and the name stands on the world without a box); it fades with the name.
        if (!services_.options.backgroundForChatOnly) {
            const int background = static_cast<int>(static_cast<float>(services_.options.textBackgroundOpacity) * 255.0f);
            if (background > 0) g.fill(x - 2, layout.nameY - 2, x + width + 2, layout.nameY + Font::kLineHeight + 2, static_cast<std::uint32_t>(background * alpha / 255) << 24);
        }
        g.text(name, static_cast<float>(x), static_cast<float>(layout.nameY), (static_cast<std::uint32_t>(alpha) << 24) | (rarityColor(stack.type().rarity) & 0xFFFFFFu));
    }
}

// Hud.extractPlayerHealth (hearts and food, 9x9 sprites) and ExperienceBar (the 182x5 strip, the level in green with a black outline above it).
void GameSession::drawSurvivalBars(GuiGraphics& g, int guiWidth, int guiHeight) {
    const SurvivalLayout l = computeSurvivalLayout(guiWidth, guiHeight, vitals_.maxHealth);
    const long long tick = static_cast<long long>(playerAgeTicks_);
    const bool hardcore = info_.hardcore;
    const int maxHearts = static_cast<int>(std::ceil(vitals_.maxHealth / 2.0f));
    // The hearts blink after a hit or a heal, and the hearts just lost stay white for a moment (the game's own bookkeeping in extractPlayerHealth).
    const int health = static_cast<int>(std::ceil(vitals_.health));
    const bool blink = healthBlinkTime_ > tick && ((healthBlinkTime_ - tick) / 3) % 2 == 1;
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (health < lastHealthShown_ && invulnerableTime_ > 0) {
        lastHealthMillis_ = now;
        healthBlinkTime_ = tick + 20;
    } else if (health > lastHealthShown_ && invulnerableTime_ > 0) {
        lastHealthMillis_ = now;
        healthBlinkTime_ = tick + 10;
    }
    if (now - lastHealthMillis_ > 1000.0) {
        displayHealth_ = health;
        lastHealthMillis_ = now;
    }
    lastHealthShown_ = health;
    const auto heartSprite = [hardcore](const char* kind, bool blinking) {
        return std::string("hud/heart/") + (hardcore ? "hardcore_" : "") + kind + (blinking ? "_blinking" : "");
    };
    // The armor bar above the hearts (Hud.extractArmor): ten icons, each two points, only while something is worn.
    if (const int armor = static_cast<int>(armorPoints()); armor > 0) {
        const int rows = static_cast<int>(std::ceil(vitals_.maxHealth / 2.0f / 10.0f));
        const int armorY = l.rowY - (rows - 1) * l.rowHeight - 10;
        for (int i = 0; i < 10; ++i) {
            const int x = l.left + i * 8;
            const int k = i * 2 + 1;
            g.blitSprite(k < armor ? "hud/armor_full" : k == armor ? "hud/armor_half" : "hud/armor_empty", x, armorY, 9, 9);
        }
    }
    for (int heart = maxHearts - 1; heart >= 0; --heart) {
        int y = l.heartY(heart);
        if (heartsShake(vitals_.health)) y += tickJitter(tick, heart, 2);
        const int x = l.heartX(heart);
        g.blitSprite(std::string("hud/heart/container") + (hardcore ? "_hardcore" : "") + (blink ? "_blinking" : ""), x, y, 9, 9);
        if (blink && displayHealth_ > heart * 2) g.blitSprite(heartSprite(heart * 2 + 1 == displayHealth_ ? "half" : "full", true), x, y, 9, 9);
        const BarFill fill = heartFill(heart, vitals_.health);
        if (fill != BarFill::Empty) g.blitSprite(heartSprite(fill == BarFill::Full ? "full" : "half", false), x, y, 9, 9);
    }
    for (int icon = 0; icon < 10; ++icon) {
        int y = l.rowY;
        if (foodShakes(vitals_.saturation, vitals_.food, tick)) y += tickJitter(tick, icon, 3) - 1;
        const int x = l.foodX(icon);
        g.blitSprite("hud/food_empty", x, y, 9, 9);
        const BarFill fill = foodFill(icon, vitals_.food);
        if (fill != BarFill::Empty) g.blitSprite(fill == BarFill::Full ? "hud/food_full" : "hud/food_half", x, y, 9, 9);
    }
    // The experience strip: the sprite is 182 wide and only the filled part of it is drawn.
    g.blitSprite("hud/experience_bar_background", l.xpLeft, l.xpTop, 182, 5);
    if (const int filled = experienceFillWidth(vitals_.progress); filled > 0) {
        g.pushScissor(l.xpLeft, l.xpTop, l.xpLeft + filled, l.xpTop + 5);
        g.blitSprite("hud/experience_bar_progress", l.xpLeft, l.xpTop, 182, 5);
        g.popScissor();
    }
    if (vitals_.level > 0) {
        const std::string level = std::to_string(vitals_.level);
        const float x = static_cast<float>((guiWidth - g.textWidth(level)) / 2);
        const float y = static_cast<float>(l.levelBaselineY);
        for (const auto& [dx, dy] : {std::pair{1.0f, 0.0f}, std::pair{-1.0f, 0.0f}, std::pair{0.0f, 1.0f}, std::pair{0.0f, -1.0f}}) g.text(level, x + dx, y + dy, 0xFF000000u, false);
        g.text(level, x, y, 0xFF80FF20u, false);
    }
}

}  // namespace falcon
