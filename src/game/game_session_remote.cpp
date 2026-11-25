// GameSession: playing on a server. The packets are taken in once a frame and become blocks, chunks, a player that is moved, a chat line; what the player does each tick
// is said back (the position, the keys, the sprint). The other players are drawn with their name over them, and Tab shows the player list.
#include "game_session_internal.hpp"

#include "falcon/game/item_names.hpp"
#include "falcon/render/mob_renderer.hpp"
#include "falcon/render/player_body.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/util/log.hpp"

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
    names.push_back("item_frame");
    names.push_back("glow_item_frame");
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

// What each particle type of the game's registry carries after its number (needed to step over the particles in entity data).
std::unordered_map<int, net::ParticleData> particleKinds(const std::filesystem::path& dataRoot) {
    std::unordered_map<int, net::ParticleData> kinds;
    std::ifstream in(dataRoot / "registries.json", std::ios::binary);
    if (!in) return kinds;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return kinds;
    const auto registry = doc.find("minecraft:particle_type");
    if (registry == doc.end()) return kinds;
    const auto entries = registry->find("entries");
    if (entries == registry->end()) return kinds;
    for (const auto& [name, entry] : entries->items()) {
        const int number = entry.value("protocol_id", -1);
        const net::ParticleData kind = net::particleDataOf(name);
        if (number >= 0 && kind != net::ParticleData::None) kinds[number] = kind;
    }
    return kinds;
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
    for (const auto& [name, entry] : entries->items()) {  // every type the game has, with how it is written when that is known
        const int number = entry.value("protocol_id", -1);
        const net::ComponentKind kind = net::componentKindOf(name);
        if (number >= 0 && kind != net::ComponentKind::Unknown) ids.kinds[number] = kind;
    }
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
    {  // the options the client information packet carries (Options in the game's own words)
        net::MultiplayerClient::ClientInformation info;
        info.language = services_.options.language;
        info.viewDistance = renderDistance();  // (the server sends as many chunks as the client asks for, up to its own limit)
        info.chatVisibility = static_cast<int>(services_.options.chatVisibility);
        info.chatColors = services_.options.chatColors;
        info.skinParts = 0;
        for (int part = 0; part < 7; ++part) {
            if (services_.options.modelParts[static_cast<std::size_t>(part)]) info.skinParts |= 1 << part;
        }
        info.mainHand = services_.options.mainHandRight ? 1 : 0;
        info.particles = static_cast<int>(services_.options.particles);
        remote_->setClientInformation(info);
    }
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
    if (!overworld_) {  // a server makes the world: its grass, leaves and water still take the colours of the game's biomes, so their files are read without a generator
        std::string biomeError;
        remoteBiomes_ = worldgen::BiomeRegistry::load(dataRoot_ / "data" / "minecraft" / "worldgen" / "biome", biomeError);
        if (remoteBiomes_) {
            models_.setBiomes(remoteBiomes_.get());
        } else {
            falcon::log::line("the biomes of the server's chunks could not be read (%s): grass and leaves keep one colour\n", biomeError.c_str());
        }
    }
    remote_->setBiomeResolver([this](std::string_view name) -> std::uint16_t {  // the server's biome registry comes in during configuration: its names are Falcon's
        const worldgen::BiomeRegistry* biomes = remoteBiomes_ ? remoteBiomes_.get() : (overworld_ ? &overworld_->biomes() : nullptr);
        if (biomes == nullptr) return 0;
        const worldgen::BiomeId id = biomes->find(name);
        return id == worldgen::kNoBiome ? 0 : static_cast<std::uint16_t>(id);
    });
    remote_->setComponentIds(componentIds(dataRoot_));
    remote_->setParticleKinds(particleKinds(dataRoot_));
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
    h.light = [this](ChunkPos pos, const ChunkLightPatch& patch) { renderer_.receiveLight(pos, patch); };
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
    h.moveVehicle = [this](double x, double y, double z, float yaw, float pitch) {  // ClientPacketListener.handleMoveVehicle: the server puts the boat that is driven here somewhere
        (void)pitch;
        if (!boat_ || remote_->vehicle() != boatEntity_) return;
        const double dx = x - boat_->x, dy = y - boat_->y, dz = z - boat_->z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > 9.999999747378752E-6) {
            boat_->x = x;
            boat_->y = y;
            boat_->z = z;
            boat_->yaw = yaw;
            if (net::RemotePlayer* e = remote_->entity(boatEntity_)) {
                e->x = e->xo = x;
                e->y = e->yo = y;
                e->z = e->zo = z;
                e->yaw = e->yawO = yaw;
            }
        }
        remote_->sendMoveVehicle(boat_->x, boat_->y, boat_->z, boat_->yaw, 0.0f, boat_->onGround);  // (and either way, where it is is told back)
    };
    h.newWorld = [this]() {  // the server moved the player to another world: the chunks of the old one go, and the player has to be put somewhere and say it is loaded again
        renderer_.forgetAllChunks();
        loadedSent_ = false;
        remoteTeleported_ = false;
        if (boat_ || ridingEntity_ >= 0) leaveVehicle();
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
        remoteMenu_->experienceLevel = [this] { return vitals_.level; };
        remoteMenu_->onRename = [this](const std::string& name) {
            if (remote_ && remote_->inPlay()) remote_->sendRenameItem(name);
        };
        remoteMenu_->onSelectTrade = [this](int index) {
            if (remote_ && remote_->inPlay()) remote_->sendSelectTrade(index);
        };
        remoteMenu_->onButton = [this, id](int button) {
            if (remote_ && remote_->inPlay()) remote_->sendContainerButton(id, button);
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
    h.merchantOffers = [this](const net::TradeOffers& offers) {  // a villager's window: the offers and its level
        if (!remoteMenuOpen_ || remoteMenu_->containerId() != offers.containerId || remoteMenu_->kind() != RemoteMenuKind::Merchant) return;
        remoteMenu_->offers = offers.offers;
        remoteMenu_->traderLevel = offers.level;
        remoteMenu_->traderXp = offers.xp;
        remoteMenu_->showProgress = offers.showProgress;
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
        falcon::log::line("left the server: %s\n", why.c_str());
        addChatMessage("\xC2\xA7" "cDisconnected: " + why);
        remoteLost_ = why;
    };
    if (!remote_->start(host, port, username, error)) {
        remote_.reset();
        renderer_.setRemote(false);
        return false;
    }
    falcon::log::line("connecting to %s:%d as %s\n", host.c_str(), port, username.c_str());
    return true;
}

// Skins that finished downloading are put on the graphics card (here, where there is a context), and the account's own skin goes on the hand too.
// What a click hits (MultiPlayerGameMode.attack, Minecraft.pick): the nearest of the players and mobs whose box the line from the eye meets within reach. A player's box is 0.6 wide
// and 1.8 high (1.5 sneaking), a mob's is its type's; arrows in the air are not hit.
int GameSession::entityUnderCrosshair(const std::array<double, 3>& eye, const std::array<double, 3>& look, double maxDistance, double* distance) const {
    if (!remote_) return -1;
    int found = -1;
    double best = maxDistance;
    for (const auto& [id, p] : remote_->players()) {
        double halfWidth = 0.0, height = 0.0;
        double half[3] = {0.0, 0.0, 0.0};  // for a thing hung on a wall: the box is round its position (HangingEntity)
        bool hanging = false;
        if (remote_->isPlayerEntity(p)) {
            halfWidth = 0.3;
            height = p.sneaking ? 1.5 : 1.8;
        } else {
            const std::string* type = remote_->mobName(p.type);
            float w = 0.0f, h = 0.0f;
            if (type != nullptr && (*type == "item_frame" || *type == "glow_item_frame" || *type == "painting")) {
                double across = 0.75, up = 0.75;  // a frame is 12 pixels square; a painting is as big as its variant says
                if (*type == "painting") {
                    const net::PaintingVariantInfo* variant = remote_->paintingVariant(p.variant);
                    if (variant == nullptr) continue;
                    across = variant->width;
                    up = variant->height;
                }
                const bool onX = p.data == 4 || p.data == 5, onY = p.data == 0 || p.data == 1;  // (west and east walls: flat in x; floor and ceiling: flat in y)
                const double thin = 0.03125;  // (a sixteenth of a block thick, round the position)
                if (onX) { half[0] = thin; half[1] = up / 2.0; half[2] = across / 2.0; }
                else if (onY) { half[0] = across / 2.0; half[1] = thin; half[2] = up / 2.0; }
                else { half[0] = across / 2.0; half[1] = up / 2.0; half[2] = thin; }
                hanging = true;
            } else if (type == nullptr || !MobRenderer::attackable(*type) || !MobRenderer::sizeOf(*type, w, h)) {
                continue;
            } else {
                halfWidth = static_cast<double>(w) / 2.0;
                height = static_cast<double>(h);
            }
        }
        const double px = p.xo + (p.x - p.xo) * partialTick_, py = p.yo + (p.y - p.yo) * partialTick_, pz = p.zo + (p.z - p.zo) * partialTick_;
        const double lo[3] = {hanging ? px - half[0] : px - halfWidth, hanging ? py - half[1] : py, hanging ? pz - half[2] : pz - halfWidth};
        const double hi[3] = {hanging ? px + half[0] : px + halfWidth, hanging ? py + half[1] : py + height, hanging ? pz + half[2] : pz + halfWidth};
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
    if (found >= 0 && distance != nullptr) *distance = best;
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
        if (type.compare(0, 6, "frame:") == 0) {  // frame:diamond (a frame with that item in it)
            const auto item = itemRegistry().find(type.substr(6));
            const std::array<double, 3> middle = {player_.x + forwardX * 4.5 + rightX * x, player_.y + 1.0, player_.z + forwardZ * 4.5 + rightZ * x};
            const Texture2D* icon = nullptr;
            if (item) {
                const int key = static_cast<int>(*item);
                auto found = frameIcons_.find(key);
                if (found == frameIcons_.end()) {
                    const Image picture = itemIcons_.render(ItemStack(*item, 1), 32);
                    found = frameIcons_.emplace(key, picture.empty() ? nullptr : std::make_unique<Texture2D>(picture, FilterMode::Nearest)).first;
                }
                icon = found->second.get();
            }
            mobs_->renderItemFrame(renderer_.viewProjection(), camPos, false, middle, player_.yaw + 180.0f, icon, static_cast<int>(playerAgeTicks_ / 20.0) & 7, {1.0f, 1.0f, 1.0f});
            x += 1.6;
            continue;
        }
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

std::string GameSession::enchantmentName(int id, int level) const {
    const std::string* name = remote_ ? remote_->enchantmentName(id) : nullptr;
    if (name == nullptr) return "?";
    std::string path = *name;
    if (path.compare(0, 10, "minecraft:") == 0) path.erase(0, 10);
    std::string text = services_.language.get("enchantment.minecraft." + path);
    if (level != 1 || path == "channeling") text += " " + services_.language.get("enchantment.level." + std::to_string(level));
    return text;
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
    if (menuKind == RemoteMenuKind::Merchant) {  // nine offers, to see the list, the scroller and the level
        const char* const names[] = {"stone", "oak_planks", "diamond", "iron_ingot", "coal", "apple", "cobblestone", "stick", "torch"};
        for (int i = 0; i < 9; ++i) {
            net::TradeOffer offer;
            const auto a = itemRegistry().find(names[i]), b = itemRegistry().find(names[(i + 3) % 9]), c = itemRegistry().find(names[(i + 5) % 9]);
            if (!a || !b || !c) continue;
            offer.costA = ItemStack(*a, 3 + i);
            if (i % 2 == 0) offer.costB = ItemStack(*b, 1 + i % 3);
            offer.result = ItemStack(*c, 1 + i % 4);
            offer.outOfStock = i == 4;
            offer.demand = i == 2 ? 2 : 0;
            offer.priceMultiplier = 0.05f;
            remoteMenu_->offers.push_back(offer);
        }
        remoteMenu_->traderLevel = 2;
        remoteMenu_->traderXp = 40;
        remoteMenu_->showProgress = true;
    }
    if (menuKind == RemoteMenuKind::Anvil) remoteMenu_->setData(0, 7);  // what putting these together costs
    if (menuKind == RemoteMenuKind::Enchanting) {
        remoteMenu_->setData(0, 5);
        remoteMenu_->setData(1, 15);
        remoteMenu_->setData(2, 30);
    }
    remoteMenuTitle_ = kind;
    remoteMenuOpen_ = true;
    containerOpenRequested_ = true;
}

// The picture of a cape by its hash (downloaded already), or, with FALCON_TEST_CAPE=<a 64x32 png>, that one for every body that has no cape (a test aid, to see the cape).
const Texture2D* GameSession::capeFor(const std::string& hash) {
    if (!hash.empty()) {
        if (const auto texture = skinTextures_.find(hash); texture != skinTextures_.end()) return texture->second.get();
    }
    static const char* const path = std::getenv("FALCON_TEST_CAPE");
    if (path == nullptr) return nullptr;
    if (!testCape_) {
        std::ifstream in(path, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (const auto image = decodePng(bytes)) testCape_ = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
    }
    return testCape_.get();
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
            if (const auto own = localSkins_->profileSkin(services_.playerUuid)) {
                if (d.cape && own->capeHash == d.hash) {
                    localCapeHash_ = d.hash;
                } else if (!d.cape && own->hash == d.hash) {
                    localSkinHash_ = d.hash;
                    if (!d.slim) hand_.setSkin(d.image);
                }
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
void GameSession::sendRemoteTick(const PlayerInput& input, bool riding, const BoatBody* steered) {
    // LocalPlayer.sendChanges returns until the client has told the server it has the world (hasClientLoaded): no keys, no movement before that.
    if (!remote_ || !remote_->inPlay() || !remoteTeleported_ || !loadedSent_) return;
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
    if (player_.startedGliding) remote_->sendFallFlying();  // (LocalPlayer.aiStep: START_FALL_FLYING)
    syncCreativeSlots();
    if (riding) {  // ClientboundSetPassengers: the rider's position is the vehicle's; the server is told only how it looks round, and the vehicle's place when the rider steers it
        remote_->sendRotation(player_.yaw, player_.pitch, player_.onGround, player_.horizontalCollision);
        if (steered != nullptr) remote_->sendMoveVehicle(steered->x, steered->y, steered->z, steered->yaw, 0.0f, steered->onGround);
        remote_->tickPlayers();
        return;
    }
    net::MoveState state;
    state.x = player_.x;
    state.y = player_.y;
    state.z = player_.z;
    state.yaw = player_.yaw;
    state.pitch = player_.pitch;
    state.onGround = player_.onGround;
    state.horizontalCollision = player_.horizontalCollision;
    remote_->sendMovement(state);
    remote_->tickPlayers();
}

bool GameSession::riding() const {
    return remote_ && remoteTeleported_ && remote_->vehicle() >= 0 && remote_->players().count(remote_->vehicle()) != 0;
}

// The player gets off (the server said so, or the vehicle is gone): the boat is no longer this machine's to move.
void GameSession::leaveVehicle() {
    if (remote_) remote_->setControlledEntity(-1);
    boat_.reset();
    boatEntity_ = -1;
    ridingEntity_ = -1;
    player_.vx = player_.vy = player_.vz = 0.0;
}

void GameSession::clampRiderYaw(float boatYaw) {
    const auto wrap = [](float degrees) {
        float d = std::fmod(degrees, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    const float relative = wrap(player_.yaw - boatYaw);
    const float clamped = std::clamp(relative, -105.0f, 105.0f);
    player_.yawO += clamped - relative;
    player_.yaw += clamped - relative;
}

// Entity.rideTick for the local player, with the order of the game: the boat moves (and its paddles are told), then the rider is put on the seat and turned along with the boat,
// then the tick's packets go out.
void GameSession::tickRiding(const PlayerInput& input) {
    const int id = remote_->vehicle();
    net::RemotePlayer* vehicle = remote_->entity(id);
    if (vehicle == nullptr) return;
    const std::string* type = remote_->mobName(vehicle->type);
    const auto ends = [type](const char* tail) {
        const std::string t = tail;
        return type != nullptr && type->size() >= t.size() && type->compare(type->size() - t.size(), t.size(), t) == 0;
    };
    const bool isBoat = ends("_boat") || ends("_raft"), isRaft = ends("_raft");
    int index = 0, count = 1;
    if (const std::vector<int>* riders = remote_->passengersOf(id)) {
        count = static_cast<int>(riders->size());
        const auto self = std::find(riders->begin(), riders->end(), remote_->selfEntity());
        index = self == riders->end() ? 0 : static_cast<int>(self - riders->begin());
    }
    const bool steering = isBoat && index == 0;  // (the first rider of a boat drives it)
    if (id != ridingEntity_) {  // ClientPacketListener.handleSetEntityPassengersPacket: boarding a boat turns the player to the way it points
        ridingEntity_ = id;
        if (isBoat) player_.yaw = player_.yawO = vehicle->yaw;
    }
    player_.xo = player_.x;
    player_.yo = player_.y;
    player_.zo = player_.z;
    player_.yawO = player_.yaw;
    player_.pitchO = player_.pitch;
    player_.vx = player_.vy = player_.vz = 0.0;
    BoatBody seatBody;  // where the vehicle is: the rider's seat is worked out from it
    if (steering) {
        if (!boat_ || boatEntity_ != id) {
            boat_.emplace();
            boat_->x = vehicle->x;
            boat_->y = vehicle->y;
            boat_->z = vehicle->z;
            boat_->yaw = vehicle->yaw;
            boatEntity_ = id;
            remote_->setControlledEntity(id);
        }
        BoatInput keys;
        keys.left = input.left;
        keys.right = input.right;
        keys.up = input.forward;
        keys.down = input.backward;
        boat_->tick(renderer_.level(), keys);
        vehicle->xo = vehicle->x;  // (the frames between two ticks draw it from where it was)
        vehicle->yo = vehicle->y;
        vehicle->zo = vehicle->z;
        vehicle->yawO = vehicle->yaw;
        vehicle->x = boat_->x;
        vehicle->y = boat_->y;
        vehicle->z = boat_->z;
        vehicle->yaw = boat_->yaw;
        remote_->sendPaddleBoat(boat_->paddleLeft, boat_->paddleRight);
        seatBody = *boat_;
    } else {
        if (boat_) {  // someone else sits first now
            remote_->setControlledEntity(-1);
            boat_.reset();
            boatEntity_ = -1;
        }
        seatBody.x = vehicle->x;
        seatBody.y = vehicle->y;
        seatBody.z = vehicle->z;
        seatBody.yaw = vehicle->yaw;
    }
    const std::array<double, 3> seat = seatBody.seat(isRaft, index, count);
    player_.x = seat[0];
    player_.y = seat[1];
    player_.z = seat[2];
    if (isBoat) {
        if (steering) player_.yaw += boat_->deltaRotation;  // (AbstractBoat.positionRider: the rider turns with the boat)
        clampRiderYaw(seatBody.yaw);
    }
    sendRemoteTick(input, /*riding=*/true, steering ? &*boat_ : nullptr);
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
    const double maxDist = static_cast<double>(renderDistance()) * 16.0;
    const double maxDistSq = maxDist * maxDist;
    for (const auto& [id, p] : remote_->players()) {
        const int cx = chunkOf(static_cast<int>(std::floor(p.x)));
        const int cz = chunkOf(static_cast<int>(std::floor(p.z)));
        if (!renderer_.isChunkRendered(cx, cz)) continue;
        const double dx = p.x - camPos[0], dz = p.z - camPos[2];
        if (dx * dx + dz * dz > maxDistSq) continue;
        if (!remote_->isPlayerEntity(p)) {  // a mob: its model and picture by its type
            const std::string* type = remote_->mobName(p.type);
            if (type == nullptr || !mobs_) continue;
            if (thrownItems_.count(p.type) != 0) continue;  // (drawn as an item: see pumpRemote)
            if (*type == "item_frame" || *type == "glow_item_frame") {  // a frame with what is in it
                const std::array<double, 3> middle = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
                const float facing = p.data == 2 ? 180.0f : p.data == 3 ? 0.0f : p.data == 4 ? 90.0f : p.data == 5 ? 270.0f : p.yaw;
                const Texture2D* icon = nullptr;
                if (!p.stack.isEmpty()) {  // the picture of the item, made the first time it is seen
                    const int key = static_cast<int>(p.stack.item);
                    auto found = frameIcons_.find(key);
                    if (found == frameIcons_.end()) {
                        const Image picture = itemIcons_.render(p.stack, 32);
                        found = frameIcons_.emplace(key, picture.empty() ? nullptr : std::make_unique<Texture2D>(picture, FilterMode::Nearest)).first;
                    }
                    icon = found->second.get();
                }
                int frameSky = 15, frameBlock = 0;
                renderer_.lightAtWithCarried(static_cast<int>(std::floor(middle[0])), static_cast<int>(std::floor(middle[1])), static_cast<int>(std::floor(middle[2])), frameSky, frameBlock);
                mobs_->renderItemFrame(renderer_.viewProjection(), camPos, *type == "glow_item_frame", middle, facing, icon, p.frameRotation,
                                       entityLightColor(frameSky, frameBlock, static_cast<float>(services_.options.gamma)));
                continue;
            }
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
            mob.standFlags = p.standFlags;
            mob.standPose = p.standPose;
            int mobSky = 15, mobBlock = 0;
            renderer_.lightAtWithCarried(static_cast<int>(std::floor(mob.position[0])), static_cast<int>(std::floor(mob.position[1] + 1.0)), static_cast<int>(std::floor(mob.position[2])), mobSky, mobBlock);
            mob.light = entityLightColor(mobSky, mobBlock, static_cast<float>(services_.options.gamma));
            mobs_->render(renderer_.viewProjection(), camPos, *type, mob);
            continue;
        }
        // Who is not seen: the invisible (the flag of entity data 0) and, unless this player is one too, spectators.
        if ((p.entityFlags & 32) != 0) continue;
        if (const net::TabEntry* listed = remote_->tabEntry(p.id); listed != nullptr && listed->gameMode == 3 && info_.gameType != 3) continue;
        BodyPose pose;
        pose.position = {p.xo + (p.x - p.xo) * partialTick_, p.yo + (p.y - p.yo) * partialTick_, p.zo + (p.z - p.zo) * partialTick_};
        const float headYaw = p.headYawO + wrap(p.headYaw - p.headYawO) * t;
        const float lookPitch = p.pitchO + (p.pitch - p.pitchO) * t;
        const float lookYaw = p.yawO + wrap(p.yaw - p.yawO) * t;
        float bodyYaw = p.bodyYawO + wrap(p.bodyYaw - p.bodyYawO) * t;  // (the body follows the walk; the head turns on it)
        const net::RemotePlayer* mount = p.vehicle >= 0 ? [&]() -> const net::RemotePlayer* {
            const auto it = remote_->players().find(p.vehicle);
            return it == remote_->players().end() ? nullptr : &it->second;
        }() : nullptr;
        pose.riding = mount != nullptr;
        if (mount != nullptr) {  // LivingEntityRenderer: on a mount the body goes where the mount does (a boat or a minecart turns it altogether, a living mount holds it within 85 degrees of the head)
            const float mountYaw = mount->yawO + wrap(mount->yaw - mount->yawO) * t;
            const std::string* mountType = remote_->mobName(mount->type);
            const bool vehicle = mountType != nullptr && (mountType->find("boat") != std::string::npos || mountType->find("raft") != std::string::npos || mountType->find("minecart") != std::string::npos);
            if (vehicle) {
                bodyYaw = mountYaw;
            } else {
                float diff = std::clamp(wrap(headYaw - mountYaw), -85.0f, 85.0f);
                bodyYaw = headYaw - diff;
                if (std::abs(diff) > 50.0f) bodyYaw += diff * 0.2f;
            }
        }
        pose.bodyYaw = bodyYaw;
        pose.headYaw = wrap(headYaw - bodyYaw);
        pose.headPitch = lookPitch;
        pose.walkPosition = p.walkPosition - p.walkSpeed * (1.0f - t);
        pose.walkSpeed = pose.riding ? 0.0f : std::min(1.0f, p.walkSpeedO + (p.walkSpeed - p.walkSpeedO) * t);
        {  // LivingEntity.getAttackAnim: the swing between two ticks (a swing that has just started over again wraps)
            float step = p.swing - p.swingO;
            if (step < 0.0f) step += 1.0f;
            pose.attackProgress = p.swing == 0.0f && p.swingO == 0.0f ? 0.0f : p.swingO + step * t;
        }
        pose.leftHanded = p.mainArmLeft;
        pose.swingOffHand = p.swingOffHand;
        pose.crouching = p.sneaking && !pose.riding;
        if (p.pose == 2) {  // sleeping: the bed's direction (Entity.getBedOrientation: the bed block's facing, from the block at the sleeping position) decides how it lies
            pose.sleeping = true;
            pose.crouching = false;
            pose.sleepYaw = bodyYaw;
            if (p.hasSleepPos) {
                const Block bedBlock = renderer_.level().block(p.sleepX, p.sleepY, p.sleepZ);
                if (registry_->type(bedBlock).name.find("_bed") != std::string::npos) {
                    const std::string_view facing = registry_->get(bedBlock, "facing");
                    // LivingEntityRenderer.sleepDirectionToRotation: south 90, west 0, north 270, east 180
                    const struct { const char* name; float yaw; int dx, dz; } kBeds[4] = {{"south", 90.0f, 0, 1}, {"west", 0.0f, -1, 0}, {"north", 270.0f, 0, -1}, {"east", 180.0f, 1, 0}};
                    for (const auto& bed : kBeds) {
                        if (facing == bed.name) {
                            pose.sleepYaw = bed.yaw;
                            pose.hasBed = true;
                            pose.bedStepX = bed.dx;
                            pose.bedStepZ = bed.dz;
                        }
                    }
                }
            }
        }
        pose.skin = net::defaultSkinOf(p.id);
        const net::SkinInfo* ownSkin = remote_->skinOf(p.id);
        if (ownSkin != nullptr) {  // a skin of their own, once it has come
            if (const auto texture = skinTextures_.find(ownSkin->hash); texture != skinTextures_.end()) {
                pose.customSkin = texture->second.get();
                pose.customSlim = ownSkin->slim;
            }
        }
        pose.cape = capeFor(ownSkin != nullptr ? ownSkin->capeHash : std::string());
        for (std::size_t part = 0; part < pose.parts.size(); ++part) pose.parts[part] = (p.skinParts >> part & 1) != 0;  // the Skin Customization they chose
        pose.ageTicks = playerAgeTicks_ + partialTick_;
        int sky = 15, block = 0;
        renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 1.0)), static_cast<int>(std::floor(pose.position[2])), sky, block);
        pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
        for (std::size_t i = 0; i < 4; ++i) {  // head, chest, legs, feet: the equipment slots 5, 4, 3 and 2
            const ItemStack& worn = p.equipment[5 - i];
            if (!worn.isEmpty() && worn.type().armorSlot != EquipSlot::None) pose.armor[i] = worn.type().name;
        }
        pose.elytra = !p.equipment[4].isEmpty() && p.equipment[4].type().name == "elytra";
        pose.elytraX = p.elytraXO + (p.elytraX - p.elytraXO) * t;
        pose.elytraY = p.elytraYO + (p.elytraY - p.elytraYO) * t;
        pose.elytraZ = p.elytraZO + (p.elytraZ - p.elytraZO) * t;
        // Swimming (Player.getSwimAmount) and what the game needs for it: whether the water is round them (the body then follows the view's pitch)
        pose.swimAmount = p.swimAmountO + (p.swimAmount - p.swimAmountO) * t;
        pose.visuallySwimming = p.pose == 3;
        {
            const BlockRegistry& registry = blockRegistry();
            const Block at = renderer_.level().block(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 0.4)), static_cast<int>(std::floor(pose.position[2])));
            pose.inWater = registry.state(at).has(kStateWater);
        }
        // Gliding (AvatarRenderer.setupRotations): the body leans over as it picks up speed, and turns toward where it is going when that is not where it looks
        if ((p.entityFlags & 0x80) != 0) {
            const double deltaX = p.deltaXO + (p.deltaX - p.deltaXO) * partialTick_, deltaY = p.deltaYO + (p.deltaY - p.deltaYO) * partialTick_, deltaZ = p.deltaZO + (p.deltaZ - p.deltaZO) * partialTick_;
            poseGliding(pose, static_cast<float>(p.fallFlyingTicks) + t, lookYaw, lookPitch, deltaX, deltaY, deltaZ);
        }
        if (pose.cape != nullptr) {  // how the cape hangs and swings (AvatarRenderer.extractCapeState); it lies flat along the body in a glide
            const float flown = static_cast<float>(p.fallFlyingTicks) + t;
            const float glide = (p.entityFlags & 0x80) != 0 ? std::clamp(flown * flown / 100.0f, 0.0f, 1.0f) : 0.0f;
            const CapeAngles angles = capeAngles(p.cloak, t, p.xo, p.x, p.yo, p.y, p.zo, p.z, p.bodyYawO, p.bodyYaw, glide);
            pose.capeFlap = angles.flap;
            pose.capeLean = angles.lean;
            pose.capeLean2 = angles.lean2;
        }
        pose.hurt = p.hurtTime > 0 || p.deathTime > 0 ? 1.0f : 0.0f;
        if (p.deathTime > 0) {
            const float fallen = std::sqrt(std::max(0.0f, (static_cast<float>(p.deathTime) + t - 1.0f) / 20.0f * 1.6f));
            pose.deathAngle = std::min(fallen, 1.0f) * 90.0f;
        }
        // The arms (AvatarRenderer.getArmPose): an item in the hand is held forward, a shield blocks, a bow is drawn, a spyglass is held to the eye ... when it is being used
        const bool using_ = (p.livingFlags & 1) != 0;
        const bool usingOffHand = (p.livingFlags & 2) != 0;
        const auto armPoseOf = [&](const ItemStack& stack, bool beingUsed) {
            if (stack.isEmpty()) return ArmPose::Empty;
            if (!p.swinging && stack.charged && stack.type().name == "crossbow") return ArmPose::CrossbowHold;  // a loaded crossbow is held up
            if (beingUsed) {
                const std::string& name = stack.type().name;
                if (name == "crossbow") return ArmPose::CrossbowCharge;
                if (name == "shield") return ArmPose::Block;
                if (name == "bow") return ArmPose::BowAndArrow;
                if (name == "trident" || (name.size() > 6 && name.compare(name.size() - 6, 6, "_spear") == 0)) return ArmPose::ThrowTrident;
                if (name == "spyglass") return ArmPose::Spyglass;
                if (name == "goat_horn") return ArmPose::TootHorn;
                if (name == "brush") return ArmPose::Brush;
            }
            return ArmPose::Item;
        };
        const ArmPose mainPose = armPoseOf(p.equipment[0], using_ && !usingOffHand), offPose = armPoseOf(p.equipment[1], using_ && usingOffHand);
        pose.crossbowTicks = static_cast<float>(p.usingTicks) + t;
        pose.rightArmPose = p.mainArmLeft ? offPose : mainPose;
        pose.leftArmPose = p.mainArmLeft ? mainPose : offPose;
        BodyHands hands;
        body_->render(renderer_.viewProjection(), camPos, pose, &hands);
        if (hands.valid) {  // what is held: the main hand's item in the main arm, the other in the other
            hand_.renderItemInHand(renderer_, renderer_.viewProjection(), p.equipment[0], p.mainArmLeft ? hands.left : hands.right, p.mainArmLeft, sky, block);
            hand_.renderItemInHand(renderer_, renderer_.viewProjection(), p.equipment[1], p.mainArmLeft ? hands.right : hands.left, !p.mainArmLeft, sky, block);
        }
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

// PlayerTabOverlay.extractRenderState, shown while the player list key is held: the header, then the players (the first 80, in the order of the game's comparator) in columns of
// up to twenty, each with the head from their own skin (when the connection is encrypted, as the game does), the name (italic for a spectator, dimmed) and the ping bars, then the footer.
// The score column of a list objective (hearts, numbers) is not drawn: the scoreboard is not followed.
// (FALCON_TEST_TAB=1: shows the list with made-up players, with no server, for a look at the layout; a test aid.)
void GameSession::drawPlayerList(GuiGraphics& g, int guiWidth) {
    static const bool testTab = std::getenv("FALCON_TEST_TAB") != nullptr;
    if (!testTab && (!remote_ || !isDown("key.playerlist"))) return;
    std::vector<net::TabEntry> list;
    StyledText header, footer;
    bool showHeads = true;
    if (remote_ != nullptr) {
        list = remote_->tabList(80);
        header = remote_->tabHeader();
        footer = remote_->tabFooter();
        showHeads = remote_->encrypted();
    } else {
        static const char* const kNames[] = {"Steve_the_Builder", "Alex", "xX_Miner_Xx", "Notch", "jeb_", "Dinnerbone", "Herobrine", "CreeperSlayer", "Zuri", "kai"};
        for (int i = 0; i < 45; ++i) {
            net::TabEntry e;
            e.id[0] = static_cast<std::uint8_t>(i * 37 + 5);
            e.id[7] = static_cast<std::uint8_t>(i);
            e.name = std::string(kNames[i % 10]) + (i >= 10 ? std::to_string(i) : std::string());
            e.gameMode = i % 11 == 10 ? 3 : 0;
            e.latency = i == 4 ? -1 : (i * 53) % 1200;
            list.push_back(e);
        }
        header = {TextSpan{"Falcon test server", 0xFFAA00, true}, TextSpan{"\n"}, TextSpan{"play.example.net", 0x55FFFF, true}};
        footer = {TextSpan{"A footer line that is long enough to need wrapping when the window is narrow, with a second part", 0xAAAAAA, true}};
    }
    std::vector<StyledText> names;
    names.reserve(list.size());
    int maxNameWidth = g.textWidth(" ");
    for (const net::TabEntry& e : list) {
        StyledText name = remote_ != nullptr ? remote_->decoratedName(e.id) : StyledText{TextSpan{e.name}};
        if (e.gameMode == 3) {
            for (TextSpan& span : name) span.italic = true;  // (PlayerTabOverlay.decorateName)
        }
        maxNameWidth = std::max(maxNameWidth, styledWidth(g, name));
        names.push_back(std::move(name));
    }
    const int playerCount = static_cast<int>(list.size());
    int rows = playerCount, columns = 1;
    while (rows > 20) {
        ++columns;
        rows = (playerCount + columns - 1) / columns;
    }
    columns = std::max(columns, 1);
    const int columnWidth = std::min(columns * ((showHeads ? 9 : 0) + maxNameWidth + 13), guiWidth - 50) / columns;
    const int left = guiWidth / 2 - (columnWidth * columns + (columns - 1) * 5) / 2;
    int totalWidth = columnWidth * columns + (columns - 1) * 5;
    std::vector<StyledText> headerLines, footerLines;
    if (!header.empty()) headerLines = wrapStyledText(g.font(), header, guiWidth - 50, 1000);
    if (!footer.empty()) footerLines = wrapStyledText(g.font(), footer, guiWidth - 50, 1000);
    for (const StyledText& line : headerLines) totalWidth = std::max(totalWidth, styledWidth(g, line));
    for (const StyledText& line : footerLines) totalWidth = std::max(totalWidth, styledWidth(g, line));
    constexpr std::uint32_t kPanel = 0x80000000u;  // Integer.MIN_VALUE
    int y = 10;
    if (!headerLines.empty()) {
        g.fill(guiWidth / 2 - totalWidth / 2 - 1, y - 1, guiWidth / 2 + totalWidth / 2 + 1, y + static_cast<int>(headerLines.size()) * 9, kPanel);
        for (const StyledText& line : headerLines) {
            drawStyled(g, line, static_cast<float>(guiWidth / 2 - styledWidth(g, line) / 2), static_cast<float>(y), 0xFFFFFF, true);
            y += 9;
        }
        ++y;
    }
    g.fill(guiWidth / 2 - totalWidth / 2 - 1, y - 1, guiWidth / 2 + totalWidth / 2 + 1, y + rows * 9, kPanel);
    static constexpr const char* kDefaultSkins[9] = {"alex", "ari", "efe", "kai", "makena", "noor", "steve", "sunny", "zuri"};
    for (int i = 0; i < playerCount; ++i) {
        const net::TabEntry& e = list[static_cast<std::size_t>(i)];
        const int column = i / rows, row = i % rows;
        int x = left + column * columnWidth + column * 5;
        const int ey = y + row * 9;
        g.fill(x, ey, x + columnWidth, ey + 8, 0x20FFFFFFu);
        const int columnLeft = x;
        if (showHeads) {  // PlayerFaceRenderer.draw: the face, then the hat layer over it
            const Texture2D* skin = nullptr;
            if (const net::SkinInfo* own = remote_ != nullptr ? remote_->skinOf(e.id) : nullptr) {
                if (const auto texture = skinTextures_.find(own->hash); texture != skinTextures_.end()) skin = texture->second.get();
            }
            if (skin == nullptr) {
                const int index = net::defaultSkinOf(e.id);
                skin = &g.textures().get(std::string("entity/player/") + (index < 9 ? "slim/" : "wide/") + kDefaultSkins[index % 9] + ".png");
            }
            g.blit(*skin, x, ey, 8.0f, 8.0f, 8, 8, 64, 64);
            if (e.hat) g.blit(*skin, x, ey, 40.0f, 8.0f, 8, 8, 64, 64);
            x += 9;
        }
        const std::uint32_t alpha = e.gameMode == 3 ? 0x91000000u : 0xFF000000u;  // (a spectator's name is dimmed: -1862270977)
        drawStyled(g, names[static_cast<std::size_t>(i)], static_cast<float>(x), static_cast<float>(ey), 0xFFFFFF, true, alpha);
        const char* ping = e.latency < 0 ? "icon/ping_unknown" : e.latency < 150 ? "icon/ping_5" : e.latency < 300 ? "icon/ping_4" : e.latency < 600 ? "icon/ping_3" : e.latency < 1000 ? "icon/ping_2" : "icon/ping_1";
        g.blitSprite(ping, columnLeft + columnWidth - 11, ey, 10, 8);
    }
    if (!footerLines.empty()) {
        y += rows * 9 + 1;
        g.fill(guiWidth / 2 - totalWidth / 2 - 1, y - 1, guiWidth / 2 + totalWidth / 2 + 1, y + static_cast<int>(footerLines.size()) * 9, kPanel);
        for (const StyledText& line : footerLines) {
            drawStyled(g, line, static_cast<float>(guiWidth / 2 - styledWidth(g, line) / 2), static_cast<float>(y), 0xFFFFFF, true);
            y += 9;
        }
    }
}

}  // namespace falcon
