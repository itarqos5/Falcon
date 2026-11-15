#include "falcon/net/multiplayer_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>

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

// An LpVec3 (the velocity of a new entity): one byte for nothing at all, else six bytes and maybe a VarInt more.
void skipLpVec3(Reader& r) {
    const int lowest = r.u8();
    if (lowest == 0) return;
    r.u8();
    r.i32();
    if ((lowest & 4) == 4) r.varint();
}

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

}  // namespace

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

MultiplayerClient::MultiplayerClient(const BlockRegistry& registry) : registry_(registry) {}
MultiplayerClient::~MultiplayerClient() { close(); }

void MultiplayerClient::close() {
    connection_.close();
    phase_ = Phase::Closed;
}

bool MultiplayerClient::start(const std::string& host, int port, const std::string& username, std::string& error) {
    username_ = username;
    if (!connection_.connect(host, port, error)) {
        phase_ = Phase::Closed;
        closeReason_ = error;
        return false;
    }
    // The id the server will call this player in a server that does not check accounts is made from the name; what is sent here is not looked at.
    for (std::size_t i = 0; i < selfId_.size(); ++i) selfId_[i] = static_cast<std::uint8_t>(0x46 + i * 7 + (i < username.size() ? username[i] : 0));
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
    Writer w;
    w.string("en_us");
    w.u8(8);        // the view distance asked for
    w.varint(0);    // chat shown
    w.boolean(true);
    w.u8(0x7F);     // every part of the skin
    w.varint(1);    // right hand
    w.boolean(false);
    w.boolean(true);
    w.varint(0);    // particles: all
    connection_.send(target, w.data);
}

void MultiplayerClient::pump(RemoteHandler& h) {
    Packet p;
    int budget = 4000;
    while (phase_ != Phase::Closed && budget-- > 0 && connection_.poll(p)) {
        switch (phase_) {
            case Phase::Login: handleLogin(p, h); break;
            case Phase::Configuration: handleConfiguration(p, h); break;
            case Phase::Play: handlePlay(p, h); break;
            default: break;
        }
    }
    if (phase_ != Phase::Closed && !connection_.open()) {
        Packet rest;
        if (!connection_.poll(rest)) fail(connection_.closedReason().empty() ? "the connection was closed" : connection_.closedReason(), &h);
    }
}

void MultiplayerClient::handleLogin(const Packet& p, RemoteHandler& h) {
    Reader r(p.body);
    switch (p.id) {
        case 0: fail("the server refused the login: " + r.string(), &h); break;  // (login_disconnect has its text as JSON)
        case 1: fail("this server checks accounts (online mode): Falcon can only join servers that do not, yet", &h); break;
        case 2: {  // login_finished: the profile (id, name, properties); then the configuration begins
            r.skip(16);
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

std::vector<TabEntry> MultiplayerClient::tabList() const {
    std::vector<const TabEntry*> list;
    for (const auto& [id, entry] : tab_) {
        if (entry.listed) list.push_back(&entry);
    }
    const auto teamOf = [this](const TabEntry& e) -> std::string {
        for (const auto& [name, team] : teams_) {
            if (team.members.count(e.name) != 0) return name;
        }
        return {};
    };
    const auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    // PlayerTabOverlay: the list order (higher first), spectators last, then by team, then by name.
    std::stable_sort(list.begin(), list.end(), [&](const TabEntry* a, const TabEntry* b) {
        if (a->order != b->order) return a->order > b->order;
        const int sa = a->gameMode == 3 ? 1 : 0, sb = b->gameMode == 3 ? 1 : 0;
        if (sa != sb) return sa < sb;
        const std::string ta = teamOf(*a), tb = teamOf(*b);
        if (ta != tb) return ta < tb;
        return lower(a->name) < lower(b->name);
    });
    std::vector<TabEntry> out;
    for (const TabEntry* e : list) out.push_back(*e);
    return out;
}

const TabEntry* MultiplayerClient::tabEntry(const Uuid& id) const {
    const auto it = tab_.find(id);
    return it == tab_.end() ? nullptr : &it->second;
}

StyledText MultiplayerClient::decoratedName(const Uuid& id) const {
    const TabEntry* e = tabEntry(id);
    if (e == nullptr) return {};
    const TeamInfo* team = nullptr;
    for (const auto& [name, t] : teams_) {
        if (t.members.count(e->name) != 0) team = &t;
    }
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
    const TeamInfo* team = nullptr;
    const TeamInfo* own = nullptr;
    const TabEntry* self = tabEntry(selfId_);
    for (const auto& [name, t] : teams_) {
        if (t.members.count(e->name) != 0) team = &t;
        if (self != nullptr && t.members.count(self->name) != 0) own = &t;
    }
    if (team == nullptr) return true;
    if (team->nameTagVisibility == "never") return false;
    if (team->nameTagVisibility == "hideForOwnTeam") return own != team;
    if (team->nameTagVisibility == "hideForOtherTeams") return own == team;
    return true;
}

void MultiplayerClient::tickPlayers() {
    for (auto& [id, p] : players_) {
        p.yawO = p.yaw;
        p.pitchO = p.pitch;
        p.headYawO = p.headYaw;
        const double dx = p.x - p.xo, dz = p.z - p.zo;
        p.xo = p.x;
        p.yo = p.y;
        p.zo = p.z;
        p.walkSpeedO = p.walkSpeed;
        p.walkSpeed += (std::min(static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 4.0f, 1.0f) - p.walkSpeed) * 0.4f;
        p.walkPosition += p.walkSpeed;
        if (p.swing > 0.0f) {
            p.swing += 1.0f / 6.0f;
            if (p.swing >= 1.0f) p.swing = 0.0f;
        }
    }
}

void MultiplayerClient::handlePlay(const Packet& p, RemoteHandler& h) {
    Reader r(p.body);
    switch (p.id) {
        case 50: selfEntity_ = r.i32(); break;  // login: our entity (the rest of it is not needed here)
        case 32: fail("disconnected: " + plainText(readComponent(r)), &h); break;
        case 120: {  // start_configuration: back to the configuration phase
            connection_.send(16, {});  // configuration_acknowledged
            phase_ = Phase::Configuration;
            players_.clear();
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
            if (auto chunk = decodeChunk(r, static_cast<int>(registry_.stateCount()), error)) {
                if (h.chunk) h.chunk({cx, cz}, std::move(chunk));
            } else {
                std::printf("[falcon] chunk %d, %d: %s\n", cx, cz, error.c_str());
            }
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
        case 18: {      // container_set_content: only the player's own inventory (container 0) is followed
            const int container = r.varint();
            r.varint();  // the state id
            const int n = r.varint();
            bool known = true;
            for (int i = 0; i < n && r.ok && known; ++i) {
                const ItemStack stack = readItemStack(r, componentIds_, known);
                const int slot = inventorySlotOfMenu(i);
                if (container == 0 && slot >= 0 && h.inventorySlot) h.inventorySlot(slot, stack);
            }
            break;
        }
        case 20: {  // container_set_slot
            const int container = r.varint();
            r.varint();  // the state id
            const int menuSlot = r.i16();
            bool known = true;
            const ItemStack stack = readItemStack(r, componentIds_, known);
            const int slot = inventorySlotOfMenu(menuSlot);
            if (r.ok && container == 0 && slot >= 0 && h.inventorySlot) h.inventorySlot(slot, stack);
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
        case 60: {  // open_screen: a chest, a furnace ...: Falcon cannot show it, so it is closed again at once
            const int container = r.varint();
            if (r.ok) sendContainerClose(container);
            break;
        }
        case 101: {  // set_entity_data: of what comes, the first entry (the flags every entity has) says whether a player sneaks
            const int entity = r.varint();
            const int index = r.u8();
            const int serializer = r.varint();
            if (r.ok && index == 0 && serializer == 0) {
                const int flags = r.u8();
                if (const auto it = players_.find(entity); it != players_.end() && r.ok) it->second.sneaking = (flags & 2) != 0;
                if (entity == selfEntity_ && r.ok && h.selfFlags) h.selfFlags(flags);
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
            if (r.boolean()) r.skip(256);
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
                        r.string();
                        r.string();
                        if (r.boolean()) r.string();
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
                if (actions & 128) r.boolean();  // the hat
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
            skipLpVec3(r);
            pl.pitch = pl.pitchO = angleOf(r.i8());
            pl.yaw = pl.yawO = angleOf(r.i8());
            pl.headYaw = pl.headYawO = angleOf(r.i8());
            if (r.ok && type == playerType_ && id != selfEntity_) players_[id] = pl;
            break;
        }
        case 78: {  // remove_entities
            const int n = r.varint();
            for (int i = 0; i < n && r.ok; ++i) players_.erase(r.varint());
            break;
        }
        case 54:
        case 55:
        case 57: {  // move_entity_pos, move_entity_pos_rot, move_entity_rot
            const int id = r.varint();
            const auto it = players_.find(id);
            if (p.id != 57) {
                const double dx = r.i16() / 4096.0, dy = r.i16() / 4096.0, dz = r.i16() / 4096.0;
                if (it != players_.end() && r.ok) {
                    it->second.x += dx;
                    it->second.y += dy;
                    it->second.z += dz;
                }
            }
            if (p.id != 54) {
                const float yaw = angleOf(r.i8()), pitch = angleOf(r.i8());
                if (it != players_.end() && r.ok) {
                    it->second.yaw = yaw;
                    it->second.pitch = pitch;
                }
            }
            if (it != players_.end()) it->second.onGround = r.boolean();
            break;
        }
        case 128: {  // teleport_entity (entity_position_sync has its own kind of position: not followed)
            const int id = r.varint();
            const PositionMoveRotation m = readPositionMoveRotation(r);
            const auto it = players_.find(id);
            if (it != players_.end() && r.ok) {
                it->second.x = m.x;
                it->second.y = m.y;
                it->second.z = m.z;
                it->second.yaw = m.yaw;
                it->second.pitch = m.pitch;
            }
            break;
        }
        case 85: {  // rotate_head
            const int id = r.varint();
            const auto it = players_.find(id);
            const float yaw = angleOf(r.i8());
            if (it != players_.end() && r.ok) it->second.headYaw = yaw;
            break;
        }
        case 2: {  // animate: an arm swing
            const int id = r.varint();
            const int action = r.u8();
            const auto it = players_.find(id);
            if (it != players_.end() && action == 0) it->second.swing = 0.0001f;
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

void MultiplayerClient::sendTickEnd() {
    if (phase_ == Phase::Play) connection_.send(13, {});
}

void MultiplayerClient::sendPlayerLoaded() {
    if (phase_ == Phase::Play) connection_.send(44, {});
}

void MultiplayerClient::sendChat(const std::string& text) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.string(text);
    w.i64(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    w.i64(0);          // the salt of a message that carries no signature
    w.boolean(false);  // no signature
    w.varint(0);       // messages acknowledged
    w.u8(0); w.u8(0); w.u8(0);
    connection_.send(9, w.data);
}

void MultiplayerClient::sendCommand(const std::string& text) {
    if (phase_ != Phase::Play) return;
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

void MultiplayerClient::sendUseItemOn(int x, int y, int z, int face, double cx, double cy, double cz) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(0);  // the main hand
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

void MultiplayerClient::sendUseItem(float yaw, float pitch) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(0);  // the main hand
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

void MultiplayerClient::sendContainerClose(int containerId) {
    if (phase_ != Phase::Play) return;
    Writer w;
    w.varint(containerId);
    connection_.send(19, w.data);
}

}  // namespace falcon::net
