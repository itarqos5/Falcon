#include "falcon/net/multiplayer_client.hpp"
#include "falcon/world/boat.hpp"
#include "falcon/util/log.hpp"

#include "falcon/net/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace falcon::net {

namespace {

constexpr int kProtocolVersion = 777;  // Minecraft 26.3

// Falcon's Face order (east, west, up, down, south, north) to the protocol's Direction order (down, up, north, south, west, east).
constexpr int kProtocolDirection[6] = {5, 4, 1, 0, 3, 2};

std::int64_t packBlockPos(int x, int y, int z) {
    return (static_cast<std::int64_t>(x & 0x3FFFFFF) << 38) | (static_cast<std::int64_t>(z & 0x3FFFFFF) << 12) | static_cast<std::int64_t>(y & 0xFFF);
}

void unpackBlockPos(std::int64_t v, int& x, int& y, int& z) {
    x = static_cast<int>(v >> 38);
    y = static_cast<int>(v << 52 >> 52);
    z = static_cast<int>(v << 26 >> 38);
}

float angleOf(int byteValue) { return static_cast<float>(byteValue) * 360.0f / 256.0f; }

std::uint32_t formattingColor(int index) {
    static constexpr std::uint32_t kColors[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
                                                  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
    return kColors[index & 15];
}

struct PositionMoveRotation {
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    float yaw = 0, pitch = 0;
};

PositionMoveRotation readPositionMoveRotation(Reader& r) {
    PositionMoveRotation p;
    p.x = r.f64();
    p.y = r.f64();
    p.z = r.f64();
    p.dx = r.f64();
    p.dy = r.f64();
    p.dz = r.f64();
    p.yaw = r.f32();
    p.pitch = r.f32();
    return p;
}

// The slots of the player's own menu (InventoryMenu) as the server numbers them: 0 the crafting result, 1-4 the crafting grid, 5-8 helmet to boots, 9-35 the inventory,
// 36-44 the hotbar, 45 the offhand. The number of the same slot in Inventory (0-8 hotbar, 9-35, 36 boots to 39 helmet, 40 offhand); -1 for the crafting slots.
int inventorySlotOfMenu(int menu) {
    if (menu >= 5 && menu <= 8) return 44 - menu;  // 5 helmet (39), 6 chestplate (38), 7 leggings (37), 8 boots (36)
    if (menu >= 9 && menu <= 35) return menu;
    if (menu >= 36 && menu <= 44) return menu - 36;
    if (menu == 45) return 40;
    return -1;
}

// The block entity list of a chunk packet, stepped over to reach the light after it. ClientboundLevelChunkPacketData$BlockEntityInfo is BYTE (the x and z, four bits each),
// SHORT (y), the block entity type (a number of the block entity registry) and OPTIONAL_COMPOUND_TAG, and a tag on the network is NbtIo.writeAnyTag: the type byte and then
// the payload, with no name for the root and no length in front of it. Nothing is done with the block entities themselves yet.
bool skipBlockEntities(Reader& r) {
    const int count = r.varint();
    if (count < 0 || !r.ok) return false;
    for (int i = 0; i < count && r.ok; ++i) {
        r.u8();      // the x and z of the block, four bits each
        r.i16();     // y
        r.varint();  // the type of the block entity
        if (r.boolean()) (void)readNetworkNbt(r);
    }
    return r.ok;
}

}  // namespace

#include "mount_seats.inc"

// Steps over a value of entity data by the number of its kind (EntityDataSerializers, in the order they are registered); false for a kind whose layout is not known here.
bool skipEntityDataValue(int serializer, Reader& r) {
    switch (serializer) {
        case 0: r.u8(); return r.ok;                                            // byte
        case 1: r.varint(); return r.ok;                                        // int
        case 2: r.varlong(); return r.ok;                                       // long
        case 3: r.f32(); return r.ok;                                           // float
        case 4: (void)r.string(); return r.ok;                                  // string
        case 5: (void)readNetworkNbt(r); return r.ok;                           // component
        case 6: if (r.boolean()) (void)readNetworkNbt(r); return r.ok;         // optional component
        case 8: r.u8(); return r.ok;                                            // boolean
        case 9: r.f32(); r.f32(); r.f32(); return r.ok;                         // rotations
        case 10: r.i64(); return r.ok;                                          // block position
        case 11: if (r.boolean()) r.i64(); return r.ok;                         // optional block position
        case 12: r.varint(); return r.ok;                                       // direction
        case 13: if (r.boolean()) r.skip(16); return r.ok;                      // optional living entity reference (a UUID)
        case 14: r.varint(); return r.ok;                                       // block state
        case 15: r.varint(); return r.ok;                                       // optional block state
        case 19: r.varint(); return r.ok;                                       // optional unsigned int
        case 18: r.varint(); r.varint(); r.varint(); return r.ok;               // villager data: type, profession, level
        case 20: r.varint(); return r.ok;                                       // pose
        case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
        case 29: case 30: case 31: case 32: r.varint(); return r.ok;            // the variants of animals (a holder of a registry each)
        case 33: if (r.boolean()) { (void)r.string(); r.i64(); } return r.ok;   // optional global position
        case 34: r.varint(); return r.ok;                                       // painting variant
        case 35: case 36: case 37: case 38: r.varint(); return r.ok;            // states of a sniffer, an armadillo, a copper golem, a copper block
        case 39: r.f32(); r.f32(); r.f32(); return r.ok;                        // a vector
        case 40: r.f32(); r.f32(); r.f32(); r.f32(); return r.ok;               // a quaternion
        case 42: case 43: r.varint(); return r.ok;                              // the arm that is the main one, a dye colour
        default: return false;
    }
}

int defaultSkinOf(const Uuid& id) {
    std::uint64_t msb = 0, lsb = 0;
    for (int i = 0; i < 8; ++i) {
        msb = (msb << 8) | id[static_cast<std::size_t>(i)];
        lsb = (lsb << 8) | id[static_cast<std::size_t>(8 + i)];
    }
    // UUID.hashCode: the two halves xored, then the two halves of that xored again (as signed 32-bit numbers).
    const std::int64_t hilo = static_cast<std::int64_t>(msb ^ lsb);
    const std::int32_t hash = static_cast<std::int32_t>(hilo >> 32) ^ static_cast<std::int32_t>(hilo);
    return ((hash % 18) + 18) % 18;  // Math.floorMod
}

// The kinds of value that need what this connection knows (the particle types, the item components).
bool MultiplayerClient::skipDataValue(int serializer, Reader& r) {
    switch (serializer) {
        case 16: return skipParticle(r, particleKinds_, componentIds_);
        case 17: {  // a list of particles (the particles of a potion's effects)
            const int n = r.varint();
            if (!r.ok || n < 0 || n > 4096) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipParticle(r, particleKinds_, componentIds_)) return false;
            }
            return r.ok;
        }
        case 41: return skipResolvableProfile(r);
        default: return skipEntityDataValue(serializer, r);
    }
}

MultiplayerClient::MultiplayerClient(const BlockRegistry& registry) : registry_(registry) {}
MultiplayerClient::~MultiplayerClient() { close(); }

void MultiplayerClient::close() {
    connection_.close();
    phase_ = Phase::Closed;
}

bool MultiplayerClient::start(const std::string& host, int port, const std::string& requestedName, std::string& error) {
    const std::string username = account_.name.empty() ? requestedName : account_.name;  // (a server in online mode knows the player by the account's name)
    username_ = username;
    if (!connection_.connect(host, port, error)) {
        phase_ = Phase::Closed;
        closeReason_ = error;
        return false;
    }
    // The id the server will call this player in a server that does not check accounts is made from the name; what is sent here is not looked at.
    if (account_.profileId.empty() || !parseUuid(account_.profileId, selfId_)) {
        for (std::size_t i = 0; i < selfId_.size(); ++i) selfId_[i] = static_cast<std::uint8_t>(0x46 + i * 7 + (i < username.size() ? username[i] : 0));
    }
    Writer hello;
    hello.varint(kProtocolVersion);
    hello.string(host);
    hello.u8(port >> 8);
    hello.u8(port);
    hello.varint(2);  // the intention: log in
    connection_.send(0, hello.data);
    Writer login;
    login.string(username);
    login.uuid(selfId_);
    connection_.send(0, login.data);
    phase_ = Phase::Login;
    return true;
}

void MultiplayerClient::fail(const std::string& why, RemoteHandler* h) {
    if (phase_ == Phase::Closed) return;
    closeReason_ = why;
    phase_ = Phase::Closed;
    connection_.close();
    if (h != nullptr && h->disconnect) h->disconnect(why);
}

void MultiplayerClient::sendClientInformation(int target) {
    const ClientInformation& info = clientInformation_;
    Writer w;
    w.string(info.language.empty() ? "en_us" : info.language);
    w.u8(static_cast<std::uint8_t>(std::clamp(info.viewDistance, 2, 32)));  // (the option's range; the server sends its own limit at most)
    w.varint(std::clamp(info.chatVisibility, 0, 2));
    w.boolean(info.chatColors);
    w.u8(static_cast<std::uint8_t>(info.skinParts & 0x7F));     // the parts of the skin that are shown
    w.varint(info.mainHand == 0 ? 0 : 1);
    w.boolean(false);  // text filtering
    w.boolean(info.allowListing);
    w.varint(std::clamp(info.particles, 0, 2));
    connection_.send(target, w.data);
}

void MultiplayerClient::pump(RemoteHandler& h) {
    if (phase_ != Phase::Closed && joinResult_.valid() && joinResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) finishEncryption(h);
    if (keysResult_.valid() && keysResult_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        KeysResult keys = keysResult_.get();
        if (keys.ok) {
            chatKeys_ = std::move(keys.keys);
            haveChatKeys_ = true;
        } else {
            falcon::log::line("chat is not signed: %s\n", keys.error.c_str());
        }
    }
    if (haveChatKeys_ && !chatSessionSent_ && phase_ == Phase::Play) sendChatSession();
    Packet p;
    int budget = 4000;
    while (phase_ != Phase::Closed && budget-- > 0 && connection_.poll(p)) {
        const Phase before = phase_;
        try {
            switch (phase_) {
                case Phase::Login: handleLogin(p, h); break;
                case Phase::Configuration: handleConfiguration(p, h); break;
                case Phase::Play: handlePlay(p, h); break;
                default: break;
            }
        } catch (const std::exception& e) {  // a packet that cannot be dealt with ends the connection with the reason, not the game
            const char* where = before == Phase::Login ? "login" : before == Phase::Configuration ? "configuration" : "play";
            std::fprintf(stderr, "[falcon] packet %d of the %s phase threw: %s\n", p.id, where, e.what());
            fail(std::string("Falcon could not deal with a packet from the server (number ") + std::to_string(p.id) + " in the " + where + " phase: " + e.what() + ")", &h);
        }
    }
    if (phase_ != Phase::Closed && !connection_.open()) {
        Packet rest;
        if (!connection_.poll(rest)) fail(connection_.closedReason().empty() ? "the connection was closed" : connection_.closedReason(), &h);
    }
}

// ClientboundHelloPacket: the server wants the connection encrypted (online mode). Its public key, a challenge, and whether it will ask Mojang who we are. The shared secret
// is made up here, Mojang's session server is told that this account joins the server with this secret (which is what the server later asks it), and only then is the
// answer sent, with the secret and the challenge locked with the server's key; everything after it is encrypted. The talk to Mojang is on another thread.
void MultiplayerClient::startEncryption(Reader& r, RemoteHandler& h) {
    const std::string serverId = r.string();
    const auto bytesOf = [&r]() {
        const int n = r.varint();
        Bytes out;
        if (n < 0 || static_cast<std::size_t>(n) > r.left()) {
            r.ok = false;
            return out;
        }
        out.assign(r.here(), r.here() + n);
        r.skip(static_cast<std::size_t>(n));
        return out;
    };
    exchange_.publicKey = bytesOf();
    exchange_.challenge = bytesOf();
    const bool authenticate = r.boolean();
    if (!r.ok || exchange_.publicKey.empty()) {
        fail("the server's request for encryption cannot be read", &h);
        return;
    }
    if (authenticate && account_.accessToken.empty()) {
        fail("this server checks accounts (online mode): start Falcon from the Falcon Launcher, which signs in with your Minecraft account", &h);
        return;
    }
    if (!randomBytes(exchange_.secret, sizeof exchange_.secret)) {
        fail("the system cannot make a secret for the encryption", &h);
        return;
    }
    const std::string hash = sessionHash(serverId, Bytes(exchange_.secret, exchange_.secret + sizeof exchange_.secret), exchange_.publicKey);
    const std::string token = account_.accessToken, profile = account_.profileId;
    // (a test of the exchange against a server of a test rig, where there is no session to join: the server will then say that the name could not be verified)
    const bool skipJoin = std::getenv("FALCON_TEST_SKIP_SESSION_JOIN") != nullptr;
    joinResult_ = std::async(std::launch::async, [authenticate, skipJoin, token, profile, hash]() -> std::string {
        if (!authenticate || skipJoin) return {};
        std::string error;
        if (joinSession(token, profile, hash, error)) return {};
        return error.empty() ? "Minecraft's session server refused the sign-in" : error;
    });
    if (authenticate && !token.empty()) {  // the key that signs chat is fetched meanwhile
        keysResult_ = std::async(std::launch::async, [token, profile]() {
            KeysResult result;
            result.ok = fetchChatKeys(token, profile, result.keys, result.error);
            return result;
        });
    }
}

void MultiplayerClient::finishEncryption(RemoteHandler& h) {
    const std::string refused = joinResult_.get();
    if (!refused.empty()) {
        fail(refused, &h);
        return;
    }
    Bytes secret(exchange_.secret, exchange_.secret + sizeof exchange_.secret), lockedSecret, lockedChallenge;
    std::string error;
    if (!rsaEncrypt(exchange_.publicKey, secret, lockedSecret, error) || !rsaEncrypt(exchange_.publicKey, exchange_.challenge, lockedChallenge, error)) {
        fail(error, &h);
        return;
    }
    Writer w;
    w.varint(static_cast<std::int32_t>(lockedSecret.size()));
    w.raw(lockedSecret);
    w.varint(static_cast<std::int32_t>(lockedChallenge.size()));
    w.raw(lockedChallenge);
    if (!connection_.sendAndEncrypt(1, w.data, exchange_.secret)) fail("the connection cannot be encrypted", &h);
}

// ServerboundChatSessionUpdatePacket: a new chat session (a random id) with the key that its messages are signed with, and Mojang's signature that the key is the account's.
void MultiplayerClient::sendChatSession() {
    chatSessionSent_ = true;
    if (!randomBytes(chatSessionId_.data(), chatSessionId_.size())) return;
    chatSessionId_[6] = static_cast<std::uint8_t>((chatSessionId_[6] & 0x0F) | 0x40);  // (a random UUID: version 4)
    chatSessionId_[8] = static_cast<std::uint8_t>((chatSessionId_[8] & 0x3F) | 0x80);
    chatIndex_ = 0;
    Writer w;
    w.uuid(chatSessionId_);
    w.i64(chatKeys_.expiresAtMillis);
    w.varint(static_cast<std::int32_t>(chatKeys_.publicKey.size()));
    w.raw(chatKeys_.publicKey);
    w.varint(static_cast<std::int32_t>(chatKeys_.signature.size()));
    w.raw(chatKeys_.signature);
    connection_.send(10, w.data);
}

void MultiplayerClient::handleLogin(const Packet& p, RemoteHandler& h) {
    Reader r(p.body);
    switch (p.id) {
        case 0: fail(componentJsonToPlain(r.string()), &h); break;  // (login_disconnect has its text as JSON)
        case 1: startEncryption(r, h); break;
        case 2: {  // login_finished: the profile (id, name, properties); then the configuration begins
            selfId_ = r.uuid();
            username_ = r.string();
            connection_.send(3, {});  // login_acknowledged
            phase_ = Phase::Configuration;
            sendClientInformation(0);
            Writer brand;
            brand.string("minecraft:brand");
            brand.string("falcon");
            connection_.send(2, brand.data);
            break;
        }
        default: break;  // 3 (compression) is dealt with by the reader; cookies and plugin queries are not answered
    }
}

void MultiplayerClient::handleConfiguration(const Packet& p, RemoteHandler& h) {
    Reader r(p.body);
    switch (p.id) {
        case 2: {
            fail("disconnected: " + plainText(readComponent(r)), &h);
            break;
        }
        case 3:  // finish_configuration
            connection_.send(3, {});
            phase_ = Phase::Play;
            break;
        case 4: {  // keep_alive
            Writer w;
            w.i64(r.i64());
            connection_.send(4, w.data);
            break;
        }
        case 5: {  // ping
            Writer w;
            w.i32(r.i32());
            connection_.send(5, w.data);
            break;
        }
        case 7: {  // registry_data: of the registries, the painting variants (name and size of each, in the order of their numbers) are kept
            const std::string registry = r.string();
            const int n = r.varint();
            const bool paintings = registry == "minecraft:painting_variant", enchantments = registry == "minecraft:enchantment";
            const bool biomes = registry == "minecraft:worldgen/biome";
            if (paintings) paintingVariants_.clear();
            if (enchantments) enchantments_.clear();
            if (biomes) biomeIds_.clear();
            for (int i = 0; i < n && r.ok; ++i) {
                const std::string entryName = r.string();
                if (enchantments) enchantments_.push_back(entryName);
                // A biome's number is where it stands in this list: the chunk palettes name biomes by it.
                if (biomes) biomeIds_.push_back(biomeResolver_ ? biomeResolver_(entryName) : 0);
                NbtTag data;
                const bool hasData = r.boolean();
                if (hasData) data = readNetworkNbt(r);
                if (paintings && r.ok) {
                    PaintingVariantInfo info;
                    info.asset = data.getString("asset_id");
                    info.width = static_cast<int>(data.getInt("width", 1));
                    info.height = static_cast<int>(data.getInt("height", 1));
                    paintingVariants_.push_back(info);
                }
            }
            break;
        }
        case 15: {  // select_known_packs: none are known here, so the registries come in full
            Writer w;
            w.varint(0);
            connection_.send(7, w.data);
            break;
        }
        case 20: fail("the server asks for the code of conduct to be accepted: Falcon cannot show it yet", &h); break;
        default: break;  // registries, tags, features, links ... are not needed to stand in a world
    }
}

const TeamInfo* MultiplayerClient::teamOfPlayer(const std::string& name) const {
    if (memberIndexDirty_) {
        memberIndex_.clear();
        for (const auto& [teamName, team] : teams_) {
            for (const std::string& member : team.members) memberIndex_[member] = &team;
        }
        memberIndexDirty_ = false;
    }
    const auto it = memberIndex_.find(name);
    return it == memberIndex_.end() ? nullptr : it->second;
}

// PlayerTabOverlay.getPlayerInfos: the list order (higher first), spectators last, then by team name, then by name ignoring case; the first `limit`.
std::vector<TabEntry> MultiplayerClient::tabList(std::size_t limit) const {
    struct Key {
        const TabEntry* entry;
        int order;
        int spectator;
        std::string team;
        std::string name;
    };
    std::vector<Key> keys;
    keys.reserve(tab_.size());
    for (const auto& [id, entry] : tab_) {
        if (!entry.listed) continue;
        const TeamInfo* team = teamOfPlayer(entry.name);
        std::string lowered = entry.name;
        for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        keys.push_back({&entry, entry.order, entry.gameMode == 3 ? 1 : 0, team != nullptr ? team->name : std::string(), std::move(lowered)});
    }
    const auto before = [](const Key& a, const Key& b) {
        if (a.order != b.order) return a.order > b.order;
        if (a.spectator != b.spectator) return a.spectator < b.spectator;
        if (a.team != b.team) return a.team < b.team;
        return a.name < b.name;
    };
    const std::size_t shown = std::min(limit, keys.size());
    std::partial_sort(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(shown), keys.end(), before);
    std::vector<TabEntry> out;
    out.reserve(shown);
    for (std::size_t i = 0; i < shown; ++i) out.push_back(*keys[i].entry);
    return out;
}

const TabEntry* MultiplayerClient::tabEntry(const Uuid& id) const {
    const auto it = tab_.find(id);
    return it == tab_.end() ? nullptr : &it->second;
}

StyledText MultiplayerClient::decoratedName(const Uuid& id) const {
    const TabEntry* e = tabEntry(id);
    if (e == nullptr) return {};
    const TeamInfo* team = teamOfPlayer(e->name);
    StyledText base = e->display.empty() ? StyledText{TextSpan{e->name}} : e->display;
    if (team == nullptr) return base;
    StyledText out = team->prefix;
    for (TextSpan span : base) {
        if (!span.hasColor && team->color >= 0 && team->color < 16) {
            span.rgb = formattingColor(team->color);
            span.hasColor = true;
        }
        out.push_back(std::move(span));
    }
    out.insert(out.end(), team->suffix.begin(), team->suffix.end());
    return out;
}

bool MultiplayerClient::nameTagVisible(const Uuid& id) const {
    const TabEntry* e = tabEntry(id);
    if (e == nullptr) return true;
    const TabEntry* self = tabEntry(selfId_);
    const TeamInfo* team = teamOfPlayer(e->name);
    const TeamInfo* own = self != nullptr ? teamOfPlayer(self->name) : nullptr;
    if (team == nullptr) return true;
    if (team->nameTagVisibility == "never") return false;
    if (team->nameTagVisibility == "hideForOwnTeam") return own != team;
    if (team->nameTagVisibility == "hideForOtherTeams") return own == team;
    return true;
}

void MultiplayerClient::clearWorld(RemoteHandler& h) {
    players_.clear();
    items_.clear();
    passengers_.clear();
    vehicle_ = -1;
    controlledEntity_ = -1;
    if (h.newWorld) h.newWorld();
}

void MultiplayerClient::injectPlayPacket(int id, const Bytes& body, RemoteHandler& h) {
    phase_ = Phase::Play;
    Packet p;
    p.id = id;
    p.body = body;
    handlePlay(p, h);
}

void MultiplayerClient::tickPlayers() {
    for (auto& [id, item] : items_) {
        item.xo = item.x;
        item.yo = item.y;
        item.zo = item.z;
        ++item.age;
    }
    for (auto& [id, p] : players_) {
        if (id == controlledEntity_) continue;  // (its last place is kept by the one who moves it)
        p.yawO = p.yaw;
        p.pitchO = p.pitch;
        p.headYawO = p.headYaw;
        p.bodyYawO = p.bodyYaw;
        p.xo = p.x;
        p.yo = p.y;
        p.zo = p.z;
        const auto wrap = [](float degrees) {
            float d = std::fmod(degrees, 360.0f);
            if (d >= 180.0f) d -= 360.0f;
            if (d < -180.0f) d += 360.0f;
            return d;
        };
        if (p.lerpSteps > 0 && projectilePhysics_.count(p.type) == 0) {  // InterpolationHandler.interpolate: a step of what is left of the way to where the server has it
            const double part = 1.0 / static_cast<double>(p.lerpSteps);
            p.x += (p.sx - p.x) * part;
            p.y += (p.sy - p.y) * part;
            p.z += (p.sz - p.z) * part;
            p.yaw += wrap(p.syaw - p.yaw) * static_cast<float>(part);
            p.pitch += (p.spitch - p.pitch) * static_cast<float>(part);
            --p.lerpSteps;
        }
        if (p.headSteps > 0) {
            p.headYaw += wrap(p.sheadYaw - p.headYaw) / static_cast<float>(p.headSteps);
            --p.headSteps;
        }
        const double dx = p.x - p.xo, dy = p.y - p.yo, dz = p.z - p.zo;
        p.deltaXO = p.deltaX;
        p.deltaYO = p.deltaY;
        p.deltaZO = p.deltaZ;
        p.deltaX = dx;
        p.deltaY = dy;
        p.deltaZ = dz;
        const bool player = isPlayerEntity(p);
        p.usingTicks = (p.livingFlags & 1) != 0 ? p.usingTicks + 1 : 0;
        if (player) p.cloak.tick(p.x, p.y, p.z, dx, dz, p.onGround && (p.entityFlags & 0x80) == 0 && p.pose != 3);  // ClientAvatarState.tick: the cape
        // LivingEntity.updateSwingTime: a swing takes six ticks
        p.swingO = p.swing;
        if (p.swinging) {
            if (++p.swingTicks >= 6) {
                p.swingTicks = 0;
                p.swinging = false;
            }
        } else {
            p.swingTicks = 0;
        }
        p.swing = static_cast<float>(p.swingTicks) / 6.0f;
        if (player) {  // LivingEntity.tick and tickHeadTurn: the body turns toward the way it walks (backwards when the walk is more against the look than with it),
                       // at three tenths of the way a tick, and never more than 50 degrees from where they look; an arm swing turns it to the look
            float target = p.bodyYaw;
            if (static_cast<float>(dx * dx + dz * dz) > 0.0025000002f) {
                const float walk = static_cast<float>(std::atan2(dz, dx)) * 57.295776f - 90.0f;
                const float apart = std::abs(wrap(p.yaw) - walk);
                target = 95.0f < apart && apart < 265.0f ? walk - 180.0f : walk;
            }
            if (p.swing > 0.0f) target = p.yaw;
            p.bodyYaw += wrap(target - p.bodyYaw) * 0.3f;
            const float off = wrap(p.yaw - p.bodyYaw);
            if (std::abs(off) > 50.0f) p.bodyYaw += off - (off > 0.0f ? 1.0f : off < 0.0f ? -1.0f : 0.0f) * 50.0f;
            // Player.updateSwimAmount: into the swimming pose and out of it at 0.09 a tick
            p.swimAmountO = p.swimAmount;
            p.swimAmount = p.pose == 3 ? std::min(1.0f, p.swimAmount + 0.09f) : std::max(0.0f, p.swimAmount - 0.09f);
            const bool gliding = (p.entityFlags & 0x80) != 0;
            p.fallFlyingTicks = gliding ? p.fallFlyingTicks + 1 : 0;
            // ElytraAnimationState.tick: the wings go toward where they are held (folded, spread by the dive, or tucked in when crouching) by three tenths a tick
            p.elytraXO = p.elytraX;
            p.elytraYO = p.elytraY;
            p.elytraZO = p.elytraZ;
            float targetX = 0.2617994f, targetZ = -0.2617994f, targetY = 0.0f;
            if (gliding) {
                float spread = 1.0f;
                const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dy < 0.0 && length > 0.0) spread = 1.0f - static_cast<float>(std::pow(-dy / length, 1.5));
                targetX = spread * 0.34906584f + (1.0f - spread) * targetX;
                targetZ = spread * -1.5707964f + (1.0f - spread) * targetZ;
            } else if (p.sneaking) {
                targetX = 0.6981317f;
                targetZ = -0.7853982f;
                targetY = 0.08726646f;
            }
            p.elytraX += (targetX - p.elytraX) * 0.3f;
            p.elytraY += (targetY - p.elytraY) * 0.3f;
            p.elytraZ += (targetZ - p.elytraZ) * 0.3f;
        }
        if (p.hurtTime > 0) --p.hurtTime;
        if (p.dying) ++p.deathTime;
        // LivingEntity.calculateEntityAnimation: how far it walked this tick sets how hard the legs swing
        p.walkSpeedO = p.walkSpeed;
        p.walkSpeed += (std::min(static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 4.0f, 1.0f) - p.walkSpeed) * 0.4f;
        p.walkPosition += p.walkSpeed;
    }
    // Entity.positionRider for what rides something (the server does not say where a passenger is; it follows its mount): a place on a boat, a minecart or a mount, from the table of
    // the game's passenger attachment points, less the rider's own 0.6, turned with the mount.
    for (auto& [id, p] : players_) p.vehicle = -1;
    for (const auto& [vehicleId, riders] : passengers_) {
        const auto mount = players_.find(vehicleId);
        if (mount == players_.end()) continue;
        const RemotePlayer& v = mount->second;
        const std::string* typeName = mobName(v.type);
        const std::string name = typeName != nullptr ? *typeName : std::string();
        const auto ends = [&name](const char* tail) {
            const std::string t = tail;
            return name.size() >= t.size() && name.compare(name.size() - t.size(), t.size(), t) == 0;
        };
        const bool boat = ends("_boat") || ends("_raft"), raft = ends("_raft");
        const MountSeat* table = nullptr;
        for (const MountSeat& s : kMountSeats) {
            if (name == s.type) table = &s;
        }
        for (std::size_t i = 0; i < riders.size(); ++i) {
            const auto found = players_.find(riders[i]);
            if (found == players_.end() || riders[i] == controlledEntity_) continue;
            RemotePlayer& rider = found->second;
            double sx = v.x, sy = v.y, sz = v.z;
            if (boat) {
                BoatBody body;
                body.x = v.x;
                body.y = v.y;
                body.z = v.z;
                body.yaw = v.yaw;
                const std::array<double, 3> at = body.seat(raft, static_cast<int>(i), static_cast<int>(riders.size()));
                sx = at[0];
                sy = at[1];
                sz = at[2];
            } else {
                float ax = 0.0f, ay = 0.5f, az = 0.0f;  // (a mount that is not in the table: about at its middle)
                if (table != nullptr) {
                    const int slot = std::min(static_cast<int>(i), std::max(0, table->riders - 1));
                    ax = table->seat[slot][0];
                    ay = table->seat[slot][1];
                    az = table->seat[slot][2];
                }
                const double angle = static_cast<double>(-v.yaw * 0.017453292f);  // Vec3.yRot
                sx = v.x + ax * std::cos(angle) + az * std::sin(angle);
                sy = v.y + ay - 0.6;
                sz = v.z + az * std::cos(angle) - ax * std::sin(angle);
            }
            rider.x = rider.sx = sx;
            rider.y = rider.sy = sy;
            rider.z = rider.sz = sz;
            rider.lerpSteps = 0;
            rider.vehicle = vehicleId;
            rider.seatIndex = static_cast<int>(i);
        }
    }
    for (auto& [id, e] : players_) {  // what flies goes by its velocity (Projectile.tick without the world: no collisions, the server corrects the place)
        const auto physics = projectilePhysics_.find(e.type);
        if (physics == projectilePhysics_.end()) continue;
        if (e.vx == 0.0 && e.vy == 0.0 && e.vz == 0.0) continue;
        e.x += e.vx;
        e.y += e.vy;
        e.z += e.vz;
        e.vx *= static_cast<double>(physics->second.second);
        e.vy = e.vy * static_cast<double>(physics->second.second) - static_cast<double>(physics->second.first);
        e.vz *= static_cast<double>(physics->second.second);
        const double horizontal = std::sqrt(e.vx * e.vx + e.vz * e.vz);  // it points the way it goes
        e.yaw = static_cast<float>(std::atan2(e.vx, e.vz) * 57.29577951308232);
        e.pitch = static_cast<float>(std::atan2(e.vy, horizontal) * 57.29577951308232);
    }
}

void MultiplayerClient::handlePlay(const Packet& p, RemoteHandler& h) {
    Reader r(p.body);
    switch (p.id) {
        case 50: {  // login: our entity and the world we are in. A login after the first (a proxy that moves the player to another server) is a new world.
            selfEntity_ = r.i32();
            r.boolean();  // hardcore
            const int levels = r.varint();
            for (int i = 0; i < levels && r.ok && i < 1024; ++i) (void)r.string();
            r.varint();   // the most players
            r.varint();   // the chunk radius
            r.varint();   // the simulation distance
            r.boolean();
            r.boolean();
            r.boolean();
            r.varint();   // the dimension type
            const std::string dimension = r.string();
            if (r.ok) dimension_ = dimension;
            clearWorld(h);
            break;
        }
        case 84: {  // respawn: into another dimension is another world (the game then makes a new level); in the same one (after a death) it is not
            r.varint();
            const std::string dimension = r.string();
            if (r.ok && dimension != dimension_) {
                dimension_ = dimension;
                clearWorld(h);
            }
            break;
        }
        case 32: fail("disconnected: " + plainText(readComponent(r)), &h); break;
        case 120: {  // start_configuration: back to the configuration phase
            connection_.send(16, {});  // configuration_acknowledged
            phase_ = Phase::Configuration;
            clearWorld(h);
            break;
        }
        case 45: {  // keep_alive
            Writer w;
            w.i64(r.i64());
            connection_.send(28, w.data);
            break;
        }
        case 62: {  // ping
            Writer w;
            w.i32(r.i32());
            connection_.send(45, w.data);
            break;
        }
        case 73: {  // player_position: the server puts the player somewhere
            const int id = r.varint();
            PositionMoveRotation m = readPositionMoveRotation(r);
            const int relative = r.i32();
            if (!r.ok) break;
            if (h.teleport) h.teleport(m.x, m.y, m.z, m.dx, m.dy, m.dz, m.yaw, m.pitch, relative);
            // accept_teleportation: since 26.x it says where the player is now (the server compares it with what it asked for).
            const MoveState now = h.self ? h.self() : MoveState{m.x, m.y, m.z, m.yaw, m.pitch, false, false};
            Writer ack;
            ack.varint(id);
            ack.f64(now.x);
            ack.f64(now.y);
            ack.f64(now.z);
            ack.f32(now.yaw);
            ack.f32(now.pitch);
            connection_.send(0, ack.data);
            last_ = now;
            haveLast_ = true;
            positionReminder_ = 0;
            break;
        }
        case 74: {  // player_rotation: the server turns the player (a teleport that only changes the angles, a forced look)
            const float yaw = r.f32();
            const bool relativeY = r.boolean();
            const float pitch = r.f32();
            const bool relativeX = r.boolean();
            if (!r.ok) break;
            // ClientPacketListener.handleRotatePlayer: PositionMoveRotation.calculateAbsolute adds an angle to the player's own; the pitch is then clamped.
            const MoveState now = h.self ? h.self() : MoveState{};
            const float y = relativeY ? now.yaw + yaw : yaw;
            const float x = std::clamp(relativeX ? now.pitch + pitch : pitch, -90.0f, 90.0f);
            if (h.rotate) h.rotate(y, x);
            sendRotation(y, x, false, false);  // (the handler sends the same move packet back, as the game does)
            break;
        }
        case 12: chunkBatchOpen_ = true; break;
        case 11: {  // chunk_batch_finished: say how many chunks a tick are welcome
            r.varint();
            Writer w;
            w.f32(20.0f);
            connection_.send(11, w.data);
            chunkBatchOpen_ = false;
            break;
        }
        case 46: {  // level_chunk_with_light
            const int cx = r.i32(), cz = r.i32();
            std::string error;
            if (auto chunk = decodeChunk(r, static_cast<int>(registry_.stateCount()), &biomeIds_, error)) {
                // What is left is the block entities (not followed yet) and then the light. The light is the server's own - the light of every block it has changed, of
                // torches and lava and of what the other players have built - so it is not worked out again here.
                if (skipBlockEntities(r)) {
                    ChunkLightPatch patch;
                    if (readLight(r, patch)) {
                        auto light = std::make_shared<ChunkLight>();
                        patch.applyTo(*light);
                        chunk->light = std::move(light);
                    }
                }
                if (h.chunk) h.chunk({cx, cz}, std::move(chunk));
            } else {
                falcon::log::line("chunk %d, %d: %s\n", cx, cz, error.c_str());
            }
            break;
        }
        case 49: {  // light_update: the light of the sections a block was placed in or broken out of, so it is the server's light and not one worked out again
            const ChunkPos pos{r.varint(), r.varint()};
            ChunkLightPatch patch;
            if (readLight(r, patch) && !patch.empty() && h.light) h.light(pos, patch);
            break;
        }
        case 38: {  // forget_level_chunk
            const std::int64_t v = r.i64();
            if (h.forget) h.forget({static_cast<int>(static_cast<std::int32_t>(v & 0xFFFFFFFF)), static_cast<int>(static_cast<std::int32_t>(v >> 32))});
            break;
        }
        case 8: {  // block_update
            int x, y, z;
            unpackBlockPos(r.i64(), x, y, z);
            const int state = r.varint();
            if (r.ok && h.block) h.block(x, y, z, static_cast<Block>(state));
            break;
        }
        case 86: {  // section_blocks_update
            const std::int64_t section = r.i64();
            const int sx = static_cast<int>(section >> 42), sy = static_cast<int>(section << 44 >> 44), sz = static_cast<int>(section << 22 >> 42);
            const int n = r.varint();
            for (int i = 0; i < n && r.ok; ++i) {
                const std::int64_t entry = r.varlong();
                const int state = static_cast<int>(entry >> 12);
                const int lx = static_cast<int>((entry >> 8) & 15), lz = static_cast<int>((entry >> 4) & 15), ly = static_cast<int>(entry & 15);
                if (h.block) h.block(sx * 16 + lx, sy * 16 + ly, sz * 16 + lz, static_cast<Block>(state));
            }
            break;
        }
        case 4: break;  // block_changed_ack
        case 16: {      // commands: the command tree (only to know which arguments are messages)
            if (!commands_.read(r)) falcon::log::line("the server's command tree could not be read: commands with messages go unsigned\n");
            break;
        }
        case 18: {      // container_set_content: only the player's own inventory (container 0) is followed
            const int container = r.varint();
            containerStates_[container] = r.varint();
            const int n = r.varint();
            bool known = true;
            std::vector<ItemStack> items;
            for (int i = 0; i < n && r.ok && known; ++i) {
                const ItemStack stack = readItemStack(r, componentIds_, known);
                if (container == 0) {
                    const int slot = inventorySlotOfMenu(i);
                    if (slot >= 0 && h.inventorySlot) h.inventorySlot(slot, stack);
                } else {
                    items.push_back(stack);
                }
            }
            if (container != 0 && r.ok && h.containerContent) {
                ItemStack carried;
                if (known) carried = readItemStack(r, componentIds_, known);
                h.containerContent(container, items, carried);
            }
            break;
        }
        case 20: {  // container_set_slot
            const int container = r.varint();
            containerStates_[container] = r.varint();
            const int menuSlot = r.i16();
            bool known = true;
            const ItemStack stack = readItemStack(r, componentIds_, known);
            const int slot = inventorySlotOfMenu(menuSlot);
            if (r.ok && container == 0 && slot >= 0 && h.inventorySlot) h.inventorySlot(slot, stack);
            if (r.ok && container > 0 && h.containerSlot) h.containerSlot(container, menuSlot, stack);
            break;
        }
        case 110: {  // set_player_inventory
            const int slot = r.varint();
            bool known = true;
            const ItemStack stack = readItemStack(r, componentIds_, known);
            if (r.ok && slot >= 0 && slot <= 40 && h.inventorySlot) h.inventorySlot(slot, stack);
            break;
        }
        case 107: {  // set_held_slot
            const int slot = r.varint();
            if (r.ok && slot >= 0 && slot < 9 && h.heldSlot) h.heldSlot(slot);
            break;
        }
        case 119: {  // sound: an event at a place (the position is in eighths of a block)
            const int holder = r.varint();
            std::string name;
            if (holder == 0) {  // a sound that is not in the registry: written out
                name = r.string();
                if (r.boolean()) r.f32();
                if (name.compare(0, 10, "minecraft:") == 0) name.erase(0, 10);
            } else if (static_cast<std::size_t>(holder - 1) < soundNames_.size()) {
                name = soundNames_[static_cast<std::size_t>(holder - 1)];
            }
            const int source = r.varint();
            const double x = r.i32() / 8.0, y = r.i32() / 8.0, z = r.i32() / 8.0;
            const float volume = r.f32(), pitch = r.f32();
            r.i64();  // the seed
            if (r.ok && !name.empty() && h.sound) h.sound(name, source, x, y, z, volume, pitch);
            break;
        }
        case 47: {  // level_event
            const int type = r.i32();
            int x, y, z;
            unpackBlockPos(r.i64(), x, y, z);
            const int data = r.i32();
            if (r.ok && h.levelEvent) h.levelEvent(type, x, y, z, data);
            break;
        }
        case 60: {  // open_screen: a chest, a furnace ...: the id of the container, the number of its menu type, its title
            const int container = r.varint();
            const int type = r.varint();
            const StyledText title = readComponent(r);
            if (!r.ok) break;
            if (h.openContainer) h.openContainer(container, type, title);
            else sendContainerClose(container);
            break;
        }
        case 53: {  // merchant_offers: what a villager trades (the offers, its level and experience)
            TradeOffers offers;
            offers.containerId = r.varint();
            const int n = r.varint();
            bool whole = r.ok && n >= 0 && n < 4096;
            for (int i = 0; i < n && whole && r.ok; ++i) {
                TradeOffer offer;
                if (readTradeOffer(r, componentIds_, offer)) offers.offers.push_back(offer);
                else whole = false;  // one that cannot be read: the rest cannot be told apart, the ones before it are shown
            }
            if (whole && r.ok) {
                offers.level = r.varint();
                offers.xp = r.varint();
                offers.showProgress = r.boolean();
                offers.canRestock = r.boolean();
            }
            if (h.merchantOffers) h.merchantOffers(offers);
            break;
        }
        case 17: {  // container_close: the server closes what it opened
            const int container = r.varint();
            if (r.ok && container > 0 && h.containerClose) h.containerClose(container);
            break;
        }
        case 19: {  // container_set_data: a number a menu shows (a furnace's fire and progress)
            const int container = r.varint();
            const int index = r.i16();
            const int value = r.i16();
            if (r.ok && container > 0 && h.containerData) h.containerData(container, index, value);
            break;
        }
        case 98: {  // set_cursor_item: what the server says the cursor holds
            bool known = true;
            const ItemStack stack = readItemStack(r, componentIds_, known);
            if (r.ok && h.cursor) h.cursor(stack);
            break;
        }
        case 101: {  // set_entity_data: entries of (index, kind of value, value) up to 0xFF. What is kept: the flags and the pose of an entity (a player sneaks, glides, swims), what a living
                     // one is doing (using an item), a player's skin parts and main arm, an armor stand's flags and poses, an item frame's rotation, the stack of an item or a frame
            const int entity = r.varint();
            const auto found = players_.find(entity);
            RemotePlayer* who = found != players_.end() ? &found->second : nullptr;
            while (r.ok) {
                const int index = r.u8();
                if (index == 0xFF || !r.ok) break;
                const int serializer = r.varint();
                if (serializer == 0) {  // a byte
                    const int value = r.u8();
                    if (!r.ok) break;
                    if (index == 0) {
                        if (who != nullptr) {
                            who->entityFlags = value;
                            who->sneaking = (value & 2) != 0 || who->pose == 5;
                        }
                        if (entity == selfEntity_ && h.selfFlags) h.selfFlags(value);
                    } else if (index == 8 && who != nullptr) {
                        who->livingFlags = value;
                    } else if (index == 15 && who != nullptr) {  // an armor stand's flags: small, has arms, no base plate, marker
                        who->standFlags = value;
                    } else if (index == 16 && who != nullptr && isPlayerEntity(*who)) {
                        who->skinParts = value;
                    }
                } else if (serializer == 20) {  // the pose
                    const int pose = r.varint();
                    if (who != nullptr && r.ok && index == 6) {
                        who->pose = pose;
                        who->sneaking = (who->entityFlags & 2) != 0 || pose == 5;
                    }
                } else if (serializer == 11 && index == 14) {  // the position of the bed a living thing sleeps in (optional)
                    const bool present = r.boolean();
                    int bx = 0, by = 0, bz = 0;
                    if (present) unpackBlockPos(r.i64(), bx, by, bz);
                    if (who != nullptr && r.ok) {
                        who->hasSleepPos = present;
                        who->sleepX = bx;
                        who->sleepY = by;
                        who->sleepZ = bz;
                    }
                } else if (serializer == 42) {  // the main arm: 0 is the left
                    const int arm = r.varint();
                    if (who != nullptr && r.ok && index == 15) who->mainArmLeft = arm == 0;
                } else if (serializer == 1 && index == 9) {  // an item frame's rotation
                    const int rotation = r.varint();
                    if (who != nullptr && r.ok) who->frameRotation = rotation;
                } else if (serializer == 9 && index >= 16 && index <= 21) {  // the pose of a part of an armor stand: three angles in degrees
                    const float x = r.f32(), y = r.f32(), z = r.f32();
                    if (who != nullptr && r.ok) who->standPose[static_cast<std::size_t>(index - 16)] = {x, y, z};
                } else if (serializer == 34) {  // a painting's variant: a holder of the registry (its number plus one; 0 would be the data written out, which is not read)
                    const int holder = r.varint();
                    if (who != nullptr && r.ok && holder > 0) who->variant = holder - 1;
                } else if (serializer == 7) {  // an item stack (the item of an item entity is entry 8)
                    bool known = true;
                    const ItemStack stack = readItemStack(r, componentIds_, known);
                    if (const auto item = items_.find(entity); item != items_.end() && index == 8 && r.ok) item->second.stack = stack;
                    if (who != nullptr && index == 8 && r.ok) who->stack = stack;  // (an item frame's)
                    if (!known) break;
                } else if (!skipDataValue(serializer, r)) {
                    break;  // a kind of value whose layout is not known here: the rest of the entries cannot be told apart
                }
            }
            break;
        }
        case 124: {     // system_chat
            const StyledText text = readComponent(r);
            const bool overlay = r.boolean();
            if (h.chat) h.chat(text, overlay);
            break;
        }
        case 33: {  // disguised_chat: the message and who it is from
            const StyledText text = readComponent(r);
            if (h.chat) h.chat(text, false);
            break;
        }
        case 66: {  // player_chat (the signature parts are skipped; the text is the message)
            const Uuid sender = r.uuid();
            r.varint();
            if (r.boolean()) {  // a signed message: it counts as seen, and the server hears of it in the next message or, after sixty-four, on its own (ServerboundChatAckPacket)
                MessageSignature signature{};
                for (std::uint8_t& b : signature) b = static_cast<std::uint8_t>(r.u8());
                if (r.ok && lastSeen_.addPending(signature) && lastSeen_.offset() > LastSeenTracker::kReportAfter) {
                    Writer ack;
                    ack.varint(lastSeen_.takeOffset());
                    connection_.send(6, ack.data);
                }
            }
            const std::string content = r.string();
            StyledText line;
            const TabEntry* who = tabEntry(sender);
            line.push_back(TextSpan{"<"});
            for (TextSpan& span : decoratedName(sender)) line.push_back(std::move(span));
            if (who == nullptr) line.push_back(TextSpan{"?"});
            line.push_back(TextSpan{"> " + content});
            if (r.ok && h.chat) h.chat(line, false);
            break;
        }
        case 106: {  // set_health
            const float health = r.f32();
            const int food = r.varint();
            const float saturation = r.f32();
            if (r.ok && h.health) h.health(health, food, saturation);
            break;
        }
        case 105: {  // set_experience
            const float progress = r.f32();
            const int level = r.varint();
            if (r.ok && h.experience) h.experience(progress, level);
            break;
        }
        case 39: {  // game_event: 3 changes the game mode
            const int event = r.u8();
            const float value = r.f32();
            if (event == 3 && h.gameMode) h.gameMode(static_cast<int>(value));
            break;
        }
        case 65: {  // player_abilities
            const int flags = r.u8();
            if (h.abilities) h.abilities((flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0, (flags & 1) != 0);
            break;
        }
        case 71: {  // player_info_update
            const int actions = r.u8();
            const int n = r.varint();
            for (int i = 0; i < n && r.ok; ++i) {
                const Uuid id = r.uuid();
                TabEntry& e = tab_[id];
                e.id = id;
                if (actions & 1) {  // add player: name and properties
                    e.name = r.string();
                    const int props = r.varint();
                    for (int k = 0; k < props && r.ok; ++k) {
                        const std::string property = r.string();
                        const std::string value = r.string();
                        if (r.boolean()) (void)r.string();  // (the signature)
                        if (property == "textures" && r.ok) {  // the skin: where it is, and which model it is for
                            if (const auto skin = parseTexturesProperty(value)) {
                                skinInfos_[id] = *skin;
                                skins_.request(*skin);
                            }
                        }
                    }
                }
                if (actions & 2) {  // chat session
                    if (r.boolean()) {
                        r.skip(16);
                        r.i64();
                        r.skip(static_cast<std::size_t>(std::max(0, r.varint())));
                        r.skip(static_cast<std::size_t>(std::max(0, r.varint())));
                    }
                }
                if (actions & 4) e.gameMode = r.varint();
                if (actions & 8) e.listed = r.boolean();
                if (actions & 16) e.latency = r.varint();
                if (actions & 32) {
                    e.display = r.boolean() ? readComponent(r) : StyledText{};
                }
                if (actions & 64) e.order = r.varint();
                if (actions & 128) e.hat = r.boolean();  // the hat
            }
            break;
        }
        case 70: {  // player_info_remove
            const int n = r.varint();
            for (int i = 0; i < n && r.ok; ++i) tab_.erase(r.uuid());
            break;
        }
        case 125: {  // tab_list: the header and the footer
            tabHeader_ = readComponent(r);
            tabFooter_ = readComponent(r);
            break;
        }
        case 111: {  // set_player_team
            memberIndexDirty_ = true;
            const std::string name = r.string();
            const int method = r.u8();
            TeamInfo& t = teams_[name];
            t.name = name;
            if (method == 1) {
                teams_.erase(name);
                break;
            }
            if (method == 0 || method == 2) {
                // The parameters, in the order of the 26.3 codec: the display name, the prefix and the suffix, how the name tags show and how bodies collide (both by
                // number), the colour (there or not), and the flags.
                (void)readComponent(r);
                t.prefix = readComponent(r);
                t.suffix = readComponent(r);
                static const char* const kVisibility[4] = {"always", "never", "hideForOtherTeams", "hideForOwnTeam"};
                const int visibility = r.varint();
                t.nameTagVisibility = visibility >= 0 && visibility < 4 ? kVisibility[visibility] : "always";
                r.varint();  // collision rule
                const bool hasColor = r.boolean();
                const int color = hasColor ? r.varint() : -1;
                t.color = color >= 0 && color < 16 ? color : -1;
                r.u8();  // friendly fire flags
            }
            if (method == 0 || method == 3 || method == 4) {
                const int n = r.varint();
                for (int i = 0; i < n && r.ok; ++i) {
                    const std::string member = r.string();
                    if (method == 4) t.members.erase(member);
                    else t.members.insert(member);
                }
            }
            break;
        }
        case 1: {  // add_entity: only players are followed
            const int id = r.varint();
            const Uuid uuid = r.uuid();
            const int type = r.varint();
            RemotePlayer pl;
            pl.entityId = id;
            pl.id = uuid;
            pl.x = pl.xo = r.f64();
            pl.y = pl.yo = r.f64();
            pl.z = pl.zo = r.f64();
            readLpVec3(r, pl.vx, pl.vy, pl.vz);
            pl.pitch = pl.pitchO = angleOf(r.i8());
            pl.yaw = pl.yawO = angleOf(r.i8());
            pl.headYaw = pl.headYawO = angleOf(r.i8());
            pl.data = r.varint();
            pl.type = type;
            pl.sx = pl.x;
            pl.sy = pl.y;
            pl.sz = pl.z;
            pl.syaw = pl.yaw;
            pl.spitch = pl.pitch;
            pl.sheadYaw = pl.headYaw;
            pl.bodyYaw = pl.bodyYawO = pl.headYaw;  // (handleAddEntity: the body starts facing the way the head does)
            if (r.ok && type == playerType_ && id != selfEntity_) players_[id] = pl;
            else if (r.ok && mobTypes_.count(type) != 0) players_[id] = pl;  // a mob that can be drawn: followed the same way
            if (r.ok && type == itemType_) {
                RemoteItem item;
                item.entityId = id;
                item.x = item.xo = pl.x;
                item.y = item.yo = pl.y;
                item.z = item.zo = pl.z;
                items_[id] = item;
            }
            break;
        }
        case 109: {  // set_passengers: who rides a vehicle (the game ejects all of them and has the listed ones start riding it again)
            const int vehicle = r.varint();
            const int n = r.varint();
            std::vector<int> riders;
            for (int i = 0; i < n && r.ok && i < 64; ++i) riders.push_back(r.varint());
            if (!r.ok) break;
            const bool mine = std::find(riders.begin(), riders.end(), selfEntity_) != riders.end();
            for (auto it = passengers_.begin(); it != passengers_.end();) {  // (Entity.startRiding: whoever boards this vehicle is off the one it sat on before)
                if (it->first != vehicle) {
                    auto& list = it->second;
                    list.erase(std::remove_if(list.begin(), list.end(), [&riders](int e) { return std::find(riders.begin(), riders.end(), e) != riders.end(); }), list.end());
                    if (list.empty()) {
                        it = passengers_.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            if (riders.empty()) passengers_.erase(vehicle);
            else passengers_[vehicle] = riders;
            if (mine) vehicle_ = vehicle;
            else if (vehicle_ == vehicle) vehicle_ = -1;
            break;
        }
        case 58: {  // move_vehicle: the server sets the vehicle the player drives
            const double x = r.f64(), y = r.f64(), z = r.f64();
            const float yaw = r.f32(), pitch = r.f32();
            if (r.ok && h.moveVehicle) h.moveVehicle(x, y, z, yaw, pitch);
            break;
        }
        case 78: {  // remove_entities
            const int n = r.varint();
            for (int i = 0; i < n && r.ok; ++i) {
                const int gone = r.varint();
                players_.erase(gone);
                items_.erase(gone);
                passengers_.erase(gone);
                if (vehicle_ == gone) vehicle_ = -1;
            }
            break;
        }
        case 103: {  // set_entity_motion: the velocity of an entity
            const int id = r.varint();
            double vx, vy, vz;
            readLpVec3(r, vx, vy, vz);
            if (const auto it = players_.find(id); it != players_.end() && r.ok) {
                it->second.vx = vx;
                it->second.vy = vy;
                it->second.vz = vz;
            }
            break;
        }
        case 127: {  // take_item_entity: an item was picked up (by anyone): it is gone
            const int item = r.varint();
            if (r.ok) items_.erase(item);
            break;
        }
        case 54:
        case 55:
        case 57: {  // move_entity_pos, move_entity_pos_rot, move_entity_rot
            const int id = r.varint();
            const int properties = r.varint();  // the ticks this move is spread over, and bit 0 for whether it is standing on the ground
            const int steps = properties >> 1;
            const auto it = id == controlledEntity_ ? players_.end() : players_.find(id);  // (the one this machine drives is not moved by the server)
            const auto item = items_.find(id);
            const bool flying = it != players_.end() && projectilePhysics_.count(it->second.type) != 0;  // (what flies is moved by its own velocity: the server's word is taken at once)
            if (p.id != 57) {
                // Either one move of a tick (three shorts) or a step for each tick of a longer one: a step is a tick and three shorts, and the last step is where it is now.
                double dx = 0.0, dy = 0.0, dz = 0.0;
                for (int i = 0; i < (steps == 0 ? 1 : steps) && r.ok; ++i) {
                    if (steps != 0) r.varint();  // the tick this step lands on
                    dx = r.i16() / 4096.0;
                    dy = r.i16() / 4096.0;
                    dz = r.i16() / 4096.0;
                }
                if (it != players_.end() && r.ok) {
                    it->second.sx += dx;  // (relative to where the server had it, not to where it is drawn)
                    it->second.sy += dy;
                    it->second.sz += dz;
                    it->second.lerpSteps = 3;
                    if (flying) {
                        it->second.x = it->second.sx;
                        it->second.y = it->second.sy;
                        it->second.z = it->second.sz;
                    }
                }
                if (item != items_.end() && r.ok) {
                    item->second.x += dx;
                    item->second.y += dy;
                    item->second.z += dz;
                }
            }
            if (p.id != 54) {
                const float yaw = angleOf(r.i8()), pitch = angleOf(r.i8());
                if (it != players_.end() && r.ok) {
                    it->second.syaw = yaw;
                    it->second.spitch = pitch;
                    it->second.lerpSteps = 3;
                    if (flying) {
                        it->second.yaw = yaw;
                        it->second.pitch = pitch;
                    }
                }
            }
            if (it != players_.end()) it->second.onGround = (properties & 1) != 0;
            if (item != items_.end()) item->second.onGround = (properties & 1) != 0;
            break;
        }
        case 128: {  // teleport_entity (entity_position_sync has its own kind of position: not followed)
            const int id = r.varint();
            const PositionMoveRotation m = readPositionMoveRotation(r);
            const auto it = id == controlledEntity_ ? players_.end() : players_.find(id);
            if (it != players_.end() && r.ok) {
                it->second.sx = m.x;
                it->second.sy = m.y;
                it->second.sz = m.z;
                it->second.syaw = m.yaw;
                it->second.spitch = m.pitch;
                it->second.lerpSteps = 3;
                if (projectilePhysics_.count(it->second.type) != 0) {
                    it->second.x = m.x;
                    it->second.y = m.y;
                    it->second.z = m.z;
                    it->second.yaw = m.yaw;
                    it->second.pitch = m.pitch;
                }
            }
            if (const auto item = items_.find(id); item != items_.end() && r.ok) {
                item->second.x = m.x;
                item->second.y = m.y;
                item->second.z = m.z;
            }
            break;
        }
        case 35: {  // entity_position_sync: where the server really has an entity (sent now and then, so that the small moves in between never drift)
            const int id = r.varint();
            // The position is one point or a path of them a tick apart (the last is where it is now); the angles are floats here, not bytes.
            PositionMoveRotation m{};
            const int kind = r.varint();
            if (kind == 0) {
                m.x = r.f64();
                m.y = r.f64();
                m.z = r.f64();
            } else {
                const int points = r.varint();
                for (int i = 0; i < points && r.ok; ++i) {
                    m.x = r.f64();
                    m.y = r.f64();
                    m.z = r.f64();
                    r.varint();  // the tick this point is for
                }
            }
            m.yaw = r.f32();
            m.pitch = r.f32();
            const bool onGround = r.boolean();
            if (!r.ok) break;
            if (const auto it = id == controlledEntity_ ? players_.end() : players_.find(id); it != players_.end()) {
                RemotePlayer& e = it->second;
                e.sx = m.x;
                e.sy = m.y;
                e.sz = m.z;
                e.syaw = m.yaw;
                e.spitch = m.pitch;
                e.lerpSteps = 3;
                e.onGround = onGround;
                if (projectilePhysics_.count(e.type) != 0) {
                    e.x = m.x;
                    e.y = m.y;
                    e.z = m.z;
                    e.yaw = m.yaw;
                    e.pitch = m.pitch;
                }
            }
            if (const auto item = items_.find(id); item != items_.end()) {
                item->second.x = m.x;
                item->second.y = m.y;
                item->second.z = m.z;
                item->second.onGround = onGround;
            }
            break;
        }
        case 34: {  // entity_event: 2 and its kin are a hit (the red flash), 3 is a death
            const int id = r.i32();
            const int status = static_cast<std::int8_t>(r.u8());
            if (const auto it = players_.find(id); it != players_.end() && r.ok) {
                if (status == 2 || status == 33 || status == 36 || status == 37 || status == 44) it->second.hurtTime = 10;
                if (status == 3) {
                    it->second.dying = true;
                    it->second.hurtTime = 10;
                }
            }
            break;
        }
        case 43: {  // hurt_animation
            const int id = r.varint();
            if (const auto it = players_.find(id); it != players_.end() && r.ok) it->second.hurtTime = 10;
            break;
        }
        case 25: {  // damage_event
            const int id = r.varint();
            if (const auto it = players_.find(id); it != players_.end() && r.ok) it->second.hurtTime = 10;
            break;
        }
        case 85: {  // rotate_head
            const int id = r.varint();
            const auto it = players_.find(id);
            const float yaw = angleOf(r.i8());
            if (it != players_.end() && r.ok) {
                it->second.sheadYaw = yaw;
                it->second.headSteps = 3;
            }
            break;
        }
        case 104: {  // set_equipment: what an entity holds and wears (slots in the game's order; the top bit says another follows)
            const int id = r.varint();
            const auto it = players_.find(id);
            for (int more = 1; more != 0 && r.ok;) {
                const int slotByte = r.u8();
                more = slotByte & 0x80;
                const int slot = slotByte & 0x7F;
                bool known = true;
                const ItemStack stack = readItemStack(r, componentIds_, known);
                if (!known) break;  // (a component that cannot be read: nothing more of the packet can be told apart)
                if (it != players_.end() && r.ok && slot >= 0 && slot < 6) it->second.equipment[static_cast<std::size_t>(slot)] = stack;
            }
            break;
        }
        case 2: {  // animate: an arm swing
            const int id = r.varint();
            const int action = r.u8();
            const auto it = players_.find(id);
            if (it != players_.end() && (action == 0 || action == 3)) {  // LivingEntity.swing: a swing that is past its middle (or none) starts over
                RemotePlayer& e = it->second;
                if (!e.swinging || e.swingTicks >= 3 || e.swingTicks < 0) {
                    e.swingTicks = -1;
                    e.swinging = true;
                    e.swingOffHand = action == 3;
                }
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Outgoing

void MultiplayerClient::sendMovement(const MoveState& s) {
    if (phase_ != Phase::Play) return;
    if (!haveLast_) {
        last_ = s;
        haveLast_ = true;
    }
    ++positionReminder_;
    const double dx = s.x - last_.x, dy = s.y - last_.y, dz = s.z - last_.z;
    const bool moved = dx * dx + dy * dy + dz * dz > 4.0E-8 || positionReminder_ >= 20;
    const bool rotated = s.yaw != last_.yaw || s.pitch != last_.pitch;
    const int flags = (s.onGround ? 1 : 0) | (s.horizontalCollision ? 2 : 0);
    Writer w;
    if (moved && rotated) {
        w.f64(s.x); w.f64(s.y); w.f64(s.z); w.f32(s.yaw); w.f32(s.pitch); w.u8(flags);
        connection_.send(31, w.data);
    } else if (moved) {
        w.f64(s.x); w.f64(s.y); w.f64(s.z); w.u8(flags);
        connection_.send(30, w.data);
    } else if (rotated) {
        w.f32(s.yaw); w.f32(s.pitch); w.u8(flags);
        connection_.send(32, w.data);
    } else if (s.onGround != lastOnGround_ || s.horizontalCollision != lastHorizontal_) {
        w.u8(flags);
        connection_.send(33, w.data);
    }
    if (moved) {
        last_.x = s.x;
        last_.y = s.y;
        last_.z = s.z;
        positionReminder_ = 0;
    }
    if (rotated) {
        last_.yaw = s.yaw;
        last_.pitch = s.pitch;
    }
    lastOnGround_ = s.onGround;
    lastHorizontal_ = s.horizontalCollision;
}

void MultiplayerClient::sendRotation(float yaw, float pitch, bool onGround, bool horizontalCollision) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.f32(yaw);
    w.f32(pitch);
    w.u8((onGround ? 1 : 0) | (horizontalCollision ? 2 : 0));
    connection_.send(32, w.data);
    last_.yaw = yaw;  // (what a later movement packet compares with)
    last_.pitch = pitch;
}

void MultiplayerClient::sendMoveVehicle(double x, double y, double z, float yaw, float pitch, bool onGround) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.f64(x);
    w.f64(y);
    w.f64(z);
    w.f32(yaw);
    w.f32(pitch);
    w.boolean(onGround);
    connection_.send(34, w.data);
}

void MultiplayerClient::sendPaddleBoat(bool left, bool right) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.boolean(left);
    w.boolean(right);
    connection_.send(35, w.data);
}

void MultiplayerClient::sendInput(int flags) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.u8(flags);
    connection_.send(43, w.data);
}

void MultiplayerClient::sendSprint(bool on) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(selfEntity_);
    w.varint(on ? 1 : 2);
    w.varint(0);
    connection_.send(42, w.data);
}

void MultiplayerClient::sendFallFlying() {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(selfEntity_);
    w.varint(6);  // START_FALL_FLYING (after STOP_SLEEPING, START/STOP_SPRINTING, START/STOP_RIDING_JUMP, OPEN_INVENTORY)
    w.varint(0);
    connection_.send(42, w.data);
}

void MultiplayerClient::sendTickEnd() {
    if (phase_ == Phase::Play) connection_.send(13, {});
}

void MultiplayerClient::sendPlayerLoaded() {
    if (phase_ == Phase::Play) connection_.send(44, {});
}

void MultiplayerClient::sendChat(const std::string& text) {
    if (phase_ != Phase::Play) return;
    const std::int64_t millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::int64_t salt = 0;
    Bytes signature;
    const LastSeenTracker::Update seen = lastSeen_.generateAndApplyUpdate();  // (the messages of others that were seen: said now, and the signature covers them)
    if (chatSessionSent_ && haveChatKeys_) {  // SignedMessageLink and SignedMessageBody: what the signature covers, in the order the game feeds it
        std::uint8_t random[8];
        if (randomBytes(random, sizeof random)) {
            for (const std::uint8_t b : random) salt = (salt << 8) | b;
            const Bytes payload = chatSignaturePayload(selfId_, chatSessionId_, chatIndex_, salt, millis / 1000, text, seen.seen);
            std::string error;
            if (rsaSign(chatKeys_.privateKey, payload, signature, error)) ++chatIndex_;
            else falcon::log::line("a chat message could not be signed: %s\n", error.c_str());
        }
    }
    Writer w;
    w.string(text);
    w.i64(millis);
    w.i64(signature.empty() ? 0 : salt);
    w.boolean(!signature.empty());
    if (!signature.empty()) w.raw(signature);  // 256 bytes
    w.varint(seen.offset);  // LastSeenMessages.Update: how many came since the last one,
    w.u8(static_cast<int>(seen.acknowledged & 0xFF));  // which of the twenty are seen (a bit each, the oldest first),
    w.u8(static_cast<int>((seen.acknowledged >> 8) & 0xFF));
    w.u8(static_cast<int>((seen.acknowledged >> 16) & 0xFF));
    w.u8(seen.checksum);  // and the checksum of those
    connection_.send(9, w.data);
}

// A command: when it has message arguments (/msg, /say, /me ...), and there is a chat session, they are signed the way a chat message is (ServerboundChatCommandSignedPacket: the
// command, the time, the salt, a signature for each message argument and which messages of others were seen); otherwise the command goes as it is.
void MultiplayerClient::sendCommand(const std::string& text) {
    if (phase_ != Phase::Play) return;
    const std::vector<SignableArgument> arguments = commands_.signableArguments(text);
    if (!arguments.empty() && chatSessionSent_ && haveChatKeys_) {
        const std::int64_t millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::uint8_t random[8];
        if (randomBytes(random, sizeof random)) {
            std::int64_t salt = 0;
            for (const std::uint8_t b : random) salt = (salt << 8) | b;
            const LastSeenTracker::Update seen = lastSeen_.generateAndApplyUpdate();
            std::vector<std::pair<std::string, Bytes>> signatures;
            int index = chatIndex_;
            bool signedAll = true;
            for (const SignableArgument& argument : arguments) {  // each message argument is a message of the chain
                Bytes signature;
                std::string error;
                const Bytes payload = chatSignaturePayload(selfId_, chatSessionId_, index, salt, millis / 1000, argument.value, seen.seen);
                if (!rsaSign(chatKeys_.privateKey, payload, signature, error)) {
                    falcon::log::line("a command could not be signed: %s\n", error.c_str());
                    signedAll = false;
                    break;
                }
                signatures.emplace_back(argument.name, std::move(signature));
                ++index;
            }
            if (signedAll) {
                chatIndex_ = index;
                Writer w;
                w.string(text);
                w.i64(millis);
                w.i64(salt);
                w.varint(static_cast<std::int32_t>(signatures.size()));
                for (const auto& [name, signature] : signatures) {
                    w.string(name);
                    w.raw(signature);
                }
                w.varint(seen.offset);
                w.u8(static_cast<int>(seen.acknowledged & 0xFF));
                w.u8(static_cast<int>((seen.acknowledged >> 8) & 0xFF));
                w.u8(static_cast<int>((seen.acknowledged >> 16) & 0xFF));
                w.u8(seen.checksum);
                connection_.send(8, w.data);
                return;
            }
        }
    }
    Writer w;
    w.string(text);
    connection_.send(7, w.data);
}

void MultiplayerClient::sendRespawn() {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(0);  // perform respawn
    connection_.send(12, w.data);
}

void MultiplayerClient::sendHeldSlot(int slot) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.i16(slot);
    connection_.send(54, w.data);
}

void MultiplayerClient::sendDig(int action, int x, int y, int z, int face) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(action);
    w.i64(packBlockPos(x, y, z));
    w.u8(kProtocolDirection[face % 6]);
    w.varint(++sequence_);
    connection_.send(41, w.data);
}

void MultiplayerClient::sendUseItemOn(int x, int y, int z, int face, double cx, double cy, double cz, bool offHand) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(offHand ? 1 : 0);  // 0 the main hand, 1 the offhand
    w.i64(packBlockPos(x, y, z));
    w.varint(kProtocolDirection[face % 6]);
    w.f32(static_cast<float>(cx));
    w.f32(static_cast<float>(cy));
    w.f32(static_cast<float>(cz));
    w.boolean(false);
    w.boolean(false);
    w.varint(++sequence_);
    connection_.send(66, w.data);
}

void MultiplayerClient::sendAttack(int entityId) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(entityId);
    connection_.send(1, w.data);
}

void MultiplayerClient::sendInteract(int entityId, double dx, double dy, double dz, bool secondary) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(entityId);
    w.varint(0);  // the main hand
    writeLpVec3(w, dx, dy, dz);
    w.boolean(secondary);
    connection_.send(26, w.data);
}

void MultiplayerClient::sendSwing() {
    if (phase_ != Phase::Play) return;
    connection_.send(46, {});  // ServerboundPunchPacket has no fields
}

void MultiplayerClient::sendCreativeSlot(int inventorySlot, const ItemStack& stack) {
    if (phase_ != Phase::Play) return;
    const int menuSlot = inventorySlot >= 0 && inventorySlot < 9 ? 36 + inventorySlot : inventorySlot < 36 ? inventorySlot : inventorySlot == 36 ? 8 : inventorySlot == 37 ? 7 : inventorySlot == 38 ? 6 : inventorySlot == 39 ? 5 : 45;
    Writer w;
    w.i16(menuSlot);
    writeItemStack(w, stack);
    connection_.send(57, w.data);
}

void MultiplayerClient::sendUseItem(float yaw, float pitch, bool offHand) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(offHand ? 1 : 0);  // 0 the main hand, 1 the offhand
    w.varint(++sequence_);
    w.f32(yaw);
    w.f32(pitch);
    connection_.send(67, w.data);
}

// ServerboundPlayerActionPacket for what has no block (drop, release use, swap hands): the position 0, the direction down, the sequence 0.
void MultiplayerClient::sendAction(int action) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(action);
    w.i64(packBlockPos(0, 0, 0));
    w.u8(0);
    w.varint(0);
    connection_.send(41, w.data);
}

// HashedStack: whether there is one, then its item, its count and the hashes of the components that differ from the item's own. The hashes are not worked out here (a
// stack with components is told without them and the server answers with what it really holds).
static void writeHashedStack(Writer& w, const ItemStack& stack) {
    if (stack.isEmpty()) {
        w.boolean(false);
        return;
    }
    w.boolean(true);
    w.varint(static_cast<int>(stack.item));
    w.varint(stack.count);
    w.varint(0);  // components added
    w.varint(0);  // components removed
}

void MultiplayerClient::sendContainerClick(int containerId, int slot, int button, int mode, const std::vector<std::pair<int, ItemStack>>& changed, const ItemStack& carried) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(containerId);
    w.varint(containerStates_[containerId]);
    w.i16(slot);
    w.u8(button);
    w.varint(mode);
    w.varint(static_cast<int>(changed.size()));
    for (const auto& [index, stack] : changed) {
        w.i16(index);
        writeHashedStack(w, stack);
    }
    writeHashedStack(w, carried);
    connection_.send(18, w.data);
}

void MultiplayerClient::sendContainerButton(int containerId, int button) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(containerId);
    w.varint(button);
    connection_.send(17, w.data);
}

void MultiplayerClient::sendSelectTrade(int index) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(index);
    connection_.send(52, w.data);
}

void MultiplayerClient::sendRenameItem(const std::string& name) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.string(name);
    connection_.send(49, w.data);
}

void MultiplayerClient::sendContainerClose(int containerId) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(containerId);
    connection_.send(19, w.data);
}

}  // namespace falcon::net
