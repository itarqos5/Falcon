#pragma once

#include "apex/assets/resource_manager.hpp"
#include "apex/game/chat.hpp"
#include "apex/game/screen_services.hpp"
#include "apex/game/hotbar_hud.hpp"
#include "apex/platform/window.hpp"
#include "apex/render/block_icons.hpp"
#include "apex/render/block_models.hpp"
#include "apex/render/hand_renderer.hpp"
#include "apex/render/item_icons.hpp"
#include "apex/render/player_preview.hpp"
#include "apex/render/world_renderer.hpp"
#include "apex/ui/gui_graphics.hpp"
#include "apex/world/level_info.hpp"
#include "apex/world/player.hpp"
#include "apex/world/inventory.hpp"
#include "apex/world/menu.hpp"
#include "apex/world/fluids.hpp"
#include "apex/world/raycast.hpp"
#include "apex/world/water_fog.hpp"
#include "apex/world/block_support.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace apex {

// A world being played: the level and its renderer, the player, what they are looking at, the inventory (the hotbar is its
// first nine slots), and the HUD. The main loop calls update() and renderWorld() every frame while a world is open, and the in-game screen
// draws the HUD and passes keys and clicks in.
//
// This is a first, small singleplayer: a generated world (Superflat, or this client's own simple hills; see
// TerrainGenerator) to walk, fly and swim in, with blocks to break and place, an inventory and the E screens. There are no
// mobs, dropped items, health, light levels or day and night, and changes to the world are not saved. The player is simulated in game ticks, 20 a
// second (see Player); between ticks the camera shows the interpolated position. A paused game does not tick.
class GameSession {
public:
    GameSession(ScreenServices& services, const ResourceManager& resources, Window& window, const LevelInfo& info);

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
    void renderWorld(int framebufferWidth, int framebufferHeight);
    // The crosshair, hotbar and (F3) debug screen.
    void renderHud(GuiGraphics& graphics, int guiWidth, int guiHeight, bool chatFocused = false);
    // The game's "Loading terrain..." screen (LevelLoadingScreen), drawn instead of the HUD until ready(): the blurred panorama,
    // the text, a progress bar and a map of the chunks around the spawn.
    void renderLoading(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks);

    // The chat log and history. The chat screen draws it focused and adds to it; the HUD draws it faded.
    [[nodiscard]] ChatComponent& chat() { return chat_; }
    // Handles a line the player typed into the chat: remembers it, then shows it (a "/..." is treated as a command).
    void chatInput(const std::string& text);
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
    // Whether the game mode may be changed from here (Minecraft.canSwitchGameMode plus the /gamemode permission): the world has cheats
    // on (Create New World: Allow Cheats).
    [[nodiscard]] bool canSwitchGameMode() const { return ready_ && info_.allowCommands; }
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
    // Everything thrown out of a menu or with Q: no item entity exists yet, so it is only counted.
    [[nodiscard]] int droppedCount() const { return droppedCount_; }
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
    // The 20 ticks a second of the HUD (the fading name of the held item).
    void tickHud(double seconds);
    void drawHotbar(GuiGraphics& graphics, int guiWidth, int guiHeight);
    void collectDrop(Block broken);
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
    };

    ScreenServices& services_;
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
    WorldRenderer renderer_;
    Player player_;

    Inventory inventory_;
    std::unique_ptr<InventoryMenu> inventoryMenu_;
    SelectedItemHighlight highlight_;
    double hudTickAccumulator_ = 0.0;
    bool advancedTooltips_ = false;
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
    FluidSimulator fluids_;         // the water that flows: wakes when a block next to it changes, runs once a game tick
    int waterVisionTicks_ = 0;      // ticks the eyes have been under water (0..600): the underwater fog clears as it grows
    WaterFogTable waterFogs_;       // each biome's water fog colour and reach
    double partialTick_ = 0.0;      // how far the frame is into the next tick (0..1), for the interpolated camera
    std::array<double, 3> spawn_{};
    double breakCooldown_ = 0.0;
    double placeCooldown_ = 0.0;
    double fps_ = 0.0;
    double frameMillis_ = 0.0;
    double cpuMillis_ = 0.0;
    double fpsAccumulated_ = 0.0;
    int fpsFrames_ = 0;
    bool pinnedPose_ = false;  // teleported: the camera stays where it was put
    bool trimPending_ = false;  // a burst of loading happened: give the memory it used back once things are quiet
    double quietSeconds_ = 0.0;
    Benchmark benchmark_;
};

}  // namespace apex
