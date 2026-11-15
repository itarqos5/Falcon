#pragma once

#include "falcon/net/connection.hpp"
#include "falcon/net/protocol_data.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/item_stack.hpp"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace falcon::net {

using Uuid = std::array<std::uint8_t, 16>;

// The default skin of a player (PlayerSkin.get(UUID): the hash of the UUID, modulo the 18 skins; 0-8 slim, 9-17 wide) as BodyPose::skin counts them.
[[nodiscard]] int defaultSkinOf(const Uuid& id);
// Steps over one value of entity data by the number of its kind; false when that kind's layout is not known (the rest of the entries cannot then be read).
[[nodiscard]] bool skipEntityDataValue(int serializer, Reader& r);

// One line of the player list (Tab).
struct TabEntry {
    Uuid id{};
    std::string name;
    StyledText display;  // the name as the server wants it shown (empty: the plain name)
    int gameMode = 0;
    bool listed = true;
    int latency = 0;     // milliseconds
    int order = 0;       // the list order: higher is further up
};

// Another player that the server tells us about.
struct RemotePlayer {
    int entityId = 0;
    Uuid id{};
    double x = 0, y = 0, z = 0;
    double xo = 0, yo = 0, zo = 0;
    float yaw = 0, pitch = 0, headYaw = 0;
    float yawO = 0, pitchO = 0, headYawO = 0;
    bool onGround = true;
    bool sneaking = false;
    float swing = 0.0f;       // 0..1 through an arm swing, 0 = none
    float walkPosition = 0.0f, walkSpeed = 0.0f, walkSpeedO = 0.0f;
};

// An item lying in the world (an item entity the server tells about).
struct RemoteItem {
    int entityId = 0;
    ItemStack stack;
    double x = 0, y = 0, z = 0;
    double xo = 0, yo = 0, zo = 0;
    int age = 0;  // ticks since it appeared (it bobs and spins by this)
    bool onGround = false;
};

// A scoreboard team: its prefix, suffix and colour go round the names of its members (the name tags and the player list).
struct TeamInfo {
    std::string name;
    StyledText prefix, suffix;
    int color = -1;  // ChatFormatting index 0..15, or -1
    std::string nameTagVisibility = "always";
    std::set<std::string> members;
};

// What the client says each game tick about the body.
struct MoveState {
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    bool onGround = false;
    bool horizontalCollision = false;
};

// What the world has to do for the packets the server sends. Called from pump(), on the thread that calls it.
struct RemoteHandler {
    std::function<MoveState()> self;  // where the player is now (to confirm a teleport with)
    std::function<void(ChunkPos, std::shared_ptr<Chunk>)> chunk;
    std::function<void(ChunkPos)> forget;
    std::function<void(int, int, int, Block)> block;
    // The server moves the player: absolute values (the relative parts have been resolved). The player must be moved to these and left like this.
    // `relative` has a bit for every part that is added to what the player has (x 1, y 2, z 4, yaw 8, pitch 16, dx 32, dy 64, dz 128).
    std::function<void(double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, int relative)> teleport;
    std::function<void(const StyledText&, bool overlay)> chat;
    std::function<void(float health, int food, float saturation)> health;
    std::function<void(float progress, int level)> experience;
    std::function<void(int gameType)> gameMode;
    std::function<void(bool flying, bool mayFly, bool instabuild, bool invulnerable)> abilities;
    std::function<void(int slot, const ItemStack&)> inventorySlot;  // a slot of the player's own inventory (numbered as Inventory is: 0-8 hotbar, 36-39 boots to helmet, 40 offhand)
    std::function<void(const std::string& event, int source, double x, double y, double z, float volume, float pitch)> sound;  // a sound event ("block.stone.place") at a place
    std::function<void(int type, int x, int y, int z, int data)> levelEvent;                                                      // a world event (2001: a block is broken, `data` its state)
    std::function<void(int flags)> selfFlags;  // the flags every entity has, of the player: bit 0 on fire, bit 1 sneaking, bit 3 sprinting, bit 7 gliding
    std::function<void(int slot)> heldSlot;                          // the server picks the hotbar slot in the hand
    std::function<void(const std::string&)> disconnect;
};

// The multiplayer protocol (26.3, protocol 777) as a client of a server that runs with online-mode=false: handshake, login, configuration, play. It keeps what the
// server tells about the other players, the player list and the teams, and hands the world's part to a RemoteHandler.
class MultiplayerClient {
public:
    enum class Phase { Connecting, Login, Configuration, Play, Closed };

    explicit MultiplayerClient(const BlockRegistry& registry);
    ~MultiplayerClient();

    // Connects and sends the handshake and the login. False and `error` when the server cannot be reached.
    bool start(const std::string& host, int port, const std::string& username, std::string& error);
    // Takes in the packets that have come, once a frame.
    void pump(RemoteHandler& handler);
    void close();

    void setPlayerEntityType(int type) { playerType_ = type; }  // the number of minecraft:player in the entity type registry
    void setItemEntityType(int type) { itemType_ = type; }      // and of minecraft:item
    void setComponentIds(const ComponentIds& ids) { componentIds_ = ids; }
    void setSoundNames(std::vector<std::string> names) { soundNames_ = std::move(names); }  // by the number the game gives each sound event
    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] bool inPlay() const { return phase_ == Phase::Play; }
    [[nodiscard]] bool closed() const { return phase_ == Phase::Closed; }
    [[nodiscard]] const std::string& closeReason() const { return closeReason_; }
    [[nodiscard]] int selfEntity() const { return selfEntity_; }
    [[nodiscard]] const Uuid& selfId() const { return selfId_; }
    [[nodiscard]] const std::map<int, RemotePlayer>& players() const { return players_; }
    [[nodiscard]] const std::map<int, RemoteItem>& items() const { return items_; }
    [[nodiscard]] std::vector<TabEntry> tabList() const;  // listed entries, in the order the game shows them
    [[nodiscard]] const StyledText& tabHeader() const { return tabHeader_; }
    [[nodiscard]] const StyledText& tabFooter() const { return tabFooter_; }
    // The name of a player as it hangs over the head or stands in the list: the team's prefix and suffix round it, in the team's colour.
    [[nodiscard]] StyledText decoratedName(const Uuid& id) const;
    [[nodiscard]] const TabEntry* tabEntry(const Uuid& id) const;
    [[nodiscard]] bool nameTagVisible(const Uuid& id) const;
    // One game tick of the other players: they glide to where they were told to be and their limbs swing.
    void tickPlayers();

    // Outgoing, from the local player.
    void sendMovement(const MoveState& s);        // every tick: the packet the game would send (position, rotation, both or neither)
    void sendInput(int flags);                    // forward 1, backward 2, left 4, right 8, jump 16, shift 32, sprint 64: when it changes
    void sendSprint(bool on);
    void sendTickEnd();
    void sendPlayerLoaded();
    void sendChat(const std::string& text);
    void sendCommand(const std::string& textWithoutSlash);
    void sendRespawn();
    void sendHeldSlot(int slot);
    void sendDig(int action, int x, int y, int z, int face);  // 0 start, 1 abort, 2 finish
    void sendUseItemOn(int x, int y, int z, int face, double cx, double cy, double cz);
    void sendSwing();                                          // the arm swings (ServerboundPunchPacket: the main hand)
    void sendCreativeSlot(int inventorySlot, const ItemStack& stack);  // creative mode: this slot of the inventory now holds this
    void sendContainerClose(int containerId);
    void sendUseItem(float yaw, float pitch);  // the item in the hand is used (eaten, drawn, thrown)
    void sendAction(int action);               // ServerboundPlayerActionPacket without a block: 3 drop one, 4 drop all, 5 let go of a used item, 6 swap with the offhand

private:
    void handleLogin(const Packet& p, RemoteHandler& h);
    void handleConfiguration(const Packet& p, RemoteHandler& h);
    void handlePlay(const Packet& p, RemoteHandler& h);
    void fail(const std::string& why, RemoteHandler* h);
    void sendClientInformation(int configurationOrPlay);

    const BlockRegistry& registry_;
    Connection connection_;
    Phase phase_ = Phase::Connecting;
    std::string closeReason_;
    std::string username_;
    Uuid selfId_{};
    int selfEntity_ = -1;
    int playerType_ = -1;  // the entity type number of a player
    int itemType_ = -1;    // and of an item
    std::map<int, RemoteItem> items_;
    ComponentIds componentIds_;
    std::vector<std::string> soundNames_;
    int sequence_ = 0;
    int positionReminder_ = 0;
    bool haveLast_ = false;
    MoveState last_;
    bool lastOnGround_ = false, lastHorizontal_ = false;
    std::map<int, RemotePlayer> players_;
    std::map<Uuid, TabEntry> tab_;
    std::map<std::string, TeamInfo> teams_;
    StyledText tabHeader_, tabFooter_;
    bool chunkBatchOpen_ = false;
};

}  // namespace falcon::net
