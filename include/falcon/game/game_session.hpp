#pragma once

#include <chrono>

#include "falcon/assets/resource_manager.hpp"
#include "falcon/game/advancements.hpp"
#include "falcon/game/chat.hpp"
#include "falcon/game/command_tree.hpp"
#include "falcon/game/frame_stats.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/game/hotbar_hud.hpp"
#include "falcon/game/mining.hpp"
#include "falcon/game/survival_hud.hpp"
#include "falcon/net/multiplayer_client.hpp"
#include "falcon/game/world_sounds.hpp"
#include "falcon/jvm/mod_runtime.hpp"
#include "falcon/platform/system_info.hpp"
#include "falcon/game/remote_menu.hpp"
#include "falcon/render/mob_renderer.hpp"
#include "falcon/render/player_body.hpp"
#include "falcon/world/boat.hpp"
#include "falcon/world/block_containers.hpp"
#include "falcon/world/item_entity.hpp"
#include "falcon/world/mob.hpp"
#include "falcon/platform/window.hpp"
#include "falcon/render/block_icons.hpp"
#include "falcon/render/block_models.hpp"
#include "falcon/render/hand_renderer.hpp"
#include "falcon/render/item_icons.hpp"
#include "falcon/render/player_preview.hpp"
#include "falcon/render/world_renderer.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/world/level_info.hpp"
#include "falcon/world/player.hpp"
#include "falcon/world/inventory.hpp"
#include "falcon/world/menu.hpp"
#include "falcon/world/fluids.hpp"
#include "falcon/world/raycast.hpp"
#include "falcon/world/water_fog.hpp"
#include "falcon/world/block_behaviours.hpp"
#include "falcon/world/block_physics.hpp"
#include "falcon/world/redstone.hpp"
#include "falcon/world/block_support.hpp"
#include "falcon/world/block_tags.hpp"
#include "falcon/world/loot.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace falcon {

// A world being played: the level and its renderer, the player, what they are looking at, the inventory (the hotbar is its
// first nine slots), and the HUD. The main loop calls update() and renderWorld() every frame while a world is open, and the in-game screen
// draws the HUD and passes keys and clicks in.
//
// This is a first, small singleplayer: a generated world (Superflat, or this client's own simple hills; see
// TerrainGenerator) to walk, fly and swim in, with blocks to break and place, an inventory and the E screens. There are no
// mobs, light levels or day and night. The world is saved when it is left (see save()). The player is simulated in game ticks, 20 a
// second (see Player); between ticks the camera shows the interpolated position. A paused game does not tick.
class GameSession {
    friend struct CommandRunner;

public:
    GameSession(ScreenServices& services, const ResourceManager& resources, Window& window, const LevelInfo& info);
    // Leaving the world (quitting to the title, closing the window) saves it.
    ~GameSession();

    [[nodiscard]] const LevelInfo& info() const { return info_; }
    // The spawn area is loaded and the player can move. Like the game's LevelLoadTracker this waits for the chunks around the
    // spawn AND for the section the player stands in to be on the GPU, and gives up waiting after 30 seconds (so the loading
    // screen can never stay for good).
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] float progress() const { return progress_; }
    // For screenshots of the loading screen: keeps it up for at least this many seconds.
    void setMinLoadSeconds(double seconds) { minLoadSeconds_ = seconds; }

    // Once a frame, before drawing. `active`: the player is steering (no menu is open). `keepTicking`: a menu is open that does not
    // stop the game (the inventory: in singleplayer the world goes on, the player only cannot steer).
    void update(double dt, bool active, int framebufferWidth, int framebufferHeight, bool keepTicking = false);
    void tickMods();  // the mods' tick handlers, run by the VM (src/game/game_session_mods.cpp)
    void renderWorld(int framebufferWidth, int framebufferHeight);
    // The crosshair, hotbar, the survival bars (hearts, food, experience) and the (F3) debug screen.
    void renderHud(GuiGraphics& graphics, int guiWidth, int guiHeight, bool chatFocused = false);
    // The game's "Loading terrain..." screen (LevelLoadingScreen), drawn instead of the HUD until ready(): the blurred panorama,
    // the text, a progress bar and a map of the chunks around the spawn.
    void renderLoading(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks);

    // The chat log and history. The chat screen draws it focused and adds to it; the HUD draws it faded.
    [[nodiscard]] ChatComponent& chat() { return chat_; }
    // Handles a line the player typed into the chat: remembers it, then shows it (a "/..." is treated as a command).
    void chatInput(const std::string& text);
    // Plays on a server instead of in a world of this computer (the server must not check accounts: online-mode=false). False and `error` when it cannot be reached.
    bool connectRemote(const std::string& host, int port, const std::string& username, std::string& error);
    [[nodiscard]] bool isRemote() const { return remote_ != nullptr; }
    // The container a server opened (a chest, a furnace ...): the menu, its title, and whether it is open. The main loop opens the window when consumeContainerOpen() says so, and
    // closes it when the server does (consumeContainerClose()).
    [[nodiscard]] RemoteMenu* remoteMenu() { return remoteMenu_.get(); }
    [[nodiscard]] const std::string& remoteMenuTitle() const { return remoteMenuTitle_; }
    // "Sharpness III" for the enchantment with this number in the server's registry and this level (what an enchanting table's clue says).
    [[nodiscard]] std::string enchantmentName(int id, int level) const;
    [[nodiscard]] bool consumeContainerOpen() { return std::exchange(containerOpenRequested_, false); }
    [[nodiscard]] bool consumeContainerClose() { return std::exchange(containerCloseRequested_, false); }
    [[nodiscard]] const std::string& remoteLost() const { return remoteLost_; }  // why the server connection ended, "" while it lasts
    // The game's commands (null when its command list is missing), and what the argument parsers and the suggestions need to know.
    [[nodiscard]] const CommandTree* commandTree() const { return commands_.get(); }
    [[nodiscard]] CommandContext commandContext() const;
    [[nodiscard]] bool gameRuleOn(const std::string& rule) const;
    void addChatMessage(const std::string& text);

    // Input events the in-game screen passes on (a key was pressed, with the GLFW modifier bits; a mouse button; the wheel turned).
    void onKey(int glfwKey, int mods = 0);
    // A key was let go (the debug key F3 shows or hides the debug screen when it is let go without a chord, as in the game).
    void onKeyReleased(int glfwKey);
    void onMouseButton(int glfwButton);
    void onScroll(double dy);
    // Whether a key press is the one bound to a key mapping name ("key.inventory").
    [[nodiscard]] bool keyIs(const char* action, int glfwKey) const;

    // For tests and the benchmark: put the player somewhere and make them fly (nothing falls).
    void teleport(double x, double y, double z, float yaw, float pitch);
    // Flies a fixed path for `seconds` and measures; benchmarkDone() turns true when it has finished.
    void startBenchmark(double seconds);
    [[nodiscard]] bool benchmarkDone() const { return benchmark_.done; }
    // Frame time numbers of the benchmark as lines of text (also what --bench prints).
    [[nodiscard]] std::vector<std::string> benchmarkReport() const;
    void setDebugVisible(bool visible) { showDebug_ = visible; }
    // F1 ("Toggle GUI"): hides the crosshair and hotbar.
    void toggleHud() { hudHidden_ = !hudHidden_; }
    [[nodiscard]] bool hudHidden() const { return hudHidden_; }
    void noteCpuMillis(double ms) {
        cpuMillis_ = cpuMillis_ == 0.0 ? ms : cpuMillis_ * 0.95 + ms * 0.05;
        if (benchmark_.running) benchmark_.cpu.push_back(static_cast<float>(ms));
    }
    // One frame, start to start: how long it took and how much of it went to each task (the Developer page of the video settings reads the last few hundred).
    void noteFrame(double frameMs, const FramePhaseTimes& phases) { frameStats_.record(frameMs, phases); }
    [[nodiscard]] const FrameStats& frameStats() const { return frameStats_; }
    void resetFrameStats() { frameStats_.reset(); }
    [[nodiscard]] double fps() const { return fps_; }
    [[nodiscard]] double frameMillis() const { return frameMillis_; }
    [[nodiscard]] double cpuMillis() const { return cpuMillis_; }

    [[nodiscard]] Player& player() { return player_; }
    [[nodiscard]] WorldRenderer& renderer() { return renderer_; }
    [[nodiscard]] const std::optional<BlockHit>& target() const { return target_; }

    // ---- items ----
    [[nodiscard]] Inventory& inventory() { return inventory_; }
    [[nodiscard]] InventoryMenu& inventoryMenu() { return *inventoryMenu_; }
    [[nodiscard]] ItemIcons& itemIcons() { return itemIcons_; }
    // The player in the inventory screens (the game's extractEntityInInventoryFollowsMouse): the player model in the box (x0, y0)-(x1, y1) of the
    // GUI, turned towards the mouse. `scale` is GUI units per block (30 in the survival inventory, 20 in the creative one).
    void drawPlayerPreview(GuiGraphics& graphics, int x0, int y0, int x1, int y1, float scale, float mouseX, float mouseY);
    [[nodiscard]] int selectedSlot() const { return inventory_.selected; }
    // Creative mode: infinite materials, flying, the creative inventory screen.
    [[nodiscard]] bool creative() const { return info_.gameType == 1; }
    // The game mode: 0 survival, 1 creative, 2 adventure, 3 spectator; and the one before it (-1 until it has changed).
    [[nodiscard]] int gameType() const { return info_.gameType; }
    [[nodiscard]] int previousGameType() const { return previousGameType_; }
    // One line on what the game is doing (the world, the dimension, where the player is, how it draws): for the log and the crash report. Reads only; safe from a crash handler.
    [[nodiscard]] std::string describe() const;
    // Survival and adventure show hearts, food and the experience bar (Hud: gameMode.canHurtPlayer / hasExperience); creative and spectator do not.
    [[nodiscard]] bool survivalBars() const { return info_.gameType == 0 || info_.gameType == 2; }
    // What the bars show (nothing changes health, food or experience yet, see Vitals); `--vitals` sets it for screenshots.
    void setVitals(const Vitals& vitals) { vitals_ = vitals; }
    [[nodiscard]] const Vitals& vitals() const { return vitals_; }
    // Whether the game mode may be changed from here (Minecraft.canSwitchGameMode plus the /gamemode permission): the world has cheats
    // on (Create New World: Allow Cheats).
    [[nodiscard]] bool canSwitchGameMode() const { return ready_ && info_.allowCommands; }

    // ---- the pause menu's World Options, Statistics and Advancements screens ----
    [[nodiscard]] int difficulty() const { return difficulty_; }  // 0 peaceful .. 3 hard
    void setDifficulty(int level) { difficulty_ = level < 0 ? 0 : level > 3 ? 3 : level; }
    [[nodiscard]] bool allowCommands() const { return info_.allowCommands; }
    void setAllowCommands(bool on) { info_.allowCommands = on; }
    [[nodiscard]] std::string gameRuleValue(const std::string& rule) const;     // what /gamerule says: the value set, or the default
    void setGameRule(const std::string& rule, const std::string& value);        // /gamerule without the chat line
    [[nodiscard]] static const std::map<std::string, std::string>& knownGameRules();  // every game rule with the value a new world has
    // Statistics, by the game's own names ("custom/jump", "mined/stone", "killed/zombie", ...): distances in centimetres, times in ticks, damage in tenths of a heart.
    void addStat(const std::string& key, std::int64_t amount = 1) { stats_[key] += amount; }
    [[nodiscard]] std::int64_t stat(const std::string& key) const {
        const auto it = stats_.find(key);
        return it == stats_.end() ? 0 : it->second;
    }
    [[nodiscard]] const std::map<std::string, std::int64_t>& stats() const { return stats_; }
    // Advancements: the ids ("story/mine_stone") of the ones that are done. A grant says whether it is new, and tells the chat.
    bool grantAdvancement(const std::string& id);
    [[nodiscard]] const AdvancementTree* advancementTree() const { return advancementTree_.get(); }
    [[nodiscard]] bool hasAdvancement(const std::string& id) const { return advancements_.count(id) != 0; }
    [[nodiscard]] const std::set<std::string>& advancements() const { return advancements_; }
    // The game mode switcher and F3 + N ask for a mode: it changes if the world allows it. setGameType() changes it unconditionally.
    void requestGameType(int gameType);
    void setGameType(int gameType);
    // True once after F3 + F4 asked for the game mode switcher: the in-game screen opens it.
    [[nodiscard]] bool consumeGameModeSwitcherRequest() {
        const bool asked = switcherRequested_;
        switcherRequested_ = false;
        return asked;
    }
    [[nodiscard]] bool advancedTooltips() const { return advancedTooltips_; }
    // Puts "diamond_pickaxe" or "apple:5" (an item and a count) into the inventory (--give). False if the item does not exist.
    bool giveItem(const std::string& spec);
    // Everything thrown out of a menu or with Q (or lost at death) becomes an item in the world; this counts them.
    [[nodiscard]] int droppedCount() const { return droppedCount_; }
    // The items lying in the world.
    [[nodiscard]] ItemEntities& itemEntities() { return itemEntities_; }
    // The mobs of this world (null on a server: its mobs are the server's).
    [[nodiscard]] Mobs* mobSim() { return mobSim_.get(); }
    // The dimension the player is in: 0 the Overworld, 1 the Nether, 2 the End.
    [[nodiscard]] int dimension() const { return dimension_; }
    // Goes to another dimension: its chunks are made behind the loading screen and the player is put at (x, y, z) -- or, with `portal`, at the portal that is near there (one is made if
    // there is none).
    void travelTo(int dimension, double x, double y, double z, bool portal);
    // How purple the screen is from standing in a portal, 0 to 1 (between two ticks: the tick before, then this one).
    [[nodiscard]] float portalOverlay(float partial) const { return portalOverlayO_ + (portalOverlay_ - portalOverlayO_) * partial; }
    // The time of day in ticks (0 sunrise, 6000 noon, 12000 sunset, 18000 midnight) and how many ticks the world has run; /time changes the first. `freeze` stops the clock (--time).
    [[nodiscard]] long long dayTime() const { return dayTime_; }
    [[nodiscard]] long long gameTime() const { return gameTime_; }
    void setDayTime(long long ticks, bool freeze = false) {
        dayTime_ = ticks;
        if (freeze) timeFrozen_ = true;
    }
    // Player.drop: puts a stack in the world at the player's hands, thrown the way they look (or, for what is lost at death, scattered at random).
    void dropFromPlayer(const ItemStack& stack, bool scatter = false);

    // ---- health ----
    // LivingEntity.hurt for the player: false if it counted for nothing (creative, spectator, dead, or a hit weaker than the one just taken).
    // Within the half second after a hit only a stronger one counts, and only for the difference; `bypassCooldown` is for the void.
    // `bypassArmor`: fall damage and the like ignore what is worn (the damage types that bypass armor); the rest is cut by CombatRules.getDamageAfterAbsorb.
    bool hurt(float amount, bool bypassCooldown = false, bool bypassArmor = false);
    // What the armor in the four armor slots adds up to (the armor bar) and its toughness.
    [[nodiscard]] float armorPoints();
    [[nodiscard]] float armorToughness();
    [[nodiscard]] bool dead() const { return dead_; }
    // True once after the player died: the game shows the death screen.
    [[nodiscard]] bool consumeDeathRequest() {
        const bool asked = deathRequested_;
        deathRequested_ = false;
        return asked;
    }
    // Back at the spawn with full health (the death screen's Respawn).
    void respawn();

    // ---- saving ----
    // Writes the player, the items lying about and every changed block to <world>/falcon_save.dat. Only worlds Falcon made are saved (a world made by
    // Minecraft is not ours to write into), and not from test runs (a pinned camera, --capture, --bench) unless FALCON_SAVE=1. Also done every five minutes.
    void save();
    void setSavingEnabled(bool enabled) { savingEnabled_ = enabled; }
    [[nodiscard]] const WorldEdits& edits() const { return *edits_; }

    // ---- F5: the camera ----
    // 0 first person, 1 from behind (the player's back), 2 from the front (looking back at the player's face).
    [[nodiscard]] int perspective() const { return perspective_; }
    void setPerspective(int perspective) { perspective_ = ((perspective % 3) + 3) % 3; }
    // The game's pick block for a block state: the held slot or a free hotbar slot takes its item (creative) or the stack
    // that holds it is brought to the hotbar (survival).
    void pickBlock(Block block);
    // Changes a block the way the player does: it is redrawn at once, and the water round it starts to flow, or to dry up, as it should.
    bool setBlock(int x, int y, int z, Block block);

    static constexpr int kMinRenderDistance = 2;
    static constexpr int kMaxRenderDistance = 32;

private:
    [[nodiscard]] bool isDown(const char* action) const;
    [[nodiscard]] int renderDistance() const;
    void applyLook(bool active);
    // The keys held right now, as the game's KeyboardInput reads them once per tick.
    [[nodiscard]] PlayerInput sampleInput() const;
    // Runs the game ticks (20 a second) that `seconds` of real time are worth, at most 10 a frame like the game. `steering`:
    // the keys move the player; otherwise (a menu is open) the player only carries on with what they were doing.
    void tickPlayer(double seconds, bool steering);
    void tickFovModifier();
    void pumpSkins();
    void openTestContainer();  // FALCON_TEST_CONTAINER=generic_9x3 (a test aid): a container with a few things in it, opened here  // skins that finished downloading become textures
    void drawFireOverlay(GuiGraphics& g, int guiWidth, int guiHeight);
    // The 20 ticks a second of the HUD (the fading name of the held item).
    void tickHud(double seconds);
    // ---- world sounds ----
    // Plays a sound of the world; `at` (a place in the world) makes it fade with the distance from the player's head (linear, over 16 blocks).
    void playWorldSound(const std::string& event, SoundSource source, float volume, float pitch, const std::array<double, 3>* at = nullptr);
    // A sound of a block ("break", "place", "step", "fall") with the volume and pitch already worked out.
    void playBlockSound(Block block, const char* kind, SoundMix mix, SoundSource source, const std::array<double, 3>* at = nullptr);
    void footsteps(bool sneakKey);
    void fallDamage(int damage);
    void splash(double vx, double vy, double vz);
    [[nodiscard]] float randomFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_); }
    // ---- the player's body ----
    void tickBody();
    void loadSave();
    void tickHealth();
    void tickAir();  // drowning (LivingEntity.baseTick): the air under water, and the damage when it is gone
    void causeExhaustion(float amount);  // FoodData.addExhaustion (survival, singleplayer): the food bar goes down as the player does things
    void pickUpItems();
    void tickStatistics(bool jumped);     // the time, the distances and, every second, the advancements (game_session_player.cpp)
    void checkAdvancements();
    // ---- the blocks of a world played here that hold items or open a window (game_session_containers.cpp) ----
    bool useBlockContainer(int x, int y, int z);  // the right click on a crafting table, chest, furnace ...: opens its window (true if the block was one)
    void openLocalMenu(RemoteMenuKind kind, int rows, Container* storage, bool local, const std::string& title, StorageKind storageKind, const std::array<int, 3>* at);
    void tickContainers();
    void storageBlockReplaced(Block old, Block now, int x, int y, int z);
    void connectChest(int x, int y, int z);
    void addExperience(int points);
    void awardFurnaceExperience(BlockStorage& storage);
    void die();
    void drawHotbar(GuiGraphics& graphics, int guiWidth, int guiHeight);
    // Hud.extractPlayerHealth and the experience bar: the hearts above the left of the hotbar, the food above the right, the experience strip between.
    void drawSurvivalBars(GuiGraphics& graphics, int guiWidth, int guiHeight);
    // The debug screen's two columns: the left one is the game (position, world, drawing), the right one the PC and what is looked at.
    void drawDebugScreen(GuiGraphics& graphics, int guiWidth);
    [[nodiscard]] std::vector<std::string> debugLinesRight() const;
    void refreshSystemLines();
    void collectDrop(Block broken, int x, int y, int z, const ItemStack& tool, bool correctTool);
    // A block taken out of the world by the player: the block goes, it sounds, and in survival it drops what its loot table says (and wears the tool).
    void breakBlockAt(int x, int y, int z, bool survival);
    void digSurvival(double dt, bool fresh);   // MultiPlayerGameMode.startDestroyBlock / continueDestroyBlock; `fresh`: the button went down this frame
    void stopDigging(bool abort = true);  // (on a server an abort is told to it)
    void swingArm();                      // the hand swings (and on a server the arm swing is told to it)
    bool useItemRemote();                 // right click on a server: the block clicked or the item in the hand
    void syncCreativeSlots();             // creative on a server: what the player took or put in the inventory is told to it
    void damageHeldTool(int amount);
    void runCommand(const std::string& text);  // a command typed in the chat, without its "/"
    void applyGameRule(const std::string& rule, const std::string& value);
    void tickBurning();
    void tickMobs();
    // ---- buckets and what the fluids do to each other (game_session_buckets.cpp)
    bool useBucket(const std::array<double, 3>& eye, const std::array<double, 3>& look);
    void fluidReact(int x, int y, int z);
    void fizzAt(int x, int y, int z);
    void tickFluids();
    void holdForLoading();  // the loading screen until the chunks round the player are there (a teleport far away)
    // ---- the dimensions (game_session_dimensions.cpp) and the portals (game_session_portals.cpp)
    struct DimensionState {
        std::shared_ptr<const worldgen::OverworldGenerator> generator;
        std::shared_ptr<const Decorator> decorator;
        std::function<void(Chunk&, int, int, const worldgen::ChunkExtras&)> decorate;
        std::shared_ptr<WorldEdits> edits;
        NbtTag mobs, containers;
        std::vector<ItemEntity> items;
        bool visited = false;
    };
    struct PortalFrame {
        int x = 0, y = 0, z = 0;  // the lowest cell of the opening at the left end (looking along the axis)
        int axis = 0;             // 0: the opening is in the plane of x, 1: of z
        int width = 0, height = 0;
    };
    struct Arrival {
        bool pending = false;
        int dimension = 0;
        bool portal = false;
        double x = 0.0, y = 0.0, z = 0.0;
    };
    bool prepareDimension(int dimension);
    void stashDimension();
    void completeArrival();
    [[nodiscard]] const worldgen::OverworldGenerator* activeGenerator() const;
    [[nodiscard]] std::filesystem::path dimensionSavePath(int dimension) const;
    [[nodiscard]] daycycle::Sky dimensionSky(const Camera& cam) const;
    mutable std::array<float, 3> fogSmooth_{};  // the Nether's fog as it is now: it moves towards the fog of the biomes round the camera, it does not jump
    mutable bool fogSmoothSet_ = false;
    mutable std::chrono::steady_clock::time_point fogSmoothAt_;
    [[nodiscard]] bool isBlockNamed(Block b, const char* name) const;
    [[nodiscard]] bool portalEmpty(Block b) const;
    [[nodiscard]] bool findPortalFrame(int x, int y, int z, int axis, PortalFrame& out) const;
    [[nodiscard]] bool findClosestPortal(double x, double z, int radius, PortalFrame& out) const;
    [[nodiscard]] bool playerInsideBlock(const char* name) const;
    bool tryLightPortal(int x, int y, int z);
    void checkPortalsAround(int x, int y, int z);
    void tryActivateEndPortal(int x, int y, int z);
    void createPortalAt(double x, double yHint, double z, int dimension, PortalFrame& out);
    void arriveByPortal(double x, double y, double z);
    void placeEndPlatform();
    void liftOutOfBlocks();
    void returnFromEnd();
    void startDragonFight();
    void makeExitPortal(bool active);
    void drawPortalOverlay(GuiGraphics& g, int guiWidth, int guiHeight);
    void drawSpyglassOverlay(GuiGraphics& g, int guiWidth, int guiHeight);
    void drawBossBar(GuiGraphics& g, int guiWidth, int guiHeight);
    void tickPortal();
    void tickDragonFight();
    void attackMob(int id);  // the player hits a mob: the damage the held item and the attack cooldown give, the knockback, the sounds
    void mobHitsPlayer(float amount, double fromX, double fromZ, float knockback);
    void renderLocalMobs(const std::array<double, 3>& camPos);
    // F3 + B and F3 + G: the lines the game's EntityHitboxDebugRenderer and ChunkBorderRenderer draw (game_session_debug.cpp).
    [[nodiscard]] std::vector<WorldRenderer::DebugLine> debugGizmos();
    bool debugChord(int key);  // the rest of the F3 chords (D, C, A, B, G, Q), with the chat feedback the game gives; true if the key was one (it is then not a key of the game as well)
    void debugMessage(const std::string& text);
    void renderModHud(GuiGraphics& graphics, int guiWidth, int guiHeight);  // what the mods' HUDs draw
    void renderModLevel(const Camera& camera, int framebufferWidth, int framebufferHeight);  // what they draw in the level
    [[nodiscard]] bool runModCommand(const std::string& line);  // a client command of a mod (true when one answered it)
    [[nodiscard]] jvm::HostState modHostState(int guiWidth, int guiHeight) const;
    void modKeyEvent(const KeyId& key, bool pressed);                       // a key a mod asked for was pressed or let go
    void tickTime();  // the clock: a tick of the day, unless the game rule advance_time is off
    void pumpRemote();
    // What LocalPlayer.sendChanges says each tick. A rider (`riding`) sends how it looks round and nothing of its position; one who steers a boat also sends the boat's place.
    void sendRemoteTick(const PlayerInput& input, bool riding = false, const BoatBody* steered = nullptr);
    // A tick of a player who sits in something the server tells of (ClientboundSetPassengersPacket): a boat this player is first to sit in is driven here with the game's own motion
    // (the server checks what is sent of it); anything else is where the server puts it. The rider is put on its seat either way.
    void tickRiding(const PlayerInput& input);
    [[nodiscard]] bool riding() const;
    void leaveVehicle();
    // Boat.clampRotation: a rider of a boat looks at most 105 degrees to either side of the way it points.
    void clampRiderYaw(float boatYaw);
    void renderRemotePlayers(const std::array<double, 3>& camPos);
    void renderTestMobs(const std::array<double, 3>& camPos);
    // The player or mob (or boat) that the line from the eye along the look meets first within `maxDistance`, or -1 (its entity id): what a click hits.
    // The entity under the crosshair (its number or -1), and how far along the look the ray reaches it.
    [[nodiscard]] int entityUnderCrosshair(const std::array<double, 3>& eye, const std::array<double, 3>& look, double maxDistance, double* distance = nullptr) const;
    void drawNameTags(GuiGraphics& g, int guiWidth, int guiHeight, const Camera& cam);
    void drawPlayerList(GuiGraphics& g, int guiWidth);
    // A flint and steel or a fire charge used on a block (TNT goes off, a campfire or candle lights, else a fire starts next to it). True if it did something.
    bool useFireStarter(const std::string& item, const BlockHit& hit);
    void interact(double dt, bool active);
    [[nodiscard]] Camera camera() const;
    [[nodiscard]] std::vector<std::string> debugLines() const;

    struct Benchmark {
        bool running = false;
        bool done = false;
        double seconds = 0.0;
        double elapsed = 0.0;
        std::vector<float> cpu;  // ms per frame
        std::vector<float> gpu;
        std::vector<float> frame;  // wall-clock ms between frames
        std::size_t startRss = 0;
        std::size_t peakRss = 0;
        std::size_t endRss = 0;
        std::string finalStats;
        std::string workerStats;  // where the worker threads spent their time (terrain, decoration, light, mesh)
    };

    ScreenServices& services_;
    const ResourceManager& resources_;
    Window& window_;
    LevelInfo info_;
    std::shared_ptr<const BlockRegistry> registry_;
    std::shared_ptr<const BlockSupport> support_;  // what plants and double blocks need to stay (null: the game files are missing)
    std::unique_ptr<PlayerPreview> playerPreview_;  // the player model drawn in the inventory screens (null if the game files lack it)
    double playerAgeTicks_ = 0.0;                    // game ticks since the world opened (the arms of the player in the inventory sway with it)
    BlockModels models_;
    ItemIcons itemIcons_;
    HandRenderer hand_;
    std::shared_ptr<const worldgen::OverworldGenerator> overworld_;  // the game-exact terrain (null: Superflat or the simple fallback)
    // A server makes the world, so there is no generator, but its chunks still name the game's biomes and their grass, leaves and water take their colours: the biome files are read
    // on their own for that.
    std::shared_ptr<const worldgen::BiomeRegistry> remoteBiomes_;
    WorldRenderer renderer_;
    Player player_;

    Inventory inventory_;
    std::unique_ptr<InventoryMenu> inventoryMenu_;
    SelectedItemHighlight highlight_;
    Vitals vitals_;
    std::shared_ptr<WorldEdits> edits_ = std::make_shared<WorldEdits>();
    std::filesystem::path savePath_;
    bool savingEnabled_ = true;
    double lastSaveTicks_ = 0.0;
    std::mt19937 rng_{std::random_device{}()};
    ItemEntities itemEntities_;
    std::array<DimensionState, 3> dims_;
    int dimension_ = 0;
    Arrival arrival_;
    int portalTicks_ = 0;
    float portalOverlay_ = 0.0f, portalOverlayO_ = 0.0f;
    bool portalLocked_ = false;  // arrived through a portal: it does not work again until the player has stepped out
    bool portalEdit_ = false;    // the portal code is changing blocks itself (their changes are not looked at again)
    bool dragonDead_ = false;
    bool dragonStarted_ = false;
    std::unique_ptr<Mobs> mobSim_;  // the mobs that live in this world (singleplayer)
    int sinceAttack_ = 100;         // ticks since the player last swung at a mob (the attack cooldown)
    StepClock stepClock_;
    bool wasInWater_ = false;
    int invulnerableTime_ = 0;   // ticks left of the protection after a hit (20 at a hit, only a stronger hit counts above 10)
    double scrollAccumulator_ = 0.0;  // ScrollWheelHandler: the wheel turned so far, in whole notches
    bool attackRawO_ = false, useRawO_ = false, attackLatched_ = false, useLatched_ = false;  // "Attack/Destroy" and "Use Item" as toggles
    float fovModifier_ = 1.0f, fovModifierO_ = 1.0f;  // Camera.fovModifier: the field of view widens when sprinting or flying, eased every tick
    int hurtTime_ = 0;           // ticks of the camera's tilt after a hit
    float lastHurt_ = 0.0f;      // the damage of the last hit
    float hurtDirection_ = 0.0f; // which way the view tilts, degrees (0 or 180)
    int airSupply_ = 300;      // LivingEntity.getAirSupply: 15 seconds of breath (the bubbles above the food bar), refilled four times as fast as it runs out
    int regenTimer_ = 0;       // FoodData.tickTimer: the slow and the fast healing and the starving count in it
    float exhaustion_ = 0.0f;  // FoodData.exhaustionLevel: at more than 4 the saturation (then the food) goes down by one
    bool dead_ = false;
    bool deathRequested_ = false;
    // The hearts blink after a hit (Hud.extractPlayerHealth's own bookkeeping).
    long long healthBlinkTime_ = 0;
    int lastHealthShown_ = 20;
    int displayHealth_ = 20;
    double lastHealthMillis_ = 0.0;
    // F5 and the body seen from outside.
    int perspective_ = 0;
    std::unique_ptr<PlayerBodyRenderer> body_;
    std::map<int, std::unique_ptr<Texture2D>> frameIcons_;  // the pictures of what is in item frames, by item
    std::map<int, Item> thrownItems_;  // the entity types that are drawn as an item (an ender pearl, a snowball ...), by their number in the server's registry
    std::unique_ptr<MobRenderer> mobs_;  // the mobs of a server (and, with FALCON_TEST_MOBS, a row of them for a look)
    float walkPosition_ = 0.0f, walkSpeed_ = 0.0f, walkSpeedOld_ = 0.0f;  // WalkAnimationState
    // ElytraAnimationState: where the wings of the player's own elytra are held (X, Y, Z angles) and where they were a tick ago.
    float elytraX_ = 0.2617994f, elytraY_ = 0.0f, elytraZ_ = -0.2617994f, elytraXO_ = 0.2617994f, elytraYO_ = 0.0f, elytraZO_ = -0.2617994f;
    void wearTestItem();                       // FALCON_TEST_WEAR=item: puts it on once the world is ready (a test aid, for looking at armor and elytra)
    // ---- eating and drinking (game_session_use.cpp): LivingEntity.startUsingItem / updatingUsingItem / completeUsingItem and Consumable ----
    bool usingItem_ = false;
    bool usingSpyglass_ = false;
    float lastAspect_ = 16.0f / 9.0f;  // the window's width over its height, as of the last frame drawn (the zoom's cone is as wide as the view)
    [[nodiscard]] bool usingSpyglass() const { return usingSpyglass_; }
    void startUsingSpyglass();
    void stopUsingSpyglass();
    int useTicksLeft_ = 0;       // ticks of the use that are left
    int useDuration_ = 0;        // the whole use
    int usedSlot_ = -1;          // the hotbar slot of the item that is used
    bool useHeld_ = false;       // the use key is down (set every frame)
    [[nodiscard]] bool canConsume(const ItemStack& stack) const;  // Consumable.canConsume: food only when hungry (or always eatable, or in creative); anything else always
    void startUsingItem();
    void stopUsingItem();
    void tickUsingItem();
    void completeUsingItem();
    void emitUseEffects(const ItemType& type);                    // Consumable.emitParticlesAndSounds: the sound of a bite or a sip
    CloakState cloak_;                          // where the cape of the player's own body hangs from (ClientAvatarState)
    std::string localCapeHash_;                 // the cape of the signed-in account, once its picture has come
    std::unique_ptr<Texture2D> testCape_;       // FALCON_TEST_CAPE=<png>: a cape for every body that has none (a test aid)
    [[nodiscard]] const Texture2D* capeFor(const std::string& hash);  // the picture of a cape (by its hash) or the test cape; null for none
    void tickGlide();                          // what the glide of the player asks of the game (wear on the elytra, a crash, a server told)
    [[nodiscard]] bool gliderUsable();   // elytra are worn and can take one more point of wear (LivingEntity.canGlideUsing)
    float swimAmount_ = 0.0f, swimAmountOld_ = 0.0f;                        // Player.swimAmount: how far into the swimming pose (0 to 1)
    float bodyYaw_ = 0.0f, bodyYawOld_ = 0.0f;
    double hudTickAccumulator_ = 0.0;
    int framebufferWidth_ = 0, framebufferHeight_ = 0;
    // The right-hand side of the debug screen: the graphics lines never change (read once), the memory of the PC is read once a second.
    std::vector<std::string> gpuLines_;
    SystemMemory systemMemory_;
    double systemMemoryAge_ = 1.0e9;
    bool advancedTooltips_ = false;
    int modGuiWidth_ = 427, modGuiHeight_ = 240, modFramebufferWidth_ = 854, modFramebufferHeight_ = 480;  // the window, as the mods are told of it
    bool showHitboxes_ = false;      // F3 + B
    bool showChunkBorders_ = false;  // F3 + G
    int droppedCount_ = 0;
    std::optional<BlockHit> target_;
    bool ready_ = false;
    float progress_ = 0.0f;          // 0..1: the chunks around the spawn and the player's section
    float smoothedProgress_ = 0.0f;  // what the bar shows: eases towards progress_ like the game's (0.2 of the gap per tick)
    double loadElapsed_ = 0.0;       // seconds spent on the loading screen
    double minLoadSeconds_ = 0.0;    // test hook: hold the loading screen at least this long
    bool showDebug_ = false;
    bool debugHeld_ = false;            // F3 is down
    bool debugCombo_ = false;           // a chord (F3 + something) was used while it was down: letting go must not toggle the screen
    bool switcherRequested_ = false;
    int previousGameType_ = -1;
    bool hudHidden_ = false;
    ChatComponent chat_;
    const Font* chatFont_ = nullptr;  // grabbed from the HUD's graphics, so a message can be wrapped when sent
    bool sneakLatched_ = false;   // "toggle" mode for Sneak and Sprint
    bool sprintLatched_ = false;
    double tickAccumulator_ = 0.0;  // seconds not yet turned into game ticks
    FluidSimulator lavaFluids_{FluidKind::Lava};  // the lava that flows (slower, and not as far, as water)
    std::vector<std::array<int, 3>> fluidTouched_;  // the cells the two simulators changed this tick, for what water and lava do to each other
    FluidSimulator fluids_;         // the water that flows: wakes when a block next to it changes, runs once a game tick
    int waterVisionTicks_ = 0;      // ticks the eyes have been under water (0..600): the underwater fog clears as it grows
    WaterFogTable waterFogs_;       // each biome's water fog colour and reach
    double partialTick_ = 0.0;      // how far the frame is into the next tick (0..1), for the interpolated camera
    std::array<double, 3> spawn_{};
    double breakCooldown_ = 0.0;
    // Digging in survival (the game's MultiPlayerGameMode): the block being dug, how much of it is gone, the game ticks spent on it, the pause after one broke.
    std::optional<std::array<int, 3>> digPos_;
    int digFace_ = 0;  // the face of the block that is being dug (the server wants it)
    float digProgress_ = 0.0f;
    int digTicks_ = 0;
    int digDelay_ = 0;
    bool digHeld_ = false;  // the attack button was down last frame
    Item digItem_ = Item::Air;
    double digClock_ = 0.0;
    std::shared_ptr<const BlockBehaviours> behaviours_;  // which blocks fall, burn or explode (null: the game files are missing)
    std::unique_ptr<BlockPhysics> physics_;              // falling blocks, fire, TNT
    std::unique_ptr<Redstone> redstone_;                 // power, dust, repeaters, lamps, doors ...
    std::shared_ptr<const CommandTree> commands_;
    mutable std::map<std::string, std::vector<std::string>> registryNames_;  // the ids of a registry, for the argument checks and the suggestions
    std::map<std::string, std::string> gameRules_;                           // game rules set by /gamerule (the others have their defaults)
    std::map<std::string, std::int64_t> stats_;                              // the statistics (see addStat), saved with the world
    std::set<std::string> advancements_;                                     // the advancements that are done ("story/mine_stone") and the criteria that are met ("story/mine_stone#cobblestone"), saved
    std::shared_ptr<const AdvancementTree> advancementTree_;                 // what the game's advancements are (read once)
    std::array<double, 3> statPosition_{0.0, 0.0, 0.0};                      // where the player was a tick ago: the distances are the steps from it
    bool statPositionKnown_ = false;
    int difficulty_ = 2;
    long long dayTime_ = 0;   // a new world starts at sunrise, as the game's do
    long long gameTime_ = 0;
    bool timeFrozen_ = false;  // --time
    bool raining_ = false;     // /weather rain: the drops are drawn around the player out to the Weather Radius
    bool timePaused_ = false;  // /time pause
    double timeRate_ = 1.0;    // /time rate: how fast the day goes (1: normal)
    double timeFraction_ = 0.0;  // the part of a tick a rate other than 1 has gathered
    std::shared_ptr<const daycycle::Timeline> timeline_;  // the game's own day (null: the closed-form approximation)
    int burnTicks_ = 0;  // how long the player is still on fire
    std::unique_ptr<BlockTags> blockTags_;
    std::filesystem::path dataRoot_;
    std::unique_ptr<net::MultiplayerClient> remote_;  // the server this session plays on (null: a world of this computer)
    net::RemoteHandler remoteHandler_;
    std::string remoteLost_;
    int lastInputFlags_ = 0;
    std::unique_ptr<net::SkinFetcher> localSkins_;                                   // the signed-in account's own skin, looked up at Mojang
    std::map<std::string, std::unique_ptr<Texture2D>> skinTextures_;                  // downloaded skins by their hash
    std::map<std::string, bool> skinSlim_;
    std::string localSkinHash_;                                                        // the skin of this player (empty: the plain Steve)
    std::unique_ptr<RemoteMenu> remoteMenu_;  // the container a server opened (kept after it is closed, so that nothing points at a menu that is gone)
    std::string remoteMenuTitle_;
    BlockStorages storages_;                                // what the chests, furnaces ... of this world hold
    SimpleContainer enderChest_{27};                        // the player's own ender chest (the same behind every ender chest block)
    std::unique_ptr<CompoundContainer> openCompound_;       // the two halves of the double chest that is open
    std::optional<std::array<int, 3>> openStoragePos_;      // the block whose window is open (a furnace's numbers follow it)
    bool unsupportedHint_ = false;                          // "cannot show this window yet" was said for this click
    std::vector<std::string> menuTypeNames_;  // the names of the menu types by their number in the server's registry
    bool remoteMenuOpen_ = false, containerOpenRequested_ = false, containerCloseRequested_ = false;
    bool selfOnFire_ = false;                  // the server says the player burns
    int lastHeldSent_ = -1;                    // the hotbar slot the server knows is in the hand
    bool usingRemote_ = false;                 // an item is being used (eaten, drawn) on a server: the release must be told
    std::array<ItemStack, 41> remoteSlots_{};  // what the server thinks the inventory holds (to tell it only what differs)
    bool lastSprintSent_ = false;
    bool loadedSent_ = false;
    bool remoteTeleported_ = false;  // the server has put the player somewhere
    std::optional<BoatBody> boat_;   // the boat the player steers, worked out here
    int boatEntity_ = -1;            // its entity number
    int ridingEntity_ = -1;          // what the player sat in last tick (to see the moment of boarding)
    std::unique_ptr<MiningRules> mining_;
    std::unique_ptr<BlockLoot> loot_;
    double placeCooldown_ = 0.0;
    double fps_ = 0.0;
    double renderLogAge_ = 0.0;  // seconds since the log last said how the world draws (every ten while a world is open)
    double frameMillis_ = 0.0;
    double cpuMillis_ = 0.0;
    double fpsAccumulated_ = 0.0;
    int fpsFrames_ = 0;
    FrameStats frameStats_;
    bool pinnedPose_ = false;  // teleported: the camera stays where it was put
    bool trimPending_ = false;  // a burst of loading happened: give the memory it used back once things are quiet
    double quietSeconds_ = 0.0;
    Benchmark benchmark_;
};

}  // namespace falcon
