#include "falcon/game/create_world_screen.hpp"
#include "falcon/util/log.hpp"

#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

namespace falcon {
namespace {

constexpr int kTabCountConst = 3;
constexpr int kTabBarMaxWidth = 400;
constexpr int kTabBarMargin = 14;
constexpr std::uint32_t kSeparatorLight = 0x33FFFFFFu;  // header/footer separator rows
constexpr std::uint32_t kSeparatorDark = 0xBF000000u;
constexpr std::uint32_t kMenuTint = 0x40000000u;       // menu_background.png: black at 25%

// Mth.roundToward(value, 2) for a value >= 0: the next even number.
constexpr int roundToEven(int value) { return (value + 1) / 2 * 2; }

constexpr int kKeyTab = 258;
constexpr int kKeyEnter = 257;
constexpr int kKeypadEnter = 335;
constexpr int kModShift = 1;
constexpr int kModControl = 2;

// MenuTabBar: three tabs along the top. The selected tab is an open frame that runs into the
// content below; the others are closed boxes sitting in the header line.
class TabBar : public Widget {
public:
    TabBar(std::vector<std::string> titles, int selected, int tabX, int tabWidth, int screenWidth)
        : Widget(0, 0, screenWidth, kCreateWorldTabBarHeight),
          titles_(std::move(titles)),
          selected_(selected),
          tabX_(tabX),
          tabWidth_(tabWidth) {}

    std::function<void(int tab)> onSelect;

    [[nodiscard]] bool clicksOnPress() const override { return false; }  // the screen plays the click on a change

    bool mouseClicked(double px, double py, int button) override {
        const int tab = tabAt(px, py);
        if (button != 0 || tab < 0) return false;
        if (tab != selected_ && onSelect) onSelect(tab);
        return true;
    }

protected:
    [[nodiscard]] CursorType hoverCursor() const override {
        return tabAt(mouseX_, mouseY_) >= 0 ? CursorType::PointingHand : CursorType::Default;
    }

    void renderContents(GuiGraphics& graphics) override {
        // The header line (white 20%, then black 75%) runs across the screen except under the tabs.
        const int tabsEnd = tabX_ + tabWidth_ * static_cast<int>(titles_.size());
        const int lineY = kCreateWorldTabBarHeight - 2;
        for (const auto& [from, to] : {std::pair{0, tabX_}, std::pair{tabsEnd, width}}) {
            graphics.fill(from, lineY, to, lineY + 1, kSeparatorLight);
            graphics.fill(from, lineY + 1, to, lineY + 2, kSeparatorDark);
        }

        const int hovered = tabAt(mouseX_, mouseY_);
        for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
            const int tabLeft = tabX_ + i * tabWidth_;
            const bool selected = i == selected_;
            const bool hover = i == hovered && isHovered();
            const char* sprite = selected ? (hover ? "widget/tab_selected_highlighted" : "widget/tab_selected")
                                          : (hover ? "widget/tab_highlighted" : "widget/tab");
            if (selected) {  // the menu tint also fills the inside of the open frame
                graphics.fill(tabLeft + 2, 2, tabLeft + tabWidth_ - 2, kCreateWorldTabBarHeight, kMenuTint);
            }
            graphics.blitSprite(sprite, tabLeft, 0, tabWidth_, kCreateWorldTabBarHeight);

            const std::string& title = titles_[static_cast<std::size_t>(i)];
            const int textWidth = graphics.textWidth(title);
            graphics.text(title, static_cast<float>(tabLeft + tabWidth_ / 2 - textWidth / 2),
                          static_cast<float>(selected ? 8 : 10), kWhite);
            if (selected) {  // the underline under the title
                const int underline = std::min(textWidth, tabWidth_ - 4);
                const int ux = tabLeft + (tabWidth_ - underline) / 2;
                graphics.fill(ux, lineY, ux + underline, lineY + 1, kWhite);
            }
        }
    }

private:
    [[nodiscard]] int tabAt(double px, double py) const {
        if (py < 0 || py >= kCreateWorldTabBarHeight || px < tabX_) return -1;
        const int index = (static_cast<int>(px) - tabX_) / tabWidth_;
        return index < static_cast<int>(titles_.size()) ? index : -1;
    }

    std::vector<std::string> titles_;
    int selected_;
    int tabX_;
    int tabWidth_;
};

const char* const kModeIds[] = {"survival", "hardcore", "creative"};
const char* const kDifficultyIds[] = {"peaceful", "easy", "normal", "hard"};
const char* const kTypeIds[] = {"normal", "flat", "large_biomes", "amplified", "single_biome_surface"};

}  // namespace

CreateWorldLayout computeCreateWorldLayout(int width, int height) {
    CreateWorldLayout l;
    const int total = std::min(kTabBarMaxWidth, width) - 2 * kTabBarMargin;
    l.tabWidth = roundToEven(total / kTabCountConst);
    l.tabX = roundToEven((width - total) / 2);

    // Footer: the two 150 wide buttons with 8 between them, centred in a 33 tall strip.
    const int footerX = static_cast<int>(0.5f * static_cast<float>(width - 308));
    const int footerY = height - kCreateWorldFooterHeight + 7;  // Math.round(0.5 * (33 - 20))
    l.create = {footerX, footerY, 150, 20};
    l.cancel = {footerX + 158, footerY, 150, 20};
    l.footerSeparatorY = height - kCreateWorldFooterHeight - 2;
    l.tabArea = {0, kCreateWorldTabBarHeight, width,
                 height - kCreateWorldFooterHeight - kCreateWorldTabBarHeight};
    return l;
}

Rect createWorldTabGrid(const CreateWorldLayout& layout, int gridWidth, int gridHeight) {
    const Rect& area = layout.tabArea;
    const int x = area.x + static_cast<int>(0.5f * static_cast<float>(area.width - gridWidth));
    const int y = area.y + static_cast<int>(0.16666667f * static_cast<float>(area.height - gridHeight));
    return {x, y, gridWidth, gridHeight};
}

CreateWorldScreen::CreateWorldScreen(ScreenNavigator& navigator, ScreenServices& services, Created onCreated)
    : Screen(navigator), services_(services), onCreated_(std::move(onCreated)) {
    settings_.name = services.language.get("selectWorld.newWorld");
}

std::string CreateWorldScreen::optionLabel(const std::string& nameKey, const std::string& value) const {
    return formatLanguageString(text("options.generic_value"), {text(nameKey), value});
}

std::string CreateWorldScreen::targetFolder() const {
    return findAvailableWorldFolder(savesDir(), settings_.name, text("selectWorld.newWorld"));
}

CycleButton& CreateWorldScreen::addCycle(const Rect& rect, int count, int index,
                                         std::function<std::string(int)> label, std::function<void(int)> onChange) {
    auto& button = addWidget<CycleButton>(rect.x, rect.y, rect.width, rect.height, count, index, std::move(label),
                                          [this] { return navigator_.isShiftDown(); });
    button.onChange = [this, change = std::move(onChange)](int i) {
        change(i);
        refresh();
    };
    return button;
}

CycleButton& CreateWorldScreen::addToggle(const Rect& rect, bool on, std::function<void(bool)> onChange,
                                          const char* nameKey) {
    return addCycle(rect, 2, on ? 0 : 1,
                    [this, nameKey](int i) {
                        const std::string state = text(i == 0 ? "options.on" : "options.off");
                        return nameKey != nullptr ? optionLabel(nameKey, state) : state;  // "Allow Cheats: ON" or "ON"
                    },
                    [change = std::move(onChange)](int i) { change(i == 0); });
}

void CreateWorldScreen::init() {
    layout_ = computeCreateWorldLayout(width_, height_);

    auto& bar = addWidget<TabBar>(
        std::vector<std::string>{text("createWorld.tab.game.title"), text("createWorld.tab.world.title"),
                                 text("createWorld.tab.more.title")},
        tab_, layout_.tabX, layout_.tabWidth, width_);
    bar.onSelect = [this](int tab) { requestTab(tab, true); };

    addButton(text("selectWorld.create"), layout_.create, [this] { create(); });
    addButton(text("gui.cancel"), layout_.cancel, [this] { onClose(); });
    contentStart_ = widgets_.size();

    buildContent();
}

void CreateWorldScreen::buildContent() {
    widgets_.erase(widgets_.begin() + static_cast<std::ptrdiff_t>(contentStart_), widgets_.end());  // the previous tab's widgets
    nameEdit_ = nullptr;
    modeButton_ = difficultyButton_ = cheatsButton_ = typeButton_ = structuresButton_ = bonusChestButton_ = nullptr;
    customizeButton_ = nullptr;
    customizeLocked_ = nullptr;

    switch (tab_) {
        case kWorldTab: buildWorldTab(createWorldTabGrid(layout_, 310, 117)); break;
        case kMoreTab: buildMoreTab(createWorldTabGrid(layout_, 210, 76)); break;
        default: buildGameTab(createWorldTabGrid(layout_, 210, 117)); break;
    }
    refresh();
}

void CreateWorldScreen::buildGameTab(const Rect& g) {
    // World Name: the label and the box are one 208 wide block, centred in the 210 wide column.
    addLabel(text("selectWorld.enterName"), g.x + 1, g.y);
    nameEdit_ = &addEditBox({g.x + 1, g.y + 13, 208, 20}, "", 32);
    nameEdit_->setText(settings_.name);
    nameEdit_->onChange = [this](const std::string& value) {
        settings_.name = value;
        refresh();
    };
    if (focusName_) {  // typing starts in the name box
        setFocus(nameEdit_);
        focusName_ = false;
    }

    modeButton_ = &addCycle({g.x, g.y + 41, 210, 20}, 3, static_cast<int>(settings_.mode),
                            [this](int i) { return optionLabel("selectWorld.gameMode", text(std::string("selectWorld.gameMode.") + kModeIds[i])); },
                            [this](int i) { settings_.mode = static_cast<NewWorldMode>(i); });
    difficultyButton_ = &addCycle({g.x, g.y + 69, 210, 20}, 4, static_cast<int>(settings_.effectiveDifficulty()),
                                  [this](int i) { return optionLabel("options.difficulty", text(std::string("options.difficulty.") + kDifficultyIds[i])); },
                                  [this](int i) { settings_.difficulty = static_cast<NewWorldDifficulty>(i); });
    // The cheats button carries its name ("Allow Cheats: ON"), unlike the short switches of the World tab.
    cheatsButton_ = &addToggle({g.x, g.y + 97, 210, 20}, settings_.effectiveAllowCommands(),
                               [this](bool on) { settings_.allowCommands = on; }, "selectWorld.allowCommands");
    // (26.3 is a stable version: there is no Experiments button on this tab; it is on the More tab.)
}

void CreateWorldScreen::buildWorldTab(const Rect& g) {
    typeButton_ = &addCycle({g.x, g.y, 150, 20}, 5, static_cast<int>(settings_.type),
                            [this](int i) { return optionLabel("selectWorld.mapType", text(std::string("generator.minecraft.") + kTypeIds[i])); },
                            [this](int i) { settings_.type = static_cast<NewWorldType>(i); });

    // Customize opens the Superflat / Single Biome editors in the game. Neither editor exists here: for
    // those two types the button is shown padlocked, for every other type it is the plain grey button.
    const Rect customize{g.x + 160, g.y, 150, 20};
    customizeButton_ = &addButton(text("selectWorld.customizeType"), customize);
    customizeButton_->active = false;
    customizeLocked_ = &addLockedButton(text("selectWorld.customizeType"), customize);

    addLabel(text("selectWorld.enterSeed"), g.x, g.y + 28);
    auto& seed = addEditBox({g.x, g.y + 41, 308, 20}, text("selectWorld.seedInfo"), 32);
    seed.setText(settings_.seedText);
    seed.onChange = [this](const std::string& value) { settings_.seedText = value; };

    // The two switches: a label on the left, a 44 wide ON/OFF button on the right edge.
    addLabel(text("selectWorld.mapFeatures"), g.x, g.y + 79);
    structuresButton_ = &addToggle({g.x + 266, g.y + 73, 44, 20}, settings_.generateStructures,
                                   [this](bool on) { settings_.generateStructures = on; });
    addLabel(text("selectWorld.bonusItems"), g.x, g.y + 103);
    bonusChestButton_ = &addToggle({g.x + 266, g.y + 97, 44, 20}, settings_.effectiveBonusChest(),
                                   [this](bool on) { settings_.bonusChest = on; });
}

void CreateWorldScreen::buildMoreTab(const Rect& g) {
    // Game Rules, Experiments and Data Packs lead to screens this client does not have.
    addLockedButton(text("selectWorld.gameRules"), {g.x, g.y, 210, 20});
    addLockedButton(text("selectWorld.experiments"), {g.x, g.y + 28, 210, 20});
    addLockedButton(text("selectWorld.dataPacks"), {g.x, g.y + 56, 210, 20});
}

void CreateWorldScreen::refresh() {
    const bool hardcore = settings_.hardcore();

    if (nameEdit_ != nullptr) {
        nameEdit_->tooltip = formatLanguageString(text("selectWorld.targetFolder"), {targetFolder()});
    }
    if (modeButton_ != nullptr) {
        modeButton_->setValues(3, static_cast<int>(settings_.mode));
        modeButton_->tooltip = text(std::string("selectWorld.gameMode.") + kModeIds[static_cast<int>(settings_.mode)] + ".info");
    }
    if (difficultyButton_ != nullptr) {
        const int d = static_cast<int>(settings_.effectiveDifficulty());
        difficultyButton_->setValues(4, d);
        difficultyButton_->active = !hardcore;
        difficultyButton_->tooltip = text(std::string("options.difficulty.") + kDifficultyIds[d] + ".info");
    }
    if (cheatsButton_ != nullptr) {
        cheatsButton_->setValues(2, settings_.effectiveAllowCommands() ? 0 : 1);
        cheatsButton_->active = !hardcore;
        cheatsButton_->tooltip = text("selectWorld.allowCommands.info");
    }
    if (typeButton_ != nullptr) {
        typeButton_->setValues(5, static_cast<int>(settings_.type));
        typeButton_->tooltip = settings_.type == NewWorldType::Amplified ? text("generator.minecraft.amplified.info") : "";
    }
    if (customizeButton_ != nullptr && customizeLocked_ != nullptr) {
        const bool hasEditor = settings_.type == NewWorldType::Superflat || settings_.type == NewWorldType::SingleBiome;
        customizeButton_->visible = !hasEditor;
        customizeLocked_->visible = hasEditor;
    }
    if (structuresButton_ != nullptr) {
        structuresButton_->setValues(2, settings_.generateStructures ? 0 : 1);
        structuresButton_->tooltip = text("selectWorld.mapFeatures.info");
    }
    if (bonusChestButton_ != nullptr) {
        bonusChestButton_->setValues(2, settings_.effectiveBonusChest() ? 0 : 1);
        bonusChestButton_->active = !hardcore;
    }
}

void CreateWorldScreen::requestTab(int tab, bool sound) {
    if (tab < 0 || tab >= kTabCount || tab == tab_) return;
    if (sound) navigator_.playButtonClick();
    pendingTab_ = tab;
}

void CreateWorldScreen::create() {
    std::mt19937_64 rng{std::random_device{}()};
    const std::int64_t seed = parseWorldSeed(settings_.seedText, rng);
    const std::string folder = targetFolder();
    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    std::string error;
    if (!createWorldFolder(savesDir(), folder, settings_, seed, now, error)) {
        // The game shows a toast here; this client has none yet, so the screen simply stays open.
        falcon::log::line("could not create the world '%s': %s\n", folder.c_str(), error.c_str());
        std::fflush(stdout);
        return;
    }
    falcon::log::line("created world '%s' in %s (seed %lld)\n",
                folder.c_str(), savesDir().string().c_str(), static_cast<long long>(seed));
    std::fflush(stdout);
    navigator_.pop();
    if (onCreated_) onCreated_(folder);
}

bool CreateWorldScreen::keyPressed(int key, int mods) {
    // Ctrl+Tab / Ctrl+Shift+Tab move between tabs, Ctrl+1..9 and Ctrl+0 jump to one (the last tab for
    // digits past the end).
    if ((mods & kModControl) != 0) {
        if (key == kKeyTab) {
            const int step = (mods & kModShift) != 0 ? kTabCount - 1 : 1;
            requestTab((tab_ + step) % kTabCount, true);
            return true;
        }
        if (key >= '0' && key <= '9') {
            const int index = ((key - '0') - 1 + 10) % 10;
            requestTab(std::min(index, kTabCount - 1), true);
            return true;
        }
    }
    if (Screen::keyPressed(key, mods)) return true;
    if (key == kKeyEnter || key == kKeypadEnter) {  // the text boxes leave Enter alone
        create();
        return true;
    }
    return false;
}

void CreateWorldScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    if (pendingTab_ >= 0) {  // a tab was picked last frame: rebuild the screen on it before drawing
        tab_ = pendingTab_;
        pendingTab_ = -1;
        focusName_ = tab_ == kGameTab;
        resize(width_, height_);
    }
    // The header strip shows the plain panorama; the 25% tint starts under it.
    ui().drawBackdropFrom(graphics, width_, height_, time.deltaTicks, kCreateWorldTabBarHeight);
    graphics.fill(0, layout_.footerSeparatorY, width_, layout_.footerSeparatorY + 1, kSeparatorDark);
    graphics.fill(0, layout_.footerSeparatorY + 1, width_, layout_.footerSeparatorY + 2, kSeparatorLight);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
