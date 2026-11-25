#pragma once

#include "falcon/net/connection.hpp"
#include "falcon/net/last_seen.hpp"
#include "falcon/net/protocol_data.hpp"
#include "falcon/net/server_commands.hpp"
#include "falcon/net/session_auth.hpp"
#include "falcon/net/skin_fetcher.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/cloak.hpp"
#include "falcon/world/item_stack.hpp"

#include <unordered_map>
#include <array>
#include <functional>
#include <future>
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

// The account that plays: what a server in online mode asks to see. Without a token only a server that does not check accounts can be joined.
struct Account {
    std::string accessToken;  // Minecraft's, for Mojang's servers (the launcher hands it over)
    std::string profileId;    // the account's id, with or without dashes
    std::string name;
};

// One line of the player list (Tab).
struct TabEntry {
    Uuid id{};
    std::string name;
    StyledText display;  // the name as the server wants it shown (empty: the plain name)
    int gameMode = 0;
    bool listed = true;
    int latency = 0;     // milliseconds
    int order = 0;       // the list order: higher is further up
    bool hat = true;     // the hat layer of the skin is shown
};

// Another player that the server tells us about.
struct RemotePlayer {
    int entityId = 0;
    int type = -1;  // the entity type: a player's (isPlayerEntity), or a mob's that was asked to be followed (setMobTypes)
    double vx = 0, vy = 0, vz = 0;  // its velocity, per tick (a projectile flies by it between the server's words)
    int data = 0;                   // the data of the add packet (a painting's direction)
    int variant = -1;               // a painting's variant: its number in the server's registry
    ItemStack stack;                // what is in an item frame
    int frameRotation = 0;          // and how far it is turned (eighths of a turn)
    int standFlags = 0;             // an armor stand: 1 small, 4 has arms, 8 no base plate
    std::array<std::array<float, 3>, 6> standPose{{{0, 0, 0}, {0, 0, 0}, {-10, 0, -10}, {-10, 0, 10}, {-1, 0, -1}, {1, 0, 1}}};  // head, body, left arm, right arm, left leg, right leg (degrees)
    Uuid id{};
    double x = 0, y = 0, z = 0;
    double xo = 0, yo = 0, zo = 0;
    float yaw = 0, pitch = 0, headYaw = 0;
    float yawO = 0, pitchO = 0, headYawO = 0;
    // Where the server last put it (what its next relative move adds to) and how many ticks are left to ease there: what the game's InterpolationHandler does, three steps a move.
    double sx = 0, sy = 0, sz = 0;
    float syaw = 0, spitch = 0, sheadYaw = 0;
    int lerpSteps = 0, headSteps = 0;
    float bodyYaw = 0, bodyYawO = 0;  // where a player's body faces (LivingEntity.yBodyRot): it follows the way they walk, the head turns on it
    // What is held and worn, by EquipmentSlot (the game's order): main hand, off hand, feet, legs, chest, head.
    std::array<ItemStack, 6> equipment;
    bool onGround = true;
    bool sneaking = false;
    // The swing of an arm (LivingEntity.swing / updateSwingTime): six ticks, started by the animate packet; `swing` is how far it is (0..1), `swingO` how far it was a tick ago.
    float swing = 0.0f, swingO = 0.0f;
    bool swinging = false, swingOffHand = false;
    int swingTicks = 0;
    // What the entity data says of a player (and of other living things): its pose (Entity.Pose by number: 1 gliding, 2 sleeping, 3 swimming, 5 crouching), the flags of entity data 0
    // (on fire 1, crouching 2, sprinting 8, swimming 16, invisible 32, glowing 64, gliding 128), of 8 (using an item 1, in the off hand 2), the parts of the skin that are shown (16)
    // and the arm that is the main one (15).
    int pose = 0;
    int entityFlags = 0;
    int livingFlags = 0;
    int skinParts = 0x7F;
    bool mainArmLeft = false;
    // Worked out each tick like the game does: how far into the swimming pose (Player.swimAmount), how long it has glided, how the elytra's wings are held (ElytraAnimationState),
    // the red flash of a hit and how long it has been dying.
    float swimAmount = 0.0f, swimAmountO = 0.0f;
    int fallFlyingTicks = 0;
    float elytraX = 0.2617994f, elytraY = 0.0f, elytraZ = -0.2617994f;
    float elytraXO = 0.2617994f, elytraYO = 0.0f, elytraZO = -0.2617994f;
    double deltaX = 0.0, deltaY = 0.0, deltaZ = 0.0;  // how far it moved in the last tick (what the game uses as its motion for the wings and the body's lean)
    double deltaXO = 0.0, deltaYO = 0.0, deltaZO = 0.0;
    int hurtTime = 0;
    bool dying = false;
    int deathTime = 0;
    int vehicle = -1;       // the entity it sits on (from set_passengers), -1 for none
    int seatIndex = 0;      // and which place on it
    float walkPosition = 0.0f, walkSpeed = 0.0f, walkSpeedO = 0.0f;
    bool hasSleepPos = false;  // the bed it sleeps in (entity data 14): the block of the bed
    int sleepX = 0, sleepY = 0, sleepZ = 0;
    int usingTicks = 0;  // how long it has been using an item (LivingEntity.getTicksUsingItem): the drawing of a crossbow goes by it
    CloakState cloak;  // where its cape hangs from and how it has walked (the cape sways by them)
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

// A kind of painting the server's registry has: the name of its picture ("minecraft:kebab") and its size in blocks.
struct PaintingVariantInfo {
    std::string asset;
    int width = 1;
    int height = 1;
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
    std::function<void(ChunkPos, const ChunkLightPatch&)> light;  // the sections of a chunk whose light the server sent again (a block was placed or broken there)
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
    // A container that is not the player's own: the server opens one (its id, the number of its menu type in the registry, its title), says what its slots hold (the menu's own
    // numbering: the container's slots, then the player's 27 and the hotbar), the numbers it shows (a furnace's fire), and closes it. `cursor` is the stack on the cursor.
    std::function<void(int id, int menuType, const StyledText& title)> openContainer;
    std::function<void(int id, const std::vector<ItemStack>& items, const ItemStack& carried)> containerContent;
    std::function<void(int id, int slot, const ItemStack&)> containerSlot;
    std::function<void(int id, int index, int value)> containerData;
    std::function<void(int id)> containerClose;
    std::function<void(const ItemStack&)> cursor;
    std::function<void(double x, double y, double z, float yaw, float pitch)> moveVehicle;  // the server puts the vehicle the player drives somewhere (ClientboundMoveVehiclePacket)
    std::function<void()> newWorld;  // the server puts the player in another world (a login on a proxy, a respawn to another dimension): what was of the old one is gone
    std::function<void(const TradeOffers&)> merchantOffers;  // a villager's window: what it offers
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
    // The entity types that are followed besides players and items, by their number in the registry, with their names ("zombie"): mobs that can be drawn.
    void setMobTypes(std::map<int, std::string> types) { mobTypes_ = std::move(types); }
    [[nodiscard]] bool isPlayerEntity(const RemotePlayer& e) const { return e.type == playerType_; }
    // The entity types that fly (arrows, thrown things) with their gravity and drag per tick: between the server's positions they go by their velocity, as on the real client.
    void setProjectileTypes(std::map<int, std::pair<float, float>> physics) { projectilePhysics_ = std::move(physics); }
    // The names of the registry's enchantments, by number ("minecraft:sharpness"): for the clue of an enchanting table.
    [[nodiscard]] const std::string* enchantmentName(int id) const { return id >= 0 && static_cast<std::size_t>(id) < enchantments_.size() ? &enchantments_[static_cast<std::size_t>(id)] : nullptr; }
    [[nodiscard]] const PaintingVariantInfo* paintingVariant(int id) const { return id >= 0 && static_cast<std::size_t>(id) < paintingVariants_.size() ? &paintingVariants_[static_cast<std::size_t>(id)] : nullptr; }
    [[nodiscard]] const std::string* mobName(int type) const {
        const auto it = mobTypes_.find(type);
        return it == mobTypes_.end() ? nullptr : &it->second;
    }
    void setComponentIds(const ComponentIds& ids) { componentIds_ = ids; }
    void setAccount(const Account& account) { account_ = account; }  // before start()
    [[nodiscard]] bool encrypted() const { return connection_.encrypted(); }
    [[nodiscard]] bool chatSigned() const { return chatSessionSent_; }  // the chat is signed with the account's key (what a server that enforces secure profiles wants)
    // The server's biomes, by their number, as Falcon's own (0 where the two have no name in common): a chunk the server sends takes the colours of its biome. The numbers come
    // from the registry_data packet of the configuration phase, in the order the server lists the biomes; this turns each name into the number of Falcon's own registry.
    void setBiomeResolver(std::function<std::uint16_t(std::string_view)> resolve) { biomeResolver_ = std::move(resolve); }
    void setParticleKinds(std::unordered_map<int, ParticleData> kinds) { particleKinds_ = std::move(kinds); }  // what each particle type carries (see particleDataOf)
    void setSoundNames(std::vector<std::string> names) { soundNames_ = std::move(names); }  // by the number the game gives each sound event
    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] bool inPlay() const { return phase_ == Phase::Play; }
    [[nodiscard]] bool closed() const { return phase_ == Phase::Closed; }
    [[nodiscard]] const std::string& closeReason() const { return closeReason_; }
    [[nodiscard]] int selfEntity() const { return selfEntity_; }
    [[nodiscard]] const Uuid& selfId() const { return selfId_; }
    [[nodiscard]] const std::map<int, RemotePlayer>& players() const { return players_; }
    [[nodiscard]] RemotePlayer* entity(int id) {
        const auto it = players_.find(id);
        return it == players_.end() ? nullptr : &it->second;
    }
    // What the player sits in: the entity's number, or -1 (ClientboundSetPassengersPacket says who rides what; the first rider of a vehicle is the one who steers it).
    [[nodiscard]] int vehicle() const { return vehicle_; }
    [[nodiscard]] const std::vector<int>* passengersOf(int vehicle) const {
        const auto it = passengers_.find(vehicle);
        return it == passengers_.end() ? nullptr : &it->second;
    }
    // An entity whose place this machine works out itself (the boat the player drives): what the server says of its movement is not followed, and tickPlayers leaves its last place alone.
    void setControlledEntity(int id) { controlledEntity_ = id; }
    [[nodiscard]] const std::map<int, RemoteItem>& items() const { return items_; }
    // The skin a player's profile names (null: none, so the default skin by their id), and the skins that finished downloading since the last call.
    [[nodiscard]] const SkinInfo* skinOf(const Uuid& id) const {
        const auto it = skinInfos_.find(id);
        return it == skinInfos_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::vector<SkinFetcher::Done> takeSkins() { return skins_.takeFinished(); }
    // The listed entries in the order the game shows them (list order, spectators last, team, name), the first `limit` of them (the game shows 80).
    [[nodiscard]] std::vector<TabEntry> tabList(std::size_t limit = 80) const;
    // The team a player is in by their name, or null (looked up in an index that is rebuilt when the teams change, so thousands of players cost nothing).
    [[nodiscard]] const TeamInfo* teamOfPlayer(const std::string& name) const;
    [[nodiscard]] const StyledText& tabHeader() const { return tabHeader_; }
    [[nodiscard]] const StyledText& tabFooter() const { return tabFooter_; }
    // The name of a player as it hangs over the head or stands in the list: the team's prefix and suffix round it, in the team's colour.
    [[nodiscard]] StyledText decoratedName(const Uuid& id) const;
    [[nodiscard]] const TabEntry* tabEntry(const Uuid& id) const;
    [[nodiscard]] bool nameTagVisible(const Uuid& id) const;
    // One game tick of the other players: they glide to where they were told to be and their limbs swing.
    void tickPlayers();
    // For tests: deals with a packet of the play phase as if the server had sent it (without a connection).
    void injectPlayPacket(int id, const Bytes& body, RemoteHandler& h);

    // Outgoing, from the local player.
    void sendMovement(const MoveState& s);        // every tick: the packet the game would send (position, rotation, both or neither)
    void sendInput(int flags);                    // forward 1, backward 2, left 4, right 8, jump 16, shift 32, sprint 64: when it changes
    void sendSprint(bool on);
    void sendFallFlying();                      // the player opens their elytra (ServerboundPlayerCommandPacket START_FALL_FLYING)
    void sendTickEnd();
    void sendPlayerLoaded();
    void sendChat(const std::string& text);
    void sendCommand(const std::string& textWithoutSlash);
    void sendRespawn();
    void sendHeldSlot(int slot);
    void sendDig(int action, int x, int y, int z, int face);  // 0 start, 1 abort, 2 finish
    void sendUseItemOn(int x, int y, int z, int face, double cx, double cy, double cz);
    // The player uses something on an entity (ServerboundInteractPacket): the point that was hit, relative to the entity, and whether sneaking. A boat is boarded, a villager trades,
    // a frame takes the item, an armor stand is dressed ... all of it by the server's answer.
    void sendInteract(int entityId, double dx, double dy, double dz, bool secondary);
    void sendAttack(int entityId);  // the player hits this entity (ServerboundAttackPacket)
    void sendSwing();                                          // the arm swings (ServerboundPunchPacket: the main hand)
    void sendCreativeSlot(int inventorySlot, const ItemStack& stack);  // creative mode: this slot of the inventory now holds this
    void sendContainerClose(int containerId);
    void sendContainerButton(int containerId, int button);  // a button of a menu (an enchanting table's three offers)
    void sendRotation(float yaw, float pitch, bool onGround, bool horizontalCollision);  // a rider turns (ServerboundMovePlayerPacket.Rot): every tick, nothing else of the player
    void sendMoveVehicle(double x, double y, double z, float yaw, float pitch, bool onGround);  // where the vehicle the player drives is (ServerboundMoveVehiclePacket)
    void sendPaddleBoat(bool left, bool right);  // the paddles of the boat the player drives (ServerboundPaddleBoatPacket)
    void sendSelectTrade(int index);                        // the offer of a villager that is chosen
    void sendRenameItem(const std::string& name);           // what is typed into an anvil's name box
    // A click in a menu (container 0 is the player's own): the slot, the button, the kind (ClickType's number), the slots it changed with what they hold now, the stack on the cursor.
    void sendContainerClick(int containerId, int slot, int button, int mode, const std::vector<std::pair<int, ItemStack>>& changed, const ItemStack& carried);
    void sendUseItem(float yaw, float pitch);  // the item in the hand is used (eaten, drawn, thrown)
    void sendAction(int action);               // ServerboundPlayerActionPacket without a block: 3 drop one, 4 drop all, 5 let go of a used item, 6 swap with the offhand

private:
    void handleLogin(const Packet& p, RemoteHandler& h);
    void handleConfiguration(const Packet& p, RemoteHandler& h);
    void handlePlay(const Packet& p, RemoteHandler& h);
    void fail(const std::string& why, RemoteHandler* h);
    void sendClientInformation(int configurationOrPlay);
    void startEncryption(Reader& r, RemoteHandler& h);   // the server asks for encryption (online mode): tell Mojang, then answer
    void finishEncryption(RemoteHandler& h);              // Mojang has answered: send the key and turn the encryption on
    void sendChatSession();                               // the key that signs chat is told to the server

    const BlockRegistry& registry_;
    Connection connection_;
    Phase phase_ = Phase::Connecting;
    std::string closeReason_;
    std::string username_;
    Uuid selfId_{};
    int selfEntity_ = -1;
    int playerType_ = -1;  // the entity type number of a player
    int itemType_ = -1;    // and of an item
    std::map<int, std::string> mobTypes_;
    std::map<int, std::pair<float, float>> projectilePhysics_;
    std::vector<PaintingVariantInfo> paintingVariants_;
    std::vector<std::string> enchantments_;
    std::map<int, std::vector<int>> passengers_;
    int vehicle_ = -1;
    int controlledEntity_ = -1;
    std::map<int, RemoteItem> items_;
    ComponentIds componentIds_;
    SkinFetcher skins_;
    std::map<Uuid, SkinInfo> skinInfos_;
    Account account_;
    struct KeyExchange {
        Bytes publicKey, challenge;
        std::uint8_t secret[16] = {};
    } exchange_;
    std::future<std::string> joinResult_;  // "" when the session server took the join, else why not
    struct KeysResult {
        bool ok = false;
        ChatKeys keys;
        std::string error;
    };
    std::future<KeysResult> keysResult_;
    ChatKeys chatKeys_;
    bool haveChatKeys_ = false, chatSessionSent_ = false;
    Uuid chatSessionId_{};
    int chatIndex_ = 0;  // the number of the next signed message in the chain
    ServerCommands commands_;  // the server's command tree: which arguments of a command are messages that must be signed
    LastSeenTracker lastSeen_;  // the signed messages of others that were seen (every message of ours says which)
    std::map<int, int> containerStates_;  // the state of each menu as the server last said it (a click must carry it)
    std::vector<std::string> soundNames_;
    int sequence_ = 0;
    int positionReminder_ = 0;
    bool haveLast_ = false;
    MoveState last_;
    bool lastOnGround_ = false, lastHorizontal_ = false;
    std::map<int, RemotePlayer> players_;
    std::map<Uuid, TabEntry> tab_;
    std::map<std::string, TeamInfo> teams_;
    std::unordered_map<int, ParticleData> particleKinds_;
    std::vector<std::uint16_t> biomeIds_;  // the server's biome numbers, as Falcon's (see setBiomeResolver)
    std::function<std::uint16_t(std::string_view)> biomeResolver_;
    std::string dimension_;                      // the dimension the player is in (its name), to tell a respawn that changes the world from one that does not
    void clearWorld(RemoteHandler& h);           // everything of the old world that was followed
    bool skipDataValue(int serializer, Reader& r);  // skipEntityDataValue and the kinds that need the tables of this connection
    mutable std::unordered_map<std::string, const TeamInfo*> memberIndex_;
    mutable bool memberIndexDirty_ = true;
    StyledText tabHeader_, tabFooter_;
    bool chunkBatchOpen_ = false;
};

}  // namespace falcon::net
