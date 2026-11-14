#include "falcon/game/pack_selection_screen.hpp"

#include "falcon/platform/dialog.hpp"
#include "falcon/text/text_layout.hpp"
#include "falcon/game/menu_background.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace falcon {
namespace {

constexpr int kListWidth = 200;
constexpr int kHeaderHeight = 49;
constexpr int kFooterHeight = 33;
constexpr int kIconSize = 32;
constexpr int kPackRowHeight = 36;
constexpr int kHeadingHeight = 13;  // (int)(9 * 1.5)
constexpr int kButtonWidth = 150;
constexpr int kButtonHeight = 20;

int javaRound(double v) { return static_cast<int>(std::floor(v + 0.5)); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "Available" / "Selected": centred, bold and underlined.
class HeadingEntry : public ScrollList::Entry {
public:
    explicit HeadingEntry(std::string title) : Entry(kHeadingHeight), title_(std::move(title)) {}

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        const int w = graphics.textWidth(title_, /*bold=*/true);
        graphics.text(title_, static_cast<float>(r.x + r.width / 2 - w / 2),
                      static_cast<float>(r.y + kHeadingHeight / 2 - Font::kLineHeight / 2), 0xFFFFFFFFu,
                      /*shadow=*/true, /*underline=*/true, /*bold=*/true);
    }

private:
    std::string title_;
};

// One pack: 32px icon, title, two description lines, and the transfer arrows on hover.
class PackEntry : public ScrollList::Entry {
public:
    PackEntry(PackSelectionScreen& screen, const PackInfo& pack)
        : Entry(kPackRowHeight), screen_(screen), pack_(pack) {}

    void render(GuiGraphics& graphics, const Rect& r, int mouseX, int mouseY,
                const ScrollList::RowState& state) override {
        const Language& lang = screen_.services().language;
        const Font& font = screen_.services().font;
        const PackSelectionModel& model = screen_.model();

        const int cx = r.x + 2;
        const int cy = r.y + 2;
        const bool incompatible = pack_.compatibility != PackCompatibility::Compatible;
        if (incompatible) {
            graphics.fill(cx - 1, cy - 1, cx + (r.width - 4) + 1, cy + kIconSize + 1, 0xFF770000u);
        }

        graphics.blit(screen_.iconTexture(pack_, graphics), cx, cy, 0.0f, 0.0f, kIconSize, kIconSize,
                      kIconSize, kIconSize);

        std::string title = pack_.title;
        std::string description = pack_.description;
        const bool overlay = state.hovered || (state.selected && state.listFocused);
        if (overlay) {
            graphics.fill(cx, cy, cx + kIconSize, cy + kIconSize, 0xA0909090u);
            if (incompatible) {
                title = lang.get("pack.incompatible");
                switch (pack_.compatibility) {
                    case PackCompatibility::TooOld: description = lang.get("pack.incompatible.old"); break;
                    case PackCompatibility::TooNew: description = lang.get("pack.incompatible.new"); break;
                    default: description = lang.get("pack.incompatible.unknown"); break;
                }
            }
            const int lx = mouseX - cx;
            const int ly = mouseY - cy;
            const bool onIcon = lx >= 0 && lx < kIconSize && ly >= 0 && ly < kIconSize;
            if (model.canSelect(pack_.id)) {
                graphics.blitSprite(onIcon ? "transferable_list/select_highlighted" : "transferable_list/select",
                                    cx, cy, kIconSize, kIconSize);
                if (onIcon) graphics.requestCursor(CursorType::PointingHand);
            } else {
                if (model.canUnselect(pack_.id)) {
                    const bool hot = onIcon && lx < 16;
                    graphics.blitSprite(hot ? "transferable_list/unselect_highlighted" : "transferable_list/unselect",
                                        cx, cy, kIconSize, kIconSize);
                    if (hot) graphics.requestCursor(CursorType::PointingHand);
                }
                if (model.canMoveUp(pack_.id)) {
                    const bool hot = onIcon && lx >= 16 && ly < 16;
                    graphics.blitSprite(hot ? "transferable_list/move_up_highlighted" : "transferable_list/move_up",
                                        cx, cy, kIconSize, kIconSize);
                    if (hot) graphics.requestCursor(CursorType::PointingHand);
                }
                if (model.canMoveDown(pack_.id)) {
                    const bool hot = onIcon && lx >= 16 && ly >= 16;
                    graphics.blitSprite(hot ? "transferable_list/move_down_highlighted" : "transferable_list/move_down",
                                        cx, cy, kIconSize, kIconSize);
                    if (hot) graphics.requestCursor(CursorType::PointingHand);
                }
            }
        }

        // Text to the right of the icon; rows are 6px narrower while the list scrolls.
        const int textX = cx + kIconSize + 2;
        const int maxWidth = r.width - 39;
        graphics.text(ellipsize(font, title, maxWidth), static_cast<float>(textX), static_cast<float>(cy + 1),
                      0xFFFFFFFFu);

        std::vector<std::string> lines = wrapText(font, description, maxWidth);
        if (lines.size() > 2) {  // at most two rows; the second is clipped with "..."
            lines[1] = ellipsize(font, lines[1] + " " + lines[2], maxWidth);
            lines.resize(2);
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            graphics.text(lines[i], static_cast<float>(textX),
                          static_cast<float>(cy + 12 + static_cast<int>(i) * Font::kLineHeight),
                          pack_.descriptionColor);
        }
    }

    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override {
        if (button != 0) return {};
        const PackSelectionModel& model = screen_.model();
        const double lx = px - (r.x + 2);
        const double ly = py - (r.y + 2);
        const bool onIcon = lx >= 0 && lx < kIconSize && ly >= 0 && ly < kIconSize;
        if (!onIcon) return {};  // anywhere else just selects the row

        using Action = PackSelectionScreen::Action;
        if (model.canSelect(pack_.id)) {
            screen_.request(Action::Select, pack_.id);
        } else if (model.canUnselect(pack_.id) && lx < 16) {
            screen_.request(Action::Unselect, pack_.id);
        } else if (model.canMoveUp(pack_.id) && lx >= 16 && ly < 16) {
            screen_.request(Action::MoveUp, pack_.id);
        } else if (model.canMoveDown(pack_.id) && lx >= 16 && ly >= 16) {
            screen_.request(Action::MoveDown, pack_.id);
        } else {
            return {};
        }
        return {true, /*clickSound=*/false};  // the pack arrows are silent
    }

private:
    PackSelectionScreen& screen_;
    const PackInfo& pack_;
};

}  // namespace

PackSelectionScreen::PackSelectionScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator),
      services_(services),
      model_(std::make_unique<PackSelectionModel>(services.packs, services.options.resourcePacks)) {}

const Texture2D& PackSelectionScreen::iconTexture(const PackInfo& pack, GuiGraphics& graphics) {
    if (pack.icon && !pack.icon->empty()) {
        auto it = icons_.find(pack.id);
        if (it == icons_.end()) it = icons_.emplace(pack.id, Texture2D(*pack.icon, FilterMode::Nearest)).first;
        return it->second;
    }
    return graphics.textures().get("misc/unknown_pack.png");
}

bool PackSelectionScreen::matchesFilter(const PackInfo& pack) const {
    if (filter_.empty()) return true;
    return lower(pack.id).find(filter_) != std::string::npos ||
           lower(pack.title).find(filter_) != std::string::npos ||
           lower(pack.description).find(filter_) != std::string::npos;
}

void PackSelectionScreen::request(Action action, const std::string& packId) {
    pending_.push_back({action, packId});
}

void PackSelectionScreen::applyPending() {
    for (const Pending& p : pending_) {
        switch (p.action) {
            case Action::Select: model_->select(p.id); break;
            case Action::Unselect: model_->unselect(p.id); break;
            case Action::MoveUp: model_->moveUp(p.id); break;
            case Action::MoveDown: model_->moveDown(p.id); break;
        }
        focusId_ = p.id;
    }
    if (!pending_.empty()) needsPopulate_ = true;
    pending_.clear();
}

void PackSelectionScreen::populate() {
    if (availableList_ == nullptr || selectedList_ == nullptr) return;
    const Language& lang = services_.language;

    int focusIndexAvailable = -1;
    int focusIndexSelected = -1;
    const auto fill = [&](ScrollList& list, const std::vector<std::string>& ids, const char* titleKey,
                          int& focusIndex) {
        list.clearEntries();
        list.addEntry<HeadingEntry>(lang.get(titleKey));
        for (const std::string& id : ids) {
            const PackInfo* pack = services_.packs.find(id);
            if (pack == nullptr || !matchesFilter(*pack)) continue;
            if (id == focusId_) focusIndex = static_cast<int>(list.entryCount());
            list.addEntry<PackEntry>(*this, *pack);
        }
    };
    fill(*availableList_, model_->available(), "pack.available.title", focusIndexAvailable);
    fill(*selectedList_, model_->selected(), "pack.selected.title", focusIndexSelected);

    // The pack that was just moved keeps the (focused) selection frame, wherever it landed.
    if (focusIndexSelected >= 0) {
        selectedList_->setSelectedIndex(focusIndexSelected);
        setFocus(selectedList_);
    } else if (focusIndexAvailable >= 0) {
        availableList_->setSelectedIndex(focusIndexAvailable);
        setFocus(availableList_);
    }
    focusId_.clear();
    needsPopulate_ = false;
}

void PackSelectionScreen::init() {
    // Header: title, drag-and-drop hint, search box - a centred column 4px apart.
    const std::string title = text("resourcePack.title");
    const std::string hint = text("pack.dropInfo");
    const int blockW = std::max({font().width(title), font().width(hint), kListWidth});
    const int blockY = javaRound((kHeaderHeight - (9 + 4 + 9 + 4 + 15)) / 2.0);

    addCenteredLabel(title, width_ / 2, blockY);
    addCenteredLabel(hint, width_ / 2, blockY + 9 + 4, 0xAAAAAAu);
    auto& search = addEditBox({layout::centeredX(width_, blockW) + (blockW - kListWidth) / 2, blockY + 9 + 4 + 9 + 4,
                               kListWidth, 15},
                              text("gui.packSelection.search"));
    search.setText(filter_);
    search.onChange = [this](const std::string& text) {
        std::string lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        filter_ = lowered;
        needsPopulate_ = true;
    };

    // Lists: 200 wide, 30px apart, centred - so on a 427px screen they overhang both edges.
    const int listHeight = height_ - kHeaderHeight - kFooterHeight;
    availableList_ = &addWidget<ScrollList>(width_ / 2 - 15 - kListWidth, kHeaderHeight, kListWidth,
                                            listHeight, kPackRowHeight, /*rowWidth=*/0,
                                            ScrollList::ScrollbarPlacement::InsideRight,
                                            /*shrinkRowsWhenScrollable=*/true, /*selectable=*/true);
    selectedList_ = &addWidget<ScrollList>(width_ / 2 + 15, kHeaderHeight, kListWidth, listHeight,
                                           kPackRowHeight, 0, ScrollList::ScrollbarPlacement::InsideRight,
                                           true, true);

    // Footer: Open Pack Folder and Done, 8px apart, centred.
    const int footerY = height_ - kFooterHeight + javaRound((kFooterHeight - kButtonHeight) / 2.0);
    const auto footer = layout::centeredRow(width_, footerY, {kButtonWidth, kButtonWidth}, 8, kButtonHeight);
    addButton(text("pack.openFolder"), footer[0], [this] {
        const std::filesystem::path dir = services_.packs.packDirectory();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);  // the game creates it at startup
        openFolderInFileManager(dir.string());
    });
    addButton(text("gui.done"), footer[1], [this] { onClose(); });

    setFocus(&search);  // typing goes to the search box straight away
    needsPopulate_ = true;
}

bool PackSelectionScreen::mouseClicked(double x, double y, int button) {
    applyPending();
    if (needsPopulate_) populate();
    return Screen::mouseClicked(x, y, button);
}

void PackSelectionScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    applyPending();
    if (needsPopulate_) populate();
    Screen::render(graphics, mouseX, mouseY, time);
}

void PackSelectionScreen::onClose() {
    // Commit: the selected pack ids go into the options, then back to the previous screen.
    const std::vector<std::string> before = services_.options.resourcePacks;
    services_.options.resourcePacks = model_->commitIds();
    services_.options.highContrast = model_->isSelected("high_contrast");
    // Only an actual change loads anything; the loading itself runs in the background.
    if (services_.options.resourcePacks != before && services_.packsChanged) services_.packsChanged();
    navigator_.pop();
}

}  // namespace falcon
