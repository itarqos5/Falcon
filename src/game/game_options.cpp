#include "apex/game/game_options.hpp"

#include <sstream>

namespace apex {
namespace {

constexpr const char* kPartKeys[7] = {"modelPart_cape", "modelPart_jacket", "modelPart_left_sleeve",
                                      "modelPart_right_sleeve", "modelPart_left_pants_leg",
                                      "modelPart_right_pants_leg", "modelPart_hat"};

// A value of a small enum stored as its number; anything out of range keeps the fallback.
template <class E>
E readEnum(const OptionsFile& f, const std::string& key, E fallback, int count) {
    const int v = f.getInt(key, static_cast<int>(fallback));
    return v >= 0 && v < count ? static_cast<E>(v) : fallback;
}

double readUnit(const OptionsFile& f, const std::string& key, double fallback, double lo = 0.0, double hi = 1.0) {
    return std::clamp(f.getDouble(key, fallback), lo, hi);
}

}  // namespace

void GameOptions::save(OptionsFile& f) const {
    f.set("fov", fov);
    f.set("menuBackgroundBlurriness", menuBackgroundBlurriness);

    for (std::size_t i = 0; i < soundVolume.size(); ++i) f.set("soundCategory_" + std::to_string(i), soundVolume[i]);
    f.set("musicFrequency", static_cast<int>(musicFrequency));
    f.set("musicToast", static_cast<int>(musicToast));
    f.set("showSubtitles", showSubtitles);
    f.set("directionalAudio", directionalAudio);
    f.set("soundDevice", audioDevice);

    std::string packs;
    for (const std::string& id : resourcePacks) packs += (packs.empty() ? "" : "|") + id;
    f.set("resourcePacks", packs);
    f.set("highContrast", highContrast);

    f.set("lang", language);
    f.set("forceUnicodeFont", forceUnicodeFont);
    f.set("japaneseGlyphVariants", japaneseGlyphVariants);

    for (std::size_t i = 0; i < modelParts.size(); ++i) f.set(kPartKeys[i], modelParts[i]);
    f.set("mainHand", mainHandRight ? std::string("right") : std::string("left"));

    f.set("chatVisibility", static_cast<int>(chatVisibility));
    f.set("chatColors", chatColors);
    f.set("chatLinks", chatLinks);
    f.set("chatLinksPrompt", chatLinksPrompt);
    f.set("chatOpacity", chatOpacity);
    f.set("textBackgroundOpacity", textBackgroundOpacity);
    f.set("chatScale", chatScale);
    f.set("chatLineSpacing", chatLineSpacing);
    f.set("chatDelay", chatDelay);
    f.set("chatWidth", chatWidth);
    f.set("chatHeightFocused", chatHeightFocused);
    f.set("chatHeightUnfocused", chatHeightUnfocused);
    f.set("narrator", static_cast<int>(narrator));
    f.set("autoSuggestions", autoSuggestions);
    f.set("hideMatchedNames", hideMatchedNames);
    f.set("reducedDebugInfo", reducedDebugInfo);
    f.set("onlyShowSecureChat", onlyShowSecureChat);
    f.set("saveChatDrafts", saveChatDrafts);

    f.set("backgroundForChatOnly", backgroundForChatOnly);
    f.set("notificationDisplayTime", notificationDisplayTime);
    f.set("bobView", bobView);
    f.set("screenEffectScale", screenEffectScale);
    f.set("fovEffectScale", fovEffectScale);
    f.set("darknessEffectScale", darknessEffectScale);
    f.set("damageTiltStrength", damageTiltStrength);
    f.set("glintSpeed", glintSpeed);
    f.set("glintStrength", glintStrength);
    f.set("hideLightningFlashes", hideLightningFlash);
    f.set("darkMojangStudiosBackground", darkMojangStudiosBackground);
    f.set("panoramaScrollSpeed", panoramaSpeed);
    f.set("hideSplashTexts", hideSplashTexts);
    keyBindings.save(f);
    f.set("skipMultiplayerWarning", skipMultiplayerWarning);
    f.set("lastServer", lastServer);
    f.set("narratorHotkey", narratorHotkey);
    f.set("rotateWithMinecart", rotateWithMinecart);
    f.set("highContrastBlockOutline", highContrastBlockOutline);

    f.set("maxFps", framerateLimit);
    f.set("enableVsync", enableVsync);
    f.set("inactivityFpsLimit", static_cast<int>(inactivityFpsLimit));
    f.set("guiScale", guiScale);
    f.set("fullscreen", fullscreen);
    f.set("exclusiveFullscreen", exclusiveFullscreen);
    f.set("fullscreenMode", exclusiveFullscreenMode);
    f.set("gamma", gamma);
    f.set("graphicsBackend", std::string(graphicsApiId(graphicsApi)));
    f.set("graphicsPreset", static_cast<int>(graphicsPreset));
    f.set("biomeBlendRadius", biomeBlendRadius);
    f.set("renderDistance", renderDistance);
    f.set("prioritizeChunkUpdates", static_cast<int>(prioritizeChunkUpdates));
    f.set("simulationDistance", simulationDistance);
    f.set("ao", ambientOcclusion);
    f.set("renderClouds", static_cast<int>(cloudStatus));
    f.set("particles", static_cast<int>(particles));
    f.set("mipmapLevels", mipmapLevels);
    f.set("entityShadows", entityShadows);
    f.set("entityDistanceScaling", entityDistanceScaling);
    f.set("cloudRange", cloudRange);
    f.set("cutoutLeaves", cutoutLeaves);
    f.set("improvedTransparency", improvedTransparency);
    f.set("textureFiltering", static_cast<int>(textureFiltering));
    f.set("maxAnisotropyBit", maxAnisotropyBit);
    f.set("weatherRadius", weatherRadius);
    f.set("showAutosaveIndicator", showAutosaveIndicator);
    f.set("vignette", vignette);
    f.set("attackIndicator", static_cast<int>(attackIndicator));
    f.set("chunkSectionFadeInTime", chunkSectionFadeInTime);
    f.set("toggleCrouch", toggleCrouch);
    f.set("toggleSprint", toggleSprint);
    f.set("toggleAttack", toggleAttack);
    f.set("toggleUse", toggleUse);
    f.set("autoJump", autoJump);
    f.set("sprintWindow", sprintWindow);
    f.set("quitShortcuts", quitShortcuts);
    f.set("operatorItemsTab", operatorItemsTab);
    f.set("mouseSensitivity", mouseSensitivity);
    f.set("mouseWheelSensitivity", mouseWheelSensitivity);
    f.set("discrete_mouse_scroll", discreteMouseScroll);
    f.set("invertXMouse", invertXMouse);
    f.set("invertYMouse", invertYMouse);
    f.set("allowCursorChanges", allowCursorChanges);

    f.set("friendsList", friendsListEnabled);
    f.set("allowFriendRequests", allowFriendRequests);
    f.set("inGameNotification", inGameNotification);
    f.set("sharePresence", static_cast<int>(sharePresence));
    f.set("allowServerListing", allowServerListing);
    f.set("realmsNotifications", realmsNotifications);
}

void GameOptions::load(const OptionsFile& f) {
    fov = std::clamp(f.getInt("fov", fov), 30, 110);
    menuBackgroundBlurriness = std::clamp(f.getInt("menuBackgroundBlurriness", menuBackgroundBlurriness), 0, 10);

    for (std::size_t i = 0; i < soundVolume.size(); ++i) {
        soundVolume[i] = readUnit(f, "soundCategory_" + std::to_string(i), soundVolume[i]);
    }
    musicFrequency = readEnum(f, "musicFrequency", musicFrequency, 3);
    musicToast = readEnum(f, "musicToast", musicToast, 3);
    showSubtitles = f.getBool("showSubtitles", showSubtitles);
    directionalAudio = f.getBool("directionalAudio", directionalAudio);
    audioDevice = f.getString("soundDevice", audioDevice);

    if (f.contains("resourcePacks")) {
        resourcePacks.clear();
        std::stringstream list(f.getString("resourcePacks"));
        for (std::string id; std::getline(list, id, '|');) {
            if (!id.empty()) resourcePacks.push_back(id);
        }
    }
    highContrast = f.getBool("highContrast", highContrast);

    language = f.getString("lang", language);
    if (language.empty()) language = "en_us";
    forceUnicodeFont = f.getBool("forceUnicodeFont", forceUnicodeFont);
    japaneseGlyphVariants = f.getBool("japaneseGlyphVariants", japaneseGlyphVariants);

    for (std::size_t i = 0; i < modelParts.size(); ++i) modelParts[i] = f.getBool(kPartKeys[i], modelParts[i]);
    mainHandRight = f.getString("mainHand", mainHandRight ? "right" : "left") != "left";

    chatVisibility = readEnum(f, "chatVisibility", chatVisibility, 3);
    chatColors = f.getBool("chatColors", chatColors);
    chatLinks = f.getBool("chatLinks", chatLinks);
    chatLinksPrompt = f.getBool("chatLinksPrompt", chatLinksPrompt);
    chatOpacity = readUnit(f, "chatOpacity", chatOpacity);
    textBackgroundOpacity = readUnit(f, "textBackgroundOpacity", textBackgroundOpacity);
    chatScale = readUnit(f, "chatScale", chatScale);
    chatLineSpacing = readUnit(f, "chatLineSpacing", chatLineSpacing);
    chatDelay = readUnit(f, "chatDelay", chatDelay, 0.0, 6.0);
    chatWidth = readUnit(f, "chatWidth", chatWidth);
    chatHeightFocused = readUnit(f, "chatHeightFocused", chatHeightFocused);
    chatHeightUnfocused = readUnit(f, "chatHeightUnfocused", chatHeightUnfocused);
    narrator = readEnum(f, "narrator", narrator, 4);
    autoSuggestions = f.getBool("autoSuggestions", autoSuggestions);
    hideMatchedNames = f.getBool("hideMatchedNames", hideMatchedNames);
    reducedDebugInfo = f.getBool("reducedDebugInfo", reducedDebugInfo);
    onlyShowSecureChat = f.getBool("onlyShowSecureChat", onlyShowSecureChat);
    saveChatDrafts = f.getBool("saveChatDrafts", saveChatDrafts);

    backgroundForChatOnly = f.getBool("backgroundForChatOnly", backgroundForChatOnly);
    notificationDisplayTime = readUnit(f, "notificationDisplayTime", notificationDisplayTime, 0.5, 10.0);
    bobView = f.getBool("bobView", bobView);
    screenEffectScale = readUnit(f, "screenEffectScale", screenEffectScale);
    fovEffectScale = readUnit(f, "fovEffectScale", fovEffectScale);
    darknessEffectScale = readUnit(f, "darknessEffectScale", darknessEffectScale);
    damageTiltStrength = readUnit(f, "damageTiltStrength", damageTiltStrength);
    glintSpeed = readUnit(f, "glintSpeed", glintSpeed);
    glintStrength = readUnit(f, "glintStrength", glintStrength);
    hideLightningFlash = f.getBool("hideLightningFlashes", hideLightningFlash);
    darkMojangStudiosBackground = f.getBool("darkMojangStudiosBackground", darkMojangStudiosBackground);
    panoramaSpeed = readUnit(f, "panoramaScrollSpeed", panoramaSpeed);
    hideSplashTexts = f.getBool("hideSplashTexts", hideSplashTexts);
    keyBindings.load(f);
    skipMultiplayerWarning = f.getBool("skipMultiplayerWarning", skipMultiplayerWarning);
    lastServer = f.getString("lastServer", lastServer);
    narratorHotkey = f.getBool("narratorHotkey", narratorHotkey);
    rotateWithMinecart = f.getBool("rotateWithMinecart", rotateWithMinecart);
    highContrastBlockOutline = f.getBool("highContrastBlockOutline", highContrastBlockOutline);

    framerateLimit = std::clamp(f.getInt("maxFps", framerateLimit), 10, 260);
    enableVsync = f.getBool("enableVsync", enableVsync);
    inactivityFpsLimit = readEnum(f, "inactivityFpsLimit", inactivityFpsLimit, 2);
    guiScale = std::clamp(f.getInt("guiScale", guiScale), 0, 64);
    fullscreen = f.getBool("fullscreen", fullscreen);
    exclusiveFullscreen = f.getBool("exclusiveFullscreen", exclusiveFullscreen);
    exclusiveFullscreenMode = std::max(-1, f.getInt("fullscreenMode", exclusiveFullscreenMode));
    gamma = readUnit(f, "gamma", gamma);
    if (f.contains("graphicsBackend")) {
        graphicsApi = parseGraphicsApi(f.getString("graphicsBackend"), graphicsApi);
    } else if (f.contains("graphicsApi")) {
        // The old number had a "Default" choice: 0 default, 1 OpenGL, 2 Vulkan. Only Vulkan is a choice that changes anything.
        graphicsApi = f.getInt("graphicsApi", 0) == 2 ? GraphicsApi::Vulkan : GraphicsApi::OpenGL;
    }
    graphicsPreset = readEnum(f, "graphicsPreset", graphicsPreset, 4);
    biomeBlendRadius = std::clamp(f.getInt("biomeBlendRadius", biomeBlendRadius), 0, 7);
    renderDistance = std::clamp(f.getInt("renderDistance", renderDistance), 2, 32);
    prioritizeChunkUpdates = readEnum(f, "prioritizeChunkUpdates", prioritizeChunkUpdates, 3);
    simulationDistance = std::clamp(f.getInt("simulationDistance", simulationDistance), 5, 32);
    ambientOcclusion = f.getBool("ao", ambientOcclusion);
    cloudStatus = readEnum(f, "renderClouds", cloudStatus, 3);
    particles = readEnum(f, "particles", particles, 3);
    mipmapLevels = std::clamp(f.getInt("mipmapLevels", mipmapLevels), 0, 4);
    entityShadows = f.getBool("entityShadows", entityShadows);
    entityDistanceScaling = readUnit(f, "entityDistanceScaling", entityDistanceScaling, 0.5, 5.0);
    cloudRange = std::clamp(f.getInt("cloudRange", cloudRange), 2, 128);
    cutoutLeaves = f.getBool("cutoutLeaves", cutoutLeaves);
    improvedTransparency = f.getBool("improvedTransparency", improvedTransparency);
    textureFiltering = readEnum(f, "textureFiltering", textureFiltering, 3);
    maxAnisotropyBit = std::clamp(f.getInt("maxAnisotropyBit", maxAnisotropyBit), 1, 3);
    weatherRadius = std::clamp(f.getInt("weatherRadius", weatherRadius), 3, 10);
    showAutosaveIndicator = f.getBool("showAutosaveIndicator", showAutosaveIndicator);
    vignette = f.getBool("vignette", vignette);
    attackIndicator = readEnum(f, "attackIndicator", attackIndicator, 3);
    chunkSectionFadeInTime = readUnit(f, "chunkSectionFadeInTime", chunkSectionFadeInTime, 0.0, 2.0);
    toggleCrouch = f.getBool("toggleCrouch", toggleCrouch);
    toggleSprint = f.getBool("toggleSprint", toggleSprint);
    toggleAttack = f.getBool("toggleAttack", toggleAttack);
    toggleUse = f.getBool("toggleUse", toggleUse);
    autoJump = f.getBool("autoJump", autoJump);
    sprintWindow = std::clamp(f.getInt("sprintWindow", sprintWindow), 0, 10);
    quitShortcuts = f.getBool("quitShortcuts", quitShortcuts);
    operatorItemsTab = f.getBool("operatorItemsTab", operatorItemsTab);
    mouseSensitivity = readUnit(f, "mouseSensitivity", mouseSensitivity);
    mouseWheelSensitivity = readUnit(f, "mouseWheelSensitivity", mouseWheelSensitivity, 0.01, 10.0);
    discreteMouseScroll = f.getBool("discrete_mouse_scroll", discreteMouseScroll);
    invertXMouse = f.getBool("invertXMouse", invertXMouse);
    invertYMouse = f.getBool("invertYMouse", invertYMouse);
    allowCursorChanges = f.getBool("allowCursorChanges", allowCursorChanges);

    friendsListEnabled = f.getBool("friendsList", friendsListEnabled);
    allowFriendRequests = f.getBool("allowFriendRequests", allowFriendRequests);
    inGameNotification = f.getBool("inGameNotification", inGameNotification);
    sharePresence = readEnum(f, "sharePresence", sharePresence, 3);
    allowServerListing = f.getBool("allowServerListing", allowServerListing);
    realmsNotifications = f.getBool("realmsNotifications", realmsNotifications);
}

}  // namespace apex
