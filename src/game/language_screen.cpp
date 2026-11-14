#include "falcon/game/language_screen.hpp"

#include "falcon/game/menu_background.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace falcon {
namespace {

constexpr int kRowHeight = 18;
constexpr int kRowWidth = 270;
constexpr int kHeader = 36;
constexpr int kFooter = 53;
constexpr std::uint32_t kWarningColor = 0xFFBABABAu;
constexpr auto kDoubleClick = std::chrono::milliseconds(250);

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// One language row: its "Name (Region)" centred in the row.
class LanguageEntry : public ScrollList::Entry {
public:
    LanguageEntry(const LanguageInfo& info, int screenWidth, std::function<void(const std::string& code)> choose)
        : Entry(kRowHeight), code_(info.code), text_(info.display()), screenWidth_(screenWidth), choose_(std::move(choose)) {}

    void render(GuiGraphics& g, const Rect& r, int, int, const ScrollList::RowState&) override {
        g.text(text_, static_cast<float>(screenWidth_ / 2 - g.textWidth(text_) / 2), static_cast<float>(r.y + 5), kWhite);
    }

    ScrollList::ClickOutcome mouseClicked(const Rect&, double, double, int) override {
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClick = lastClick_ && now - *lastClick_ < kDoubleClick;
        lastClick_ = now;
        if (!doubleClick) return {};  // a single click only selects
        lastClick_.reset();
        choose_(code_);
        return {true, true};
    }

    [[nodiscard]] const std::string& code() const { return code_; }

private:
    std::string code_;
    std::string text_;
    int screenWidth_;
    std::function<void(const std::string&)> choose_;
    std::optional<std::chrono::steady_clock::time_point> lastClick_;
};

}  // namespace

LanguageScreenLayout computeLanguageScreenLayout(int width, int height, int titleWidth, int warningWidth) {
    LanguageScreenLayout l;
    const int column = (width - 200) / 2;
    l.title = {column + (200 - titleWidth) / 2, 4, titleWidth, 9};
    l.search = {column, 17, 200, 15};
    l.list = {0, kHeader, width, height - kHeader - kFooter};
    const int buttons = (width - 308) / 2;
    l.fontSettings = {buttons, height - 28, 150, 20};
    l.done = {buttons + 158, height - 28, 150, 20};
    l.warningX = buttons + (308 - warningWidth) / 2;
    l.warningY = height - 45;
    return l;
}

bool languageMatchesFilter(const LanguageInfo& language, const std::string& filter) {
    if (filter.empty()) return true;
    const std::string f = lowered(filter);
    return lowered(language.name).find(f) != std::string::npos || lowered(language.region).find(f) != std::string::npos;
}

LanguageSelectScreen::LanguageSelectScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator), services_(services), selectedCode_(services.options.language) {}

const LanguageInfo* LanguageSelectScreen::selected() const {
    for (const LanguageInfo* info : shown_) {
        if (info->code == selectedCode_) return info;
    }
    return nullptr;
}

void LanguageSelectScreen::fillEntries(bool centreOnSelection) {
    if (list_ == nullptr) return;
    list_->clearEntries();
    shown_.clear();
    for (const LanguageInfo& info : services_.languages) {
        if (!languageMatchesFilter(info, filter_)) continue;
        shown_.push_back(&info);
        list_->addEntry<LanguageEntry>(info, width_, [this](const std::string& code) {
            selectedCode_ = code;  // a double click selects the row, then chooses it
            for (std::size_t i = 0; i < shown_.size(); ++i) {
                if (shown_[i]->code == code) list_->setSelectedIndex(static_cast<int>(i));
            }
            done();
        });
    }
    int index = -1;
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        if (shown_[i]->code == selectedCode_) index = static_cast<int>(i);
    }
    list_->setSelectedIndex(index);
    if (centreOnSelection && index >= 0) {
        list_->setScrollAmount(static_cast<double>(index * kRowHeight + kRowHeight / 2 - list_->height / 2));
    }
}

void LanguageSelectScreen::done() {
    if (const LanguageInfo* info = selected(); info != nullptr && info->code != services_.options.language) {
        services_.options.language = info->code;
        if (services_.packsChanged) services_.packsChanged();  // reloads the translations in the background
    }
    navigator_.pop();
}

void LanguageSelectScreen::init() {
    const std::string title = text("options.language.title");
    const std::string warning = formatLanguageString(text("options.languageAccuracyWarning"), {});  // "%%" -> "%"
    const LanguageScreenLayout l =
        computeLanguageScreenLayout(width_, height_, font().width(title), font().width(warning));

    addLabel(title, l.title.x, l.title.y);
    auto& search = addEditBox(l.search, text("gui.language.search"));
    search.setText(filter_);
    search.onChange = [this](const std::string& text) {
        filter_ = text;
        selectedCode_.clear();  // refiltering drops the selection, like the game
        fillEntries(false);
    };

    list_ = &addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, kRowHeight, kRowWidth,
                                   ScrollList::ScrollbarPlacement::RightOfRow,
                                   /*shrinkRowsWhenScrollable=*/false, /*selectable=*/true);
    list_->onSelectionChanged = [this](int index) {
        selectedCode_ = index >= 0 && index < static_cast<int>(shown_.size()) ? shown_[static_cast<std::size_t>(index)]->code
                                                                              : std::string();
    };
    list_->onActivate = [this](int) { done(); };

    addLabel(warning, l.warningX, l.warningY, kWarningColor & 0xFFFFFFu);
    addButton(text("options.font"), l.fontSettings, openScreen<FontSettingsScreen>(navigator_, services_));
    addButton(text("gui.done"), l.done, [this] { done(); });

    fillEntries(/*centreOnSelection=*/true);
    setFocus(&search);  // the search box has the initial focus
}

void LanguageSelectScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

void FontSettingsScreen::addOptions() {
    addPair(optionToggle("options.forceUnicodeFont", &GameOptions::forceUnicodeFont),
            optionToggle("options.japaneseGlyphVariants", &GameOptions::japaneseGlyphVariants,
                         "options.japaneseGlyphVariants.tooltip"));
}

}  // namespace falcon
