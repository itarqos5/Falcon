#include "falcon/game/select_world_screen.hpp"

#include "falcon/game/create_world_screen.hpp"
#include "falcon/ui/confirm_screen.hpp"
#include "falcon/game/menu_background.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/level_info.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

namespace falcon {
namespace {

constexpr int kRowWidth = 270;
constexpr int kRowHeight = 36;
constexpr int kTextMaxWidth = 231;  // 270 - (2 + 32 + 3) - 2: fixed for every entry and window size
constexpr std::uint32_t kGrey = 0xFF808080u;
constexpr auto kDoubleClick = std::chrono::milliseconds(250);

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The longest start of `text` that is at most `maxWidth` wide (whole characters only).
std::string cutToWidth(const GuiGraphics& g, std::string text, int maxWidth) {
    while (!text.empty() && g.textWidth(text) > maxWidth) {
        std::size_t n = text.size();
        do {
            --n;
        } while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80);
        text.resize(n);
    }
    return text;
}

// The text, or its start followed by "..." if it is too wide (the game's CLAMPED overflow).
std::string clipText(const GuiGraphics& g, const std::string& text, int maxWidth) {
    if (g.textWidth(text) <= maxWidth) return text;
    return cutToWidth(g, text, maxWidth - g.textWidth("...")) + "...";
}

// The same for a line made of differently coloured pieces.
std::vector<InfoSegment> clipSegments(const GuiGraphics& g, const std::vector<InfoSegment>& segments, int maxWidth) {
    int total = 0;
    for (const InfoSegment& s : segments) total += g.textWidth(s.text);
    if (total <= maxWidth) return segments;

    std::vector<InfoSegment> out;
    int budget = maxWidth - g.textWidth("...");
    for (const InfoSegment& s : segments) {
        const int w = g.textWidth(s.text);
        if (w <= budget) {
            out.push_back(s);
            budget -= w;
            continue;
        }
        InfoSegment cut = s;
        cut.text = cutToWidth(g, s.text, budget);
        out.push_back(cut);
        break;
    }
    out.push_back({"...", 0, false});
    return out;
}

// One world in the list: icon, name, "folder (date)" and the info line.
class WorldEntry : public ScrollList::Entry {
public:
    WorldEntry(std::shared_ptr<const LevelSummary> summary, const Language& language,
               std::function<void(const std::string& id)> join)
        : Entry(kRowHeight),
          summary_(std::move(summary)),
          segments_(levelInfoSegments(*summary_, language)),
          idLine_(levelIdLine(*summary_)),
          join_(std::move(join)) {
        if (summary_->icon) icon_.emplace(*summary_->icon, FilterMode::Nearest);
    }

    void render(GuiGraphics& g, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState& state) override {
        const int contentX = r.x + 2;
        const int contentY = r.y + 2;

        if (icon_) {
            g.blit(*icon_, contentX, contentY, 0.0f, 0.0f, 32, 32, 64, 64, 64, 64);
        } else {
            const Texture2D& unknown = g.textures().get("misc/unknown_server.png");
            g.blit(unknown, contentX, contentY, 0.0f, 0.0f, 32, 32, unknown.width(), unknown.height(),
                   unknown.width(), unknown.height());
        }

        const int textX = r.x + 37;
        g.text(clipText(g, summary_->displayName(), kTextMaxWidth), static_cast<float>(textX), static_cast<float>(r.y + 3), kWhite);
        g.text(clipText(g, idLine_, kTextMaxWidth), static_cast<float>(textX), static_cast<float>(r.y + 14), kGrey);
        float x = static_cast<float>(textX);
        for (const InfoSegment& s : clipSegments(g, segments_, kTextMaxWidth)) {
            g.text(s.text, x, static_cast<float>(r.y + 23), s.color != 0 ? s.color : kGrey);
            x += static_cast<float>(g.textWidth(s.text));
        }

        if (state.hovered) drawOverlay(g, contentX, contentY, mouseX, mouseY);
    }

    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int /*button*/) override {
        if (!summary_->primaryActionActive()) return {};  // a disabled world can be selected, never opened
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClick = lastClick_ && now - *lastClick_ < kDoubleClick;
        lastClick_ = now;
        const bool onIcon = px >= r.x + 2 && px < r.x + 34 && py >= r.y + 2 && py < r.y + 34;
        if (!doubleClick && !onIcon) return {};
        lastClick_.reset();
        join_(summary_->id);
        return {true, true};
    }

    [[nodiscard]] const LevelSummary& summary() const { return *summary_; }

private:
    // The join arrow (or a warning badge) over the icon while the row is hovered.
    void drawOverlay(GuiGraphics& g, int cx, int cy, int mouseX, int mouseY) const {
        g.fill(cx, cy, cx + 32, cy + 32, 0xA0909090u);
        const bool overIcon = mouseX >= cx && mouseX < cx + 32 && mouseY >= cy && mouseY < cy + 32;
        const auto sprite = [&](const char* name) {
            g.blitSprite(std::string("world_list/") + name + (overIcon ? "_highlighted" : ""), cx, cy, 32, 32);
        };
        const LevelSummary& s = *summary_;
        if (s.kind != WorldKind::Normal) {
            sprite("error");
            sprite("marked_join");
        } else if (s.locked || s.requiresManualConversion || !s.compatible) {
            sprite("error");
        } else if (s.shouldBackup()) {
            sprite("marked_join");
            if (s.backup == BackupStatus::Downgrade) sprite("error");
            if (overIcon) g.requestCursor(CursorType::PointingHand);
        } else {
            sprite("join");
            if (overIcon) g.requestCursor(CursorType::PointingHand);
        }
    }

    std::shared_ptr<const LevelSummary> summary_;
    std::vector<InfoSegment> segments_;
    std::string idLine_;
    std::function<void(const std::string&)> join_;
    std::optional<Texture2D> icon_;
    std::optional<std::chrono::steady_clock::time_point> lastClick_;
};

// The only row while the saves folder is being read.
class LoadingEntry : public ScrollList::Entry {
public:
    LoadingEntry(std::string label, int screenWidth) : Entry(kRowHeight), label_(std::move(label)), screenWidth_(screenWidth) {}

    void render(GuiGraphics& g, const Rect& r, int, int, const ScrollList::RowState&) override {
        static constexpr const char* kFrames[] = {"O o o", "o O o", "o o O", "o O o"};
        const int y = r.y + 13;
        g.text(label_, static_cast<float>((screenWidth_ - g.textWidth(label_)) / 2), static_cast<float>(y), kWhite);
        const std::string dots = kFrames[(static_cast<long long>(g.timeMillis()) / 300) % 4];
        g.text(dots, static_cast<float>((screenWidth_ - g.textWidth(dots)) / 2), static_cast<float>(y + 9), kGrey);
    }

private:
    std::string label_;
    int screenWidth_;
};

}  // namespace

SelectWorldLayout computeSelectWorldLayout(int width, int height, int titleWidth) {
    constexpr int kHeader = 49;
    constexpr int kFooter = 60;
    constexpr int kSearchWidth = 200;
    SelectWorldLayout l;
    const int hx = (width - kSearchWidth) / 2;
    l.title = {hx + (kSearchWidth - titleWidth) / 2, 8, titleWidth, 9};
    l.search = {hx, 21, kSearchWidth, 20};
    l.list = {0, kHeader, width, height - kHeader - kFooter};
    const int gx = (width - 308) / 2;
    const int gy = height - kFooter + (kFooter - 44) / 2;  // H - 52
    l.play = {gx, gy, 150, 20};
    l.create = {gx + 158, gy, 150, 20};
    l.edit = {gx, gy + 24, 71, 20};
    l.remove = {gx + 79, gy + 24, 71, 20};
    l.recreate = {gx + 158, gy + 24, 71, 20};
    l.back = {gx + 237, gy + 24, 71, 20};
    return l;
}

bool worldMatchesFilter(const LevelSummary& s, const std::string& filter) {
    if (filter.empty()) return true;
    const std::string f = toLower(filter);
    return toLower(s.displayName()).find(f) != std::string::npos || toLower(s.id).find(f) != std::string::npos;
}

SelectWorldScreen::SelectWorldScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator), services_(services) {
    startScan();
}

SelectWorldScreen::~SelectWorldScreen() {
    if (scan_.valid()) scan_.wait();  // never leave a scan running against a destroyed screen
}

void SelectWorldScreen::startScan() {
    const std::filesystem::path dir = savesDir();
    scan_ = std::async(std::launch::async, [dir]() -> ScanResult { return scanWorlds(dir); });
}

void SelectWorldScreen::pollScan() {
    if (!scan_.valid() || scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    ScanResult result = scan_.get();
    if (!result) {
        std::printf("[falcon] could not read the saves folder %s\n", savesDir().string().c_str());
        loaded_ = true;  // show an empty list rather than loading forever
        levels_.clear();
    } else {
        levels_.clear();
        for (LevelSummary& summary : *result) levels_.push_back(std::make_shared<const LevelSummary>(std::move(summary)));
        loaded_ = true;
        if (levels_.empty()) {
            // No worlds at all: like the game, go straight to Create New World. Cancelling it leaves for the
            // title screen (this screen is replaced, not kept underneath); making a world plays it, with this list
            // underneath for when the player leaves it.
            navigator_.pop();
            navigator_.push(std::make_unique<CreateWorldScreen>(
                navigator_, services_, [&navigator = navigator_, &services = services_](const std::string& folder) {
                    navigator.push(std::make_unique<SelectWorldScreen>(navigator, services));
                    if (services.startWorld) services.startWorld(folder);
                }));
            return;
        }
    }
    fillEntries();
    selectAfterScan_.clear();  // done: this scan had the chance to find the new world
}

void SelectWorldScreen::openCreateWorld() {
    navigator_.push(std::make_unique<CreateWorldScreen>(navigator_, services_, [this](const std::string& folder) {
        selectAfterScan_ = folder;
        startScan();
        if (services_.startWorld) services_.startWorld(folder);  // (the game plays a world as soon as it is made; a world Falcon made is always one it can open)
    }));
}

const LevelSummary* SelectWorldScreen::selectedSummary() const {
    if (list_ == nullptr) return nullptr;
    const int index = list_->selectedIndex();
    return index >= 0 && index < static_cast<int>(shown_.size()) ? shown_[static_cast<std::size_t>(index)].get() : nullptr;
}

void SelectWorldScreen::fillEntries() {
    if (list_ == nullptr) return;
    const LevelSummary* previous = selectedSummary();
    const std::string keepId = previous != nullptr ? previous->id : std::string();

    list_->clearEntries();
    shown_.clear();
    if (!loaded_) {
        list_->addEntry<LoadingEntry>(services_.language.get("selectWorld.loading_list"), width_);
    } else {
        for (const auto& summary : levels_) {
            if (!worldMatchesFilter(*summary, filter_)) continue;
            shown_.push_back(summary);
            list_->addEntry<WorldEntry>(summary, services_.language, [this](const std::string& id) {
                for (std::size_t i = 0; i < shown_.size(); ++i) {
                    if (shown_[i]->id == id) list_->select(static_cast<int>(i));
                }
                joinSelected();
            });
        }
        // The same world stays selected across a refresh or a filter change.
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            if (!keepId.empty() && shown_[i]->id == keepId) list_->setSelectedIndex(static_cast<int>(i));
        }
        // A world that was just created is selected as soon as a scan has found it (pollScan() forgets
        // it after that scan; a rebuild of the screen before then must not lose it).
        if (!selectAfterScan_.empty()) {
            for (std::size_t i = 0; i < shown_.size(); ++i) {
                if (shown_[i]->id == selectAfterScan_) list_->setSelectedIndex(static_cast<int>(i));
            }
        }
    }
    updateButtons();
}

void SelectWorldScreen::updateButtons() {
    if (play_ == nullptr) return;
    const LevelSummary* s = selectedSummary();
    if (s == nullptr) {
        play_->setMessage(services_.language.get("selectWorld.select"));
        play_->active = edit_->active = delete_->active = recreate_->active = false;
        return;
    }
    play_->setMessage(services_.language.get(s->primaryActionKey()));
    play_->active = s->primaryActionActive();
    edit_->active = s->canEdit();
    recreate_->active = s->canRecreate();
    delete_->active = s->canDelete();
}

void SelectWorldScreen::notImplemented(const char* what) const {
    std::printf("[falcon] '%s' pressed (not implemented yet)\n", what);
    std::fflush(stdout);
}

void SelectWorldScreen::joinSelected() {
    const LevelSummary* s = selectedSummary();
    if (s == nullptr || !s->primaryActionActive()) return;
    if (!services_.startWorld) return;
    const std::string folder = s->id;
    const auto info = readLevelInfo(savesDir() / pathFromUtf8(folder));
    if (!info) {
        std::printf("[falcon] cannot open world '%s': level.dat cannot be read\n", folder.c_str());
        return;
    }
    if (info->madeByFalcon) {
        services_.startWorld(folder);
        return;
    }
    // A world made by Minecraft keeps its blocks in region files, which Falcon cannot read yet. Say so, and offer
    // a generated world with the same seed instead (the folder itself is never touched).
    navigator_.push(std::make_unique<ConfirmScreen>(
        navigator_, "This world was made by Minecraft",
        "Falcon cannot read its blocks yet. It can open a generated world with the same seed instead; "
        "nothing in the world's folder is changed, and changes you make are not saved.",
        "Play generated world", services_.language.get("gui.cancel"), [this, folder](bool yes) {
            if (yes) services_.startWorld(folder);
        }));
}

void SelectWorldScreen::deleteSelected() {
    const LevelSummary* s = selectedSummary();
    if (s == nullptr || !s->canDelete()) return;
    const std::string id = s->id;
    const Language& lang = services_.language;
    navigator_.push(std::make_unique<ConfirmScreen>(
        navigator_, lang.get("selectWorld.deleteQuestion"),
        formatLanguageString(lang.get("selectWorld.deleteWarning"), {s->displayName()}),
        lang.get("selectWorld.deleteButton"), lang.get("gui.cancel"), [this, id](bool yes) {
            if (yes && !deleteWorld(savesDir(), id)) std::printf("[falcon] could not delete world '%s'\n", id.c_str());
            startScan();  // the old rows stay until the new scan arrives, like the game
        }));
}

void SelectWorldScreen::init() {
    const std::string title = text("selectWorld.title");
    const SelectWorldLayout l = computeSelectWorldLayout(width_, height_, font().width(title));

    addLabel(title, l.title.x, l.title.y);
    auto& search = addEditBox(l.search, text("gui.selectWorld.search"));
    search.setText(filter_);
    search.onChange = [this](const std::string& text) {
        filter_ = text;
        fillEntries();
    };

    list_ = &addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, kRowHeight, kRowWidth,
                                   ScrollList::ScrollbarPlacement::RightOfRow,
                                   /*shrinkRowsWhenScrollable=*/false, /*selectable=*/true);
    list_->onSelectionChanged = [this](int) { updateButtons(); };
    list_->onActivate = [this](int) { joinSelected(); };

    play_ = &addButton(text("selectWorld.select"), l.play, [this] { joinSelected(); });
    addButton(text("selectWorld.create"), l.create, [this] { openCreateWorld(); });
    edit_ = &addButton(text("selectWorld.edit"), l.edit, [this] { notImplemented("edit world"); });
    delete_ = &addButton(text("selectWorld.delete"), l.remove, [this] { deleteSelected(); });
    recreate_ = &addButton(text("selectWorld.recreate"), l.recreate, [this] { notImplemented("re-create world"); });
    addButton(text("gui.back"), l.back, [this] { onClose(); });

    fillEntries();
    setFocus(&search);  // the search box has the initial focus
}

void SelectWorldScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    pollScan();
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
