#pragma once

#include "falcon/audio/sound_source.hpp"
#include "falcon/game/key_mappings.hpp"
#include "falcon/gfx/graphics_api.hpp"
#include "falcon/util/options_file.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace falcon {

// How often music plays in a world (options.music_frequency). In the menus only CONSTANT matters.
enum class MusicFrequency { Default, Frequent, Constant };

// When the "now playing" toast is shown (options.musicToast).
enum class MusicToast { Never, Pause, PauseAndToast };

enum class GraphicsPreset { Fast, Fancy, Fabulous, Custom };
// (GraphicsApi, the Graphics API setting, is in falcon/gfx/graphics_api.hpp: OpenGL or Vulkan, there is no "Default".)
enum class InactivityFpsLimit { Minimized, Afk };
enum class CloudStatus { Off, Fast, Fancy };
enum class ParticleStatus { All, Decreased, Minimal };
enum class TextureFiltering { None, Rgss, Anisotropic };
enum class AttackIndicator { Off, Crosshair, Hotbar };

enum class ChatVisibility { Full, System, Hidden };       // options.chat.visibility
enum class NarratorMode { Off, All, Chat, System };       // options.narrator
enum class PresenceSharing { None, Limited, All };        // options.sharePresence

// Player settings shared between screens, with the 26.3 defaults. Saved to falcon_options.txt in the
// game folder (never to the real options.txt) whenever a screen is left.
struct GameOptions {
    int fov = 70;                         // options.fov: 30..110, default 70 ("Normal")
    int menuBackgroundBlurriness = 5;     // 0..10, default 5: radius of the panorama blur in menus

    // ---- sound ----
    // Volume sliders (0..1), one per sound category; every default is 100%.
    std::array<double, kSoundSourceCount> soundVolume = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                                                         1.0, 1.0, 1.0, 1.0, 1.0};
    MusicFrequency musicFrequency = MusicFrequency::Default;
    MusicToast musicToast = MusicToast::Never;
    bool showSubtitles = false;           // "Closed Captions"
    bool directionalAudio = false;
    std::string audioDevice;              // empty = "System Default"

    // ---- resource packs ----
    // Selected resource packs, lowest priority first (as options.txt stores them). Empty until the
    // Resource Packs screen is first closed; the Default pack is implied.
    std::vector<std::string> resourcePacks;
    bool highContrast = false;            // toggled by selecting the High Contrast pack

    // ---- language ----
    std::string language = "en_us";       // the selected language code (options.txt "lang")
    bool forceUnicodeFont = false;
    bool japaneseGlyphVariants = false;

    // ---- skin customization ----
    // Cape, Jacket, Left Sleeve, Right Sleeve, Left Pant Leg, Right Pant Leg, Hat.
    std::array<bool, 7> modelParts = {true, true, true, true, true, true, true};
    bool mainHandRight = true;

    // ---- chat ----
    ChatVisibility chatVisibility = ChatVisibility::Full;
    bool chatColors = true;
    bool chatLinks = true;
    bool chatLinksPrompt = true;
    double chatOpacity = 1.0;
    double textBackgroundOpacity = 0.5;
    double chatScale = 1.0;
    double chatLineSpacing = 0.0;
    double chatDelay = 0.0;               // seconds, 0..6 in steps of 0.1
    double chatWidth = 1.0;
    double chatHeightFocused = 1.0;
    double chatHeightUnfocused = 0.4375;  // 70 / (180 - 20)
    NarratorMode narrator = NarratorMode::Off;
    bool autoSuggestions = true;          // "Command Suggestions"
    bool hideMatchedNames = true;
    bool reducedDebugInfo = false;
    bool onlyShowSecureChat = false;
    bool saveChatDrafts = false;

    // ---- accessibility ----
    bool backgroundForChatOnly = true;    // "Text Background: Chat" (false = Everywhere)
    double notificationDisplayTime = 1.0; // 0.5..10 in steps of 0.1
    bool bobView = true;
    double screenEffectScale = 1.0;
    double fovEffectScale = 1.0;
    double darknessEffectScale = 1.0;
    double damageTiltStrength = 1.0;
    double glintSpeed = 0.5;
    double glintStrength = 0.75;
    bool hideLightningFlash = false;
    bool darkMojangStudiosBackground = false;  // "Monochrome Logo"
    double panoramaSpeed = 1.0;           // the panorama's scroll speed
    bool hideSplashTexts = false;

    // ---- controls ----
    KeyBindings keyBindings;              // what each action is bound to (the Key Binds screen)

    // ---- multiplayer ----
    bool skipMultiplayerWarning = false;  // the "Do not show this screen again" box of the third-party warning
    std::string lastServer;               // the address last typed into Direct Connection (options.txt "lastServer")
    bool narratorHotkey = true;
    bool rotateWithMinecart = false;
    bool highContrastBlockOutline = false;

    // ---- video: display ----
    int framerateLimit = 120;             // 10..260 in steps of 10; 260 = unlimited
    bool enableVsync = true;
    InactivityFpsLimit inactivityFpsLimit = InactivityFpsLimit::Afk;
    int guiScale = 0;                     // 0 = auto
    bool fullscreen = false;
    bool exclusiveFullscreen = false;
    int exclusiveFullscreenMode = -1;     // index into the monitor's video modes; -1 = the current one
    double gamma = 0.5;                   // "Brightness"
    GraphicsApi graphicsApi = GraphicsApi::OpenGL;  // "Graphics API"; kept as "graphicsBackend:opengl" (the old "graphicsApi" number is still read)

    // ---- video: quality and performance (the 17 options a graphics preset sets, then the preset) ----
    GraphicsPreset graphicsPreset = GraphicsPreset::Fancy;
    int biomeBlendRadius = 2;
    int renderDistance = 12;
    int simulationDistance = 12;
    bool ambientOcclusion = true;         // "Smooth Lighting"
    CloudStatus cloudStatus = CloudStatus::Fancy;
    ParticleStatus particles = ParticleStatus::All;
    int mipmapLevels = 4;
    bool entityShadows = true;
    double entityDistanceScaling = 1.0;   // 0.5..5.0 in steps of 0.25
    int cloudRange = 128;                 // "Cloud Distance"
    bool cutoutLeaves = true;             // "See-Through Leaves"
    bool improvedTransparency = false;
    TextureFiltering textureFiltering = TextureFiltering::None;
    int maxAnisotropyBit = 2;             // 2^n x, n = 1..3
    int weatherRadius = 10;
    // Levels of detail beyond the loaded chunks (render/lod_renderer.hpp). The detail is "LOD View Strength": 0 Low Detail, 1 Medium Detail, 2 Full Detail.
    bool lodsEnabled = true;              // (the Fancy preset, which is the default, has them)
    int lodChunkRadius = 128;             // 64..8192 chunks
    int lodDetail = 1;

    // ---- video: the sky (the Sky page) ----
    bool skyEnabled = true;               // the sky itself: its colours, and the fog at the horizon is the sky's colour
    bool sunEnabled = true;
    bool moonEnabled = true;
    bool starsEnabled = true;

    // ---- video: preferences ----
    bool showAutosaveIndicator = true;
    bool vignette = true;
    AttackIndicator attackIndicator = AttackIndicator::Crosshair;
    double chunkSectionFadeInTime = 0.75; // seconds, 0..2 in steps of 0.05

    // ---- controls and mouse ----
    bool toggleCrouch = false;            // false = Hold
    bool toggleSprint = false;
    bool toggleAttack = false;
    bool toggleUse = false;
    bool autoJump = false;
    int sprintWindow = 7;                 // ticks, 0 = off
    bool quitShortcuts = true;
    bool operatorItemsTab = false;
    double mouseSensitivity = 0.5;        // "Sensitivity: 100%"
    double mouseWheelSensitivity = 1.0;   // 0.01..10, stored as a value (the slider is logarithmic)
    bool discreteMouseScroll = false;
    bool invertXMouse = false;
    bool invertYMouse = false;
    bool allowCursorChanges = true;

    // ---- online ----
    bool friendsListEnabled = false;      // needs a signed-in account, so off here
    bool allowFriendRequests = false;
    bool inGameNotification = false;
    PresenceSharing sharePresence = PresenceSharing::All;
    bool allowServerListing = true;
    bool realmsNotifications = true;

    [[nodiscard]] double volume(SoundSource source) const {
        return soundVolume[static_cast<std::size_t>(source)];
    }
    void setVolume(SoundSource source, double value) {
        soundVolume[static_cast<std::size_t>(source)] = std::clamp(value, 0.0, 1.0);
    }
    // Options.getFinalSoundSourceVolume: a category slider times the master slider.
    [[nodiscard]] double finalVolume(SoundSource source) const {
        return source == SoundSource::Master ? volume(SoundSource::Master)
                                             : volume(source) * volume(SoundSource::Master);
    }

    // Writes every setting into `file`, and reads them back (a missing or out-of-range value keeps
    // the default).
    void save(OptionsFile& file) const;
    void load(const OptionsFile& file);
};

}  // namespace falcon
