// GameSession: playing on a server. The packets are taken in once a frame and become blocks, chunks, a player that is moved, a chat line; what the player does each tick
// is said back (the position, the keys, the sprint). The other players are drawn with their name over them, and Tab shows the player list.
#include "game_session_internal.hpp"

#include "falcon/game/item_names.hpp"
#include "falcon/render/mob_renderer.hpp"
#include "falcon/render/player_body.hpp"
#include "falcon/util/text_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace falcon {

namespace {

// A line of coloured runs as text with colour codes (the chat takes it in that form): each colour becomes the nearest of the sixteen.
std::string toLegacy(const StyledText& text) {
    static constexpr std::uint32_t kColors[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
                                                  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
    std::string out;
    for (const TextSpan& span : text) {
        out += "\xC2\xA7r";
        if (span.hasColor) {
            int best = 15;
            long long bestDistance = 1LL << 60;
            for (int i = 0; i < 16; ++i) {
                const long long dr = static_cast<long long>((span.rgb >> 16) & 255) - ((kColors[i] >> 16) & 255), dg = static_cast<long long>((span.rgb >> 8) & 255) - ((kColors[i] >> 8) & 255),
                                db = static_cast<long long>(span.rgb & 255) - (kColors[i] & 255);
                const long long d = dr * dr + dg * dg + db * db;
                if (d < bestDistance) {
                    bestDistance = d;
                    best = i;
                }
            }
            out += std::string("\xC2\xA7") + "0123456789abcdef"[best];
        }
        if (span.bold) out += "\xC2\xA7l";
        if (span.italic) out += "\xC2\xA7o";
        if (span.underlined) out += "\xC2\xA7n";
        if (span.strikethrough) out += "\xC2\xA7m";
        out += span.text;
    }
    return out;
}

int styledWidth(const GuiGraphics& g, const StyledText& text) {
    int w = 0;
    for (const TextSpan& span : text) w += g.textWidth(span.text, span.bold);
    return w;
}

void drawStyled(GuiGraphics& g, const StyledText& text, float x, float y, std::uint32_t defaultRgb, bool shadow, std::uint32_t alpha = 0xFF000000u) {
    for (const TextSpan& span : text) {
        g.text(span.text, x, y, alpha | (span.hasColor ? span.rgb : defaultRgb), shadow, span.underlined, span.bold);
        x += static_cast<float>(g.textWidth(span.text, span.bold));
    }
}

// What is thrown and drawn as the item it is (the entity type, the item): an ender pearl, a snowball ...
constexpr const char* kThrownTypes[] = {"ender_pearl", "snowball", "egg", "experience_bottle", "eye_of_ender", "small_fireball", "fireball", "wind_charge"};
constexpr const char* kThrownItems[] = {"ender_pearl", "snowball", "egg", "experience_bottle", "ender_eye", "fire_charge", "fire_charge", "wind_charge"};

// The number of an entity type in the entity type registry (registries.json of the game's data).
int entityTypeNumber(const std::filesystem::path& dataRoot, const char* name) {
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return -1;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return -1;
    const auto registry = doc.find("minecraft:entity_type");
    if (registry == doc.end()) return -1;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return -1;
    const auto entry = entries->find(name);
    if (entry == entries->end()) return -1;
    return entry->value("protocol_id", -1);
}

// The entity types that can be drawn as mobs, by their number in the registry (registries.json), with their names.
std::map<int, std::string> mobTypeNumbers(const std::filesystem::path& dataRoot) {
    std::map<int, std::string> types;
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return types;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return types;
    const auto registry = doc.find("minecraft:entity_type");
    if (registry == doc.end()) return types;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return types;
    std::vector<std::string> names = MobRenderer::supportedTypes();
    names.push_back("painting");
    for (const char* thrown : kThrownTypes) names.emplace_back(thrown);
    for (const std::string& name : names) {
        const auto e = entries->find("minecraft:" + name);
        if (e != entries->end()) types[e->value("protocol_id", -1)] = name;
    }
    return types;
}

// The gravity and the drag per tick of what flies (Projectile: arrows are slowed by 1% and fall by 0.05, thrown things fall by 0.03, a thrown bottle by 0.07), by entity type number.
std::map<int, std::pair<float, float>> projectileTypeNumbers(const std::filesystem::path& dataRoot) {
    std::map<int, std::pair<float, float>> out;
    const struct { const char* name; float gravity; } kFlying[] = {{"arrow", 0.05f}, {"spectral_arrow", 0.05f}, {"ender_pearl", 0.03f}, {"snowball", 0.03f}, {"egg", 0.03f}, {"experience_bottle", 0.07f}};
    for (const auto& f : kFlying) {
        const int id = entityTypeNumber(dataRoot, (std::string("minecraft:") + f.name).c_str());
        if (id >= 0) out[id] = {f.gravity, 0.99f};
    }
    return out;
}

// The names of the menu types by their number (registries.json), without the namespace: "generic_9x3", "furnace", ...
std::vector<std::string> menuTypeNameList(const std::filesystem::path& dataRoot) {
    std::vector<std::string> names;
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return names;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return names;
    const auto registry = doc.find("minecraft:menu");
    if (registry == doc.end()) return names;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return names;
    for (auto it = entries->begin(); it != entries->end(); ++it) {
        const int id = it.value().value("protocol_id", -1);
        if (id < 0 || id > 1000) continue;
        if (names.size() <= static_cast<std::size_t>(id)) names.resize(static_cast<std::size_t>(id) + 1);
        std::string name = it.key();
        if (name.compare(0, 10, "minecraft:") == 0) name.erase(0, 10);
        names[static_cast<std::size_t>(id)] = std::move(name);
    }
    return names;
}

// The names of the sound events by their number (registries.json), without the namespace.
std::vector<std::string> soundEventNames(const std::filesystem::path& dataRoot) {
    std::vector<std::string> names;
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return names;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return names;
    const auto registry = doc.find("minecraft:sound_event");
    if (registry == doc.end()) return names;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return names;
    for (auto it = entries->begin(); it != entries->end(); ++it) {
        const int id = it.value().value("protocol_id", -1);
        if (id < 0 || id > 100000) continue;
        if (names.size() <= static_cast<std::size_t>(id)) names.resize(static_cast<std::size_t>(id) + 1);
        std::string name = it.key();
        if (name.compare(0, 10, "minecraft:") == 0) name.erase(0, 10);
        names[static_cast<std::size_t>(id)] = std::move(name);
    }
    return names;
}

// The numbers of the item components that can be read (the game's data component type registry).
net::ComponentIds componentIds(const std::filesystem::path& dataRoot) {
    net::ComponentIds ids;
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return ids;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return ids;
    const auto registry = doc.find("minecraft:data_component_type");
    if (registry == doc.end()) return ids;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return ids;
    const auto id = [&](const char* name) {
        const auto e = entries->find(name);
        return e == entries->end() ? -1 : e->value("protocol_id", -1);
    };
    ids.damage = id("minecraft:damage");
    ids.repairCost = id("minecraft:repair_cost");
    ids.maxStackSize = id("minecraft:max_stack_size");
    ids.maxDamage = id("minecraft:max_damage");
    ids.unbreakable = id("minecraft:unbreakable");
    ids.customName = id("minecraft:custom_name");
    ids.itemName = id("minecraft:item_name");
    ids.enchantments = id("minecraft:enchantments");
    ids.glintOverride = id("minecraft:enchantment_glint_override");
    ids.tooltipDisplay = id("minecraft:tooltip_display");
    ids.customData = id("minecraft:custom_data");
    return ids;
}

}  // namespace

// Creative mode on a server: a slot that holds something else than the server thinks (the player took an item from the creative inventory, or moved one) is told to it.
void GameSession::syncCreativeSlots() {
    for (int slot = 0; slot < 41; ++slot) {
        const ItemStack& now = inventory_.item(slot);
        ItemStack& known = remoteSlots_[static_cast<std::size_t>(slot)];
        const bool same = now.isEmpty() ? known.isEmpty() : (now.item == known.item && now.count == known.count && now.damage == known.damage);
        if (same) continue;
        if (info_.gameType == 1) {
            remote_->sendCreativeSlot(slot, now);
            known = now;
        } else {
            known = now;  // (in the other modes a click is told to the server when it is made; what differs is corrected by what the server sends)
        }
    }
}

bool GameSession::connectRemote(const std::string& host, int port, const std::string& username, std::string& error) {
    remote_ = std::make_unique<net::MultiplayerClient>(*registry_);
    {  // the account (a server in online mode asks for it); without a token only a server that does not check accounts can be joined
        net::Account account;
        account.accessToken = services_.accessToken;
        account.profileId = services_.playerUuid;
        account.name = username;
        if (!account.accessToken.empty() && account.profileId.empty()) {  // not told who it is: Mojang knows
            std::string id, name;
            if (!net::fetchProfile(account.accessToken, id, name, error)) {
                remote_.reset();
                return false;
            }
            account.profileId = id;
            account.name = name;
        }
        remote_->setAccount(account);
    }
    remote_->setPlayerEntityType(entityTypeNumber(dataRoot_, "minecraft:player"));
    remote_->setItemEntityType(entityTypeNumber(dataRoot_, "minecraft:item"));
    if (mobs_) remote_->setMobTypes(mobTypeNumbers(dataRoot_));
    remote_->setProjectileTypes(projectileTypeNumbers(dataRoot_));
    thrownItems_.clear();
    for (std::size_t i = 0; i < sizeof kThrownTypes / sizeof kThrownTypes[0]; ++i) {
        const int type = entityTypeNumber(dataRoot_, (std::string("minecraft:") + kThrownTypes[i]).c_str());
        if (const auto item = itemRegistry().find(kThrownItems[i]); type >= 0 && item) thrownItems_[type] = *item;
    }
    remote_->setComponentIds(componentIds(dataRoot_));
    remote_->setSoundNames(soundEventNames(dataRoot_));
    menuTypeNames_ = menuTypeNameList(dataRoot_);
    physics_.reset();  // the server runs the world: no falling blocks, fire, redstone or random ticks of ours on its chunks
    redstone_.reset();
    renderer_.setRemote(true);
    net::setComponentTranslator([this](const std::string& key) { return services_.language.get(key); });  // (the server speaks in translation keys)
    net::RemoteHandler& h = remoteHandler_;
    h.self = [this] {
        net::MoveState s;
        s.x = player_.x;
        s.y = player_.y;
        s.z = player_.z;
        s.yaw = player_.yaw;
        s.pitch = player_.pitch;
        s.onGround = player_.onGround;
        s.horizontalCollision = player_.horizontalCollision;
        return s;
    };
    h.chunk = [this](ChunkPos pos, std::shared_ptr<Chunk> chunk) { renderer_.receiveChunk(pos, std::move(chunk)); };
    h.forget = [this](ChunkPos pos) { renderer_.forgetChunk(pos); };
    h.block = [this](int x, int y, int z, Block b) { renderer_.setBlock(x, y, z, b); };
    h.teleport = [this](double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, int relative) {
        // PositionMoveRotation.calculateAbsolute: a part that is relative is added to what the player has now.
        const double nx = (relative & 1) ? player_.x + x : x, ny = (relative & 2) ? player_.y + y : y, nz = (relative & 4) ? player_.z + z : z;
        player_.setPosition(nx, ny, nz);
        player_.xo = nx;
        player_.yo = ny;
        player_.zo = nz;
        if (!(relative & 8)) player_.yaw = player_.yawO = yaw; else player_.yaw += yaw;
        if (!(relative & 16)) player_.pitch = player_.pitchO = pitch; else player_.pitch = std::clamp(player_.pitch + pitch, -90.0f, 90.0f);
        player_.vx = (relative & 32) ? player_.vx + dx : dx;
        player_.vy = (relative & 64) ? player_.vy + dy : dy;
        player_.vz = (relative & 128) ? player_.vz + dz : dz;
        remoteTeleported_ = true;
    };
    h.chat = [this](const StyledText& text, bool overlay) {
        (void)overlay;
        addChatMessage(toLegacy(text));
    };
    h.health = [this](float health, int food, float saturation) {
        if (health < vitals_.health) {  // hurt: the view tilts for half a second (the server does not say from where)
            invulnerableTime_ = 20;
            hurtTime_ = 10;
            hurtDirection_ = 0.0f;
        }
        vitals_.health = health;
        vitals_.food = food;
        vitals_.saturation = saturation;
        if (health <= 0.0f && !dead_) die();
    };
    h.experience = [this](float progress, int level) {
        vitals_.progress = progress;
        vitals_.level = level;
    };
    h.gameMode = [this](int gameType) { setGameType(gameType); };
    h.abilities = [this](bool flying, bool mayFly, bool instabuild, bool invulnerable) {
        (void)instabuild;
        (void)invulnerable;
        player_.canFly = mayFly;
        player_.flying = flying && mayFly;
    };
    h.inventorySlot = [this](int slot, const ItemStack& stack) {
        if (slot < 0 || slot >= 41) return;
        inventory_.item(slot) = stack;
        remoteSlots_[static_cast<std::size_t>(slot)] = stack;  // (the server knows it: nothing to tell back)
    };
    // The player's own menu: a click in survival or adventure is played out here at once and told to the server, which does the same and corrects what differs (creative
    // mode tells the server slot by slot instead); closing the menu is told too.
    inventoryMenu_->onClicked = [this](int slot, int button, ClickType type, const std::vector<std::pair<int, ItemStack>>& changed, const ItemStack& carried) {
        if (info_.gameType == 1 || !remote_ || !remote_->inPlay()) return;
        remote_->sendContainerClick(0, slot, button, static_cast<int>(type), changed, carried);
    };
    inventoryMenu_->onRemoved = [this] {
        if (remote_ && remote_->inPlay()) remote_->sendContainerClose(0);
    };
    // A container that the server opens: if it is one that can be shown, a menu for it (whose clicks are told to the server) and a window; otherwise it is closed again at once.
    h.openContainer = [this](int id, int menuType, const StyledText& title) {
        RemoteMenuKind kind;
        int rows = 0;
        if (menuType < 0 || static_cast<std::size_t>(menuType) >= menuTypeNames_.size() || !remoteMenuKindOf(menuTypeNames_[static_cast<std::size_t>(menuType)], kind, rows)) {
            if (remote_) remote_->sendContainerClose(id);
            addChatMessage("\xC2\xA7" "7Falcon cannot show this kind of window yet.");
            return;
        }
        remoteMenu_ = std::make_unique<RemoteMenu>(inventoryMenu_->player(), kind, rows, id);
        remoteMenu_->onClicked = [this, id](int slot, int button, ClickType type, const std::vector<std::pair<int, ItemStack>>& changed, const ItemStack& carried) {
            if (remote_ && remote_->inPlay()) remote_->sendContainerClick(id, slot, button, static_cast<int>(type), changed, carried);
        };
        remoteMenu_->onRemoved = [this, id] {
            if (remote_ && remote_->inPlay()) remote_->sendContainerClose(id);
            remoteMenuOpen_ = false;
        };
        remoteMenuTitle_ = plainText(title);
        remoteMenuOpen_ = true;
        containerOpenRequested_ = true;
    };
    h.containerContent = [this](int id, const std::vector<ItemStack>& items, const ItemStack& carried) {
        if (!remoteMenuOpen_ || remoteMenu_->containerId() != id) return;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int inventorySlot = remoteMenu_->setFromServer(static_cast<int>(i), items[i]);
            if (inventorySlot >= 0 && inventorySlot < 41) remoteSlots_[static_cast<std::size_t>(inventorySlot)] = items[i];
        }
        remoteMenu_->setCarried(carried);
    };
    h.containerSlot = [this](int id, int slot, const ItemStack& stack) {
        if (!remoteMenuOpen_ || remoteMenu_->containerId() != id) return;
        const int inventorySlot = remoteMenu_->setFromServer(slot, stack);
        if (inventorySlot >= 0 && inventorySlot < 41) remoteSlots_[static_cast<std::size_t>(inventorySlot)] = stack;
    };
    h.containerData = [this](int id, int index, int value) {
        if (remoteMenuOpen_ && remoteMenu_->containerId() == id) remoteMenu_->setData(index, value);
    };
    h.containerClose = [this](int id) {
        if (remoteMenuOpen_ && remoteMenu_->containerId() == id) {
            remoteMenuOpen_ = false;
            containerCloseRequested_ = true;
        }
    };
    h.cursor = [this](const ItemStack& stack) {
        if (remoteMenuOpen_) remoteMenu_->setCarried(stack);
        else inventoryMenu_->setCarried(stack);
    };
    h.selfFlags = [this](int flags) { selfOnFire_ = (flags & 1) != 0; };
    h.sound = [this](const std::string& event, int source, double x, double y, double z, float volume, float pitch) {
        const std::array<double, 3> at = {x, y, z};
        playWorldSound(event, static_cast<SoundSource>(std::clamp(source, 0, kSoundSourceCount - 1)), volume, pitch, &at);
    };
    h.levelEvent = [this](int type, int x, int y, int z, int data) {
        if (type != 2001) return;  // LevelEvent PARTICLES_DESTROY_BLOCK: the break sound of the block (and its particles, which are not drawn yet)
        const Block broken = static_cast<Block>(data);
        if (registry_->isAir(broken)) return;
        const std::array<double, 3> where = {x + 0.5, y + 0.5, z + 0.5};
        const BlockType& type_ = registry_->type(broken);
        playBlockSound(broken, "break", breakOrPlaceMix(type_.soundVolume, type_.soundPitch), SoundSource::Blocks, &where);
    };
    h.heldSlot = [this](int slot) {
        inventory_.setSelected(slot);
        lastHeldSent_ = inventory_.selected;
    };
    h.disconnect = [this](const std::string& why) {
        std::printf("[falcon] left the server: %s\n", why.c_str());
        addChatMessage("\xC2\xA7" "cDisconnected: " + why);
        remoteLost_ = why;
    };
    if (!remote_->start(host, port, username, error)) {
        remote_.reset();
        renderer_.setRemote(false);
        return false;
    }
    std::printf("[falcon] connecting to %s:%d as %s\n", host.c_str(), port, username.c_str());
    return true;
}

// Skins that finished downloading are put on the graphics card (here, where there is a context), and the account's own skin goes on the hand too.
// What a click hits (MultiPlayerGameMode.attack, Minecraft.pick): the nearest of the players and mobs whose box the line from the eye meets within reach. A player's box is 0.6 wide
// and 1.8 high (1.5 sneaking), a mob's is its type's; arrows in the air are not hit.
int GameSession::entityUnderCrosshair(const std::array<double, 3>& eye, const std::array<double, 3>& look, double maxDistance) const {
    if (!remote_) return -1;
    int found = -1;
    double best = maxDistance;
    for (const auto& [id, p] : remote_->players()) {
        double halfWidth = 0.0, height = 0.0;
        if (remote_->isPlayerEntity(p)) {
            halfWidth = 0.3;
            height = p.sneaking ? 1.5 : 1.8;
        } else {
            const std::string* type = remote_->mobName(p.type);
            float w = 0.0f, h = 0.0f;
            if (type == nullptr || !MobRenderer::attackable(*type) || !MobRenderer::sizeOf(*type, w, h)) continue;
            halfWidth = static_cast<double>(w) / 2.0;
            height = static_cast<double>(h);
        }
        const double px = p.xo + (p.x - p.xo) * partialTick_, py = p.yo + (p.y - p.yo) * partialTick_, pz = p.zo + (p.z - p.zo) * partialTick_;
        const double lo[3] = {px - halfWidth, py, pz - halfWidth}, hi[3] = {px + halfWidth, py + height, pz + halfWidth};
        double tNear = 0.0, tFar = best;
        bool hit = true;
        for (int axis = 0; axis < 3 && hit; ++axis) {  // the slabs of the box
            if (std::abs(look[static_cast<std::size_t>(axis)]) < 1.0e-9) {
                if (eye[static_cast<std::size_t>(axis)] < lo[axis] || eye[static_cast<std::size_t>(axis)] > hi[axis]) hit = false;
                continue;
            }
            double t0 = (lo[axis] - eye[static_cast<std::size_t>(axis)]) / look[static_cast<std::size_t>(axis)];
            double t1 = (hi[axis] - eye[static_cast<std::size_t>(axis)]) / look[static_cast<std::size_t>(axis)];
            if (t0 > t1) std::swap(t0, t1);
            tNear = std::max(tNear, t0);
            tFar = std::min(tFar, t1);
            if (tNear > tFar) hit = false;
        }
        if (hit && tNear < best) {
            best = tNear;
            found = id;
        }
    }
    return found;
}

// FALCON_TEST_MOBS=zombie,pig,sheep: a row of those mobs five blocks in front of the player, walking on the spot, for a look at the models and pictures (a test aid).
void GameSession::renderTestMobs(const std::array<double, 3>& camPos) {
    static const char* const list = std::getenv("FALCON_TEST_MOBS");
    if (list == nullptr || !mobs_ || !ready_) return;
    std::string names = list;
    double x = 0.0;
    const double yaw = static_cast<double>(player_.yaw) * 3.14159265358979 / 180.0;
    const double forwardX = -std::sin(yaw), forwardZ = std::cos(yaw), rightX = std::cos(yaw), rightZ = std::sin(yaw);
    std::size_t at = 0;
    int count = 0;
    for (const char c : names) count += c == ',' ? 1 : 0;
    x = -1.6 * count / 2.0;
    while (at <= names.size()) {
        std::size_t end = names.find(',', at);
        if (end == std::string::npos) end = names.size();
        const std::string type = names.substr(at, end - at);
        at = end + 1;
        if (type.empty()) continue;
        if (type.compare(0, 9, "painting:") == 0) {  // painting:kebab:1:1 (the picture, its width and height in blocks)
            const std::size_t c1 = type.find(':', 9), c2 = c1 == std::string::npos ? c1 : type.find(':', c1 + 1);
            if (c2 != std::string::npos) {
                const std::array<double, 3> middle = {player_.x + forwardX * 4.5 + rightX * x, player_.y + 1.0, player_.z + forwardZ * 4.5 + rightZ * x};
                mobs_->renderPainting(renderer_.viewProjection(), camPos, type.substr(9, c1 - 9), std::atoi(type.c_str() + c1 + 1), std::atoi(type.c_str() + c2 + 1), middle, player_.yaw + 180.0f, {1.0f, 1.0f, 1.0f});
            }
            x += 1.6;
            continue;
        }
        MobPose mob;
        mob.position = {player_.x + forwardX * 4.5 + rightX * x, player_.y, player_.z + forwardZ * 4.5 + rightZ * x};
        mob.bodyYaw = player_.yaw + 180.0f - 25.0f;  // (facing the player, turned a little)
        mob.walkPosition = static_cast<float>(playerAgeTicks_ + partialTick_) * 0.6f;
        mob.walkSpeed = 0.8f;
        mob.ageTicks = playerAgeTicks_ + partialTick_;
        mobs_->render(renderer_.viewProjection(), camPos, type, mob);
        x += 1.6;
    }
}

// FALCON_TEST_CONTAINER=generic_9x3 (or furnace, crafting, hopper, generic_3x3, shulker_box, ...): once the world is ready a container of that kind opens with a few stacks in it
// and its fire burning, as if a server had opened it (a test aid, to see the windows).
void GameSession::openTestContainer() {
    static const char* const kind = std::getenv("FALCON_TEST_CONTAINER");
    static bool done = false;
    if (kind == nullptr || done || !ready_ || remote_) return;
    done = true;
    RemoteMenuKind menuKind;
    int rows = 0;
    if (!remoteMenuKindOf(kind, menuKind, rows)) return;
    remoteMenu_ = std::make_unique<RemoteMenu>(inventoryMenu_->player(), menuKind, rows, 1);
    const char* const things[] = {"stone", "oak_planks", "diamond", "iron_ingot", "coal", "apple", "cobblestone", "stick", "torch"};
    for (int i = 0; i < remoteMenu_->containerSlots() && i < 9; ++i) {
        if (const auto item = itemRegistry().find(things[i])) remoteMenu_->setFromServer(i, ItemStack(*item, 8 + i * 7));
    }
    remoteMenu_->setData(0, 120);
    remoteMenu_->setData(1, 200);
    remoteMenu_->setData(2, 80);
    remoteMenu_->setData(3, 200);
    remoteMenuTitle_ = kind;
    remoteMenuOpen_ = true;
    containerOpenRequested_ = true;
}

void GameSession::pumpSkins() {
    std::vector<net::SkinFetcher::Done> done;
    if (remote_) done = remote_->takeSkins();
    if (localSkins_) {
        std::vector<net::SkinFetcher::Done> mine = localSkins_->takeFinished();
        for (net::SkinFetcher::Done& d : mine) done.push_back(std::move(d));
    }
    for (const net::SkinFetcher::Done& d : done) {
        skinTextures_[d.hash] = std::make_unique<Texture2D>(d.image, FilterMode::Nearest);
        skinSlim_[d.hash] = d.slim;
        if (localSkins_) {
            if (const auto own = localSkins_->profileSkin(services_.playerUuid); own && own->hash == d.hash) {
                localSkinHash_ = d.hash;
                if (!d.slim) hand_.setSkin(d.image);
            }
        }
    }
}

void GameSession::pumpRemote() {
    if (!remote_) return;
    remote_->pump(remoteHandler_);
    {  // the items lying about, as the server has them, in the place where the world's own items are drawn from
        std::vector<ItemEntity>& items = itemEntities_.items();
        items.clear();
        for (const auto& [id, item] : remote_->items()) {
            if (item.stack.isEmpty()) continue;
            ItemEntity e;
            e.stack = item.stack;
            e.x = item.x;
            e.y = item.y;
            e.z = item.z;
            e.xo = item.xo;
            e.yo = item.yo;
            e.zo = item.zo;
            e.age = item.age;
            e.id = id;
            e.onGround = item.onGround;
            e.pickupDelay = 0;
            e.bobOffset = static_cast<float>(id % 628) * 0.01f;
            items.push_back(e);
        }
        for (const auto& [id, thing] : remote_->players()) {  // an ender pearl, a snowball ... in the air: the item it is
            const auto item = thrownItems_.find(thing.type);
            if (item == thrownItems_.end()) continue;
            ItemEntity e;
            e.stack = ItemStack(item->second, 1);
            e.x = thing.x;
            e.y = thing.y;
            e.z = thing.z;
            e.xo = thing.xo;
            e.yo = thing.yo;
            e.zo = thing.zo;
            e.age = 0;
            e.id = id;
            e.pickupDelay = 0;
            items.push_back(e);
        }
    }
    if (remote_->closed() && remoteLost_.empty()) remoteLost_ = remote_->closeReason();
    // The world is on the screen: the server may now take the player's movement (ServerboundPlayerLoadedPacket).
    if (ready_ && !loadedSent_ && remote_->inPlay() && remoteTeleported_) {
        remote_->sendPlayerLoaded();
        loadedSent_ = true;
    }
}

// What LocalPlayer.tick says to the server: the keys when they changed, the sprint, the position, and that the tick is over.
void GameSession::sendRemoteTick(const PlayerInput& input) {
    if (!remote_ || !remote_->inPlay() || !remoteTeleported_) return;
    const int flags = (input.forward ? 1 : 0) | (input.backward ? 2 : 0) | (input.left ? 4 : 0) | (input.right ? 8 : 0) | (input.jump ? 16 : 0) | (input.shift ? 32 : 0) | (input.sprint ? 64 : 0);
    if (flags != lastInputFlags_) {
        remote_->sendInput(flags);
        lastInputFlags_ = flags;
    }
    if (player_.sprinting != lastSprintSent_) {
        remote_->sendSprint(player_.sprinting);
        lastSprintSent_ = player_.sprinting;
    }
    if (inventory_.selected != lastHeldSent_) {  // (LocalPlayer: the server is told when the hotbar slot in the hand changes)
        remote_->sendHeldSlot(inventory_.selected);
        lastHeldSent_ = inventory_.selected;
    }
    syncCreativeSlots();
    net::MoveState state;
    state.x = player_.x;
    state.y = player_.y;
    state.z = player_.z;
    state.yaw = player_.yaw;
    state.pitch = player_.pitch;
    state.onGround = player_.onGround;
    state.horizontalCollision = player_.horizontalCollision;
    remote_->sendMovement(state);
    remote_->sendTickEnd();
    remote_->tickPlayers();
}

void GameSession::renderRemotePlayers(const std::array<double, 3>& camPos) {
    if (!remote_ || !body_) return;
    const float partial = static_cast<float>(partialTick_);
    const float t = static_cast<float>(partialTick_);
    const auto wrap = [](float degrees) {
        float d = std::fmod(degrees, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    for (const auto& [id, p] : remote_->players()) {
        if (!remote_->isPlayerEntity(p)) {  // a mob: its model and picture by its type
            const std::string* type = remote_->mobName(p.type);
            if (type == nullptr || !mobs_) continue;
            if (thrownItems_.count(p.type) != 0) continue;  // (drawn as an item: see pumpRemote)
            if (*type == "painting") {  // a picture on a square, facing the way the game says (the direction in the add packet: north 2, south 3, west 4, east 5)
                const net::PaintingVariantInfo* variant = remote_->paintingVariant(p.variant);
                if (variant == nullptr) continue;
                std::string asset = variant->asset;
                if (asset.compare(0, 10, "minecraft:") == 0) asset.erase(0, 10);
                const std::array<double, 3> middle = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
                const float facing = p.data == 2 ? 180.0f : p.data == 3 ? 0.0f : p.data == 4 ? 90.0f : p.data == 5 ? 270.0f : p.yaw;
                int paintingSky = 15, paintingBlock = 0;
                renderer_.lightAtWithCarried(static_cast<int>(std::floor(middle[0])), static_cast<int>(std::floor(middle[1])), static_cast<int>(std::floor(middle[2])), paintingSky, paintingBlock);
                mobs_->renderPainting(renderer_.viewProjection(), camPos, asset, variant->width, variant->height, middle, facing,
                                      entityLightColor(paintingSky, paintingBlock, static_cast<float>(services_.options.gamma)));
                continue;
            }
            MobPose mob;
            mob.position = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
            const float mobYaw = p.yawO + wrap(p.yaw - p.yawO) * partial;
            mob.bodyYaw = mobYaw;
            mob.headYaw = wrap((p.headYawO + wrap(p.headYaw - p.headYawO) * partial) - mobYaw);
            mob.headPitch = p.pitchO + (p.pitch - p.pitchO) * partial;
            mob.walkPosition = p.walkPosition - p.walkSpeed * (1.0f - partial);
            mob.walkSpeed = std::min(1.0f, p.walkSpeedO + (p.walkSpeed - p.walkSpeedO) * partial);
            mob.ageTicks = playerAgeTicks_ + partialTick_;
            int mobSky = 15, mobBlock = 0;
            renderer_.lightAtWithCarried(static_cast<int>(std::floor(mob.position[0])), static_cast<int>(std::floor(mob.position[1] + 1.0)), static_cast<int>(std::floor(mob.position[2])), mobSky, mobBlock);
            mob.light = entityLightColor(mobSky, mobBlock, static_cast<float>(services_.options.gamma));
            mobs_->render(renderer_.viewProjection(), camPos, *type, mob);
            continue;
        }
        BodyPose pose;
        pose.position = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
        const float bodyYaw = p.yawO + wrap(p.yaw - p.yawO) * t;
        pose.bodyYaw = bodyYaw;
        pose.headYaw = wrap((p.headYawO + wrap(p.headYaw - p.headYawO) * t) - bodyYaw);
        pose.headPitch = p.pitchO + (p.pitch - p.pitchO) * t;
        pose.walkPosition = p.walkPosition - p.walkSpeed * (1.0f - t);
        pose.walkSpeed = std::min(1.0f, p.walkSpeedO + (p.walkSpeed - p.walkSpeedO) * t);
        pose.attackProgress = p.swing;
        pose.crouching = p.sneaking;
        pose.skin = net::defaultSkinOf(p.id);
        if (const net::SkinInfo* own = remote_->skinOf(p.id)) {  // a skin of their own, once it has come
            if (const auto texture = skinTextures_.find(own->hash); texture != skinTextures_.end()) {
                pose.customSkin = texture->second.get();
                pose.customSlim = own->slim;
            }
        }
        pose.ageTicks = playerAgeTicks_ + partialTick_;
        int sky = 15, block = 0;
        renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 1.0)), static_cast<int>(std::floor(pose.position[2])), sky, block);
        pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
        body_->render(renderer_.viewProjection(), camPos, pose);
    }
}

// EntityRenderer.renderNameTag: the name hangs half a block over the head, always the same size in the world (a quarter of a pixel... 0.025 of a block for every pixel of
// the font), on a box of the background colour. It is not shown beyond 64 blocks or when the team hides it.
void GameSession::drawNameTags(GuiGraphics& g, int guiWidth, int guiHeight, const Camera& cam) {
    if (!remote_) return;
    const Matrix4f& m = renderer_.viewProjection();
    const auto project = [&](double wx, double wy, double wz, float& sx, float& sy, float& w) {
        const float x = static_cast<float>(wx - cam.x), y = static_cast<float>(wy - cam.y), z = static_cast<float>(wz - cam.z);
        const float cx = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12], cy = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
        w = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15];
        if (w <= 0.05f) return false;
        sx = (cx / w * 0.5f + 0.5f) * static_cast<float>(guiWidth);
        sy = (1.0f - (cy / w * 0.5f + 0.5f)) * static_cast<float>(guiHeight);
        return true;
    };
    const float t = static_cast<float>(partialTick_);
    for (const auto& [id, p] : remote_->players()) {
        if (!remote_->isPlayerEntity(p) || !remote_->nameTagVisible(p.id)) continue;
        const double px = p.xo + (p.x - p.xo) * partialTick_, py = p.yo + (p.y - p.yo) * partialTick_, pz = p.zo + (p.z - p.zo) * partialTick_;
        const double dx = px - cam.x, dy = py - cam.y, dz = pz - cam.z;
        if (dx * dx + dy * dy + dz * dz > 64.0 * 64.0) continue;
        const StyledText name = remote_->decoratedName(p.id);
        if (name.empty()) continue;
        float sx, sy, w;
        const double top = py + (p.sneaking ? 1.5 : 1.8) + 0.5;
        if (!project(px, top, pz, sx, sy, w)) continue;
        // How many GUI pixels one pixel of the font is: 0.025 of a block at this distance.
        const double yawRad = static_cast<double>(cam.look.yaw) * 0.017453292519943295;
        float rx, ry, rw;
        if (!project(px - std::cos(yawRad) * 0.025, top, pz - std::sin(yawRad) * 0.025, rx, ry, rw)) continue;
        const float scale = std::abs(rx - sx);
        (void)t;
        if (scale < 0.05f) continue;
        const int width = styledWidth(g, name);
        g.pushPose();
        g.pose().translate(sx, sy).scale(scale);
        g.fill(-width / 2 - 1, -1, width / 2 + 1, 9, 0x40000000u);
        drawStyled(g, name, static_cast<float>(-width / 2), 0.0f, 0xFFFFFF, false);
        g.popPose();
    }
}

// PlayerTabOverlay: the header, the columns of names (twenty to a column) with the head and the ping, the footer; shown while the player list key is held.
void GameSession::drawPlayerList(GuiGraphics& g, int guiWidth) {
    if (!remote_ || !isDown("key.playerlist")) return;
    const std::vector<net::TabEntry> entries = remote_->tabList();
    const int count = static_cast<int>(std::min<std::size_t>(entries.size(), 80));
    int widest = 0;
    for (int i = 0; i < count; ++i) {
        const net::TabEntry& e = entries[static_cast<std::size_t>(i)];
        const StyledText name = remote_->decoratedName(e.id);
        widest = std::max(widest, styledWidth(g, name));
    }
    int rows = count, columns = 1;
    while (rows > 20) {
        ++columns;
        rows = (count + columns - 1) / columns;
    }
    const int columnWidth = std::min(columns * (9 + widest + 13), guiWidth - 50) / columns;
    const int total = columnWidth * columns + (columns - 1) * 5;
    int left = guiWidth / 2 - total / 2, y = 10;
    const auto block = [&](const StyledText& text) {  // a header or footer: lines on boxes
        if (text.empty()) return;
        std::string joined;
        for (const TextSpan& span : text) joined += span.text;
        std::size_t start = 0;
        while (start <= joined.size()) {
            std::size_t end = joined.find('\n', start);
            if (end == std::string::npos) end = joined.size();
            const std::string line = joined.substr(start, end - start);
            const int w = g.textWidth(line);
            g.fill(guiWidth / 2 - w / 2 - 1, y - 1, guiWidth / 2 + w / 2 + 1, y + 9, 0x80000000u);
            g.text(line, static_cast<float>(guiWidth / 2 - w / 2), static_cast<float>(y), 0xFFFFFFFFu);
            y += 9;
            start = end + 1;
        }
        y += 1;
    };
    block(remote_->tabHeader());
    g.fill(guiWidth / 2 - total / 2 - 1, y - 1, guiWidth / 2 + total / 2 + 1, y + rows * 9, 0x80000000u);
    const Texture2D& skin = g.textures().get("entity/player/wide/steve.png");
    for (int i = 0; i < count; ++i) {
        const net::TabEntry& e = entries[static_cast<std::size_t>(i)];
        const int column = i / rows, row = i % rows;
        const int x = left + column * (columnWidth + 5), ey = y + row * 9;
        g.fill(x, ey, x + columnWidth, ey + 8, 0x20FFFFFFu);
        g.blit(skin, x, ey, 8.0f, 8.0f, 8, 8, 64, 64);
        g.blit(skin, x, ey, 40.0f, 8.0f, 8, 8, 64, 64);  // the hat layer
        const std::uint32_t alpha = e.gameMode == 3 ? 0x91000000u : 0xFF000000u;
        drawStyled(g, remote_->decoratedName(e.id), static_cast<float>(x + 9), static_cast<float>(ey), 0xFFFFFF, true, alpha);
        const char* ping = e.latency < 0 ? "icon/ping_unknown" : e.latency < 150 ? "icon/ping_5" : e.latency < 300 ? "icon/ping_4" : e.latency < 600 ? "icon/ping_3" : e.latency < 1000 ? "icon/ping_2" : "icon/ping_1";
        g.blitSprite(ping, x + columnWidth - 11, ey, 10, 8);
    }
    y += rows * 9 + 1;
    block(remote_->tabFooter());
}

}  // namespace falcon
