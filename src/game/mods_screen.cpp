#include "falcon/game/mods_screen.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/platform/dialog.hpp"
#include "falcon/text/text_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace falcon {
namespace {

constexpr int kRowHeight = 36;   // ModListWidget: a mod in the list
constexpr int kIconSize = 32;
constexpr int kLine = 10;        // a line of the description: the font's 9 and one
constexpr int kBadgeHeight = 10;
constexpr std::uint32_t kModWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kModDescription = 0xFFAAAAAAu;  // the description and the header lines (DescriptionListWidget, ModsScreen)
constexpr std::uint32_t kModSummary = 0xFF808080u;      // the lines of a mod in the list (ModListEntry: -8355712)
constexpr std::uint32_t kModLink = 0xFF7FA6FFu;
constexpr std::uint32_t kModWarning = 0xFFFF5555u;

std::string joined(const std::vector<std::string>& items, const char* separator) {
    std::string out;
    for (const std::string& item : items) {
        if (item.empty()) continue;
        out += out.empty() ? item : separator + item;
    }
    return out;
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string sizeText(std::uint64_t bytes) {
    char buffer[32];
    if (bytes >= 1024ull * 1024ull) std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull) std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

// A word-wrapped paragraph, each line cut to the width if one word is longer than it (a web address).
std::vector<std::string> wrapped(const Font& font, const std::string& text, int width) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= text.size()) {  // (a line break in the text starts a new paragraph)
        const std::size_t end = text.find('\n', at);
        const std::string paragraph = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (paragraph.empty()) lines.emplace_back();
        for (const std::string& line : wrapText(font, paragraph, width)) lines.push_back(ellipsize(font, line, width));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return lines;
}

// A badge: a box in two colours round a word. Returns how wide it is.
int badgeWidth(const Font& font, const ModsScreen::Badge& badge) { return font.width(badge.text) + 6; }

int drawBadge(GuiGraphics& graphics, const Font& font, int x, int y, const ModsScreen::Badge& badge) {
    const int width = badgeWidth(font, badge);
    graphics.fill(x, y, x + width, y + kBadgeHeight, badge.outline);
    graphics.fill(x + 1, y + 1, x + width - 1, y + kBadgeHeight - 1, badge.fill);
    graphics.text(badge.text, static_cast<float>(x + 3), static_cast<float>(y + 1), kModWhite, /*shadow=*/false);
    return width;
}

int badgesWidth(const Font& font, const std::vector<ModsScreen::Badge>& badges) {
    int total = 0;
    for (const ModsScreen::Badge& b : badges) total += badgeWidth(font, b) + 2;
    return total;
}

// One mod in the list: its icon, its name with the badges after it, and the first lines of its description.
class ModRow : public ScrollList::Entry {
public:
    ModRow(ModsScreen& screen, std::size_t index) : Entry(kRowHeight), screen_(screen), index_(index) {}

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        const ModsScreen::Listed& mod = screen_.mods()[index_];
        const Font& font = screen_.services().font;
        const int cx = r.x + 2, cy = r.y + 2;
        if (mod.verdict.blocked) graphics.fill(cx - 1, cy - 1, cx + kIconSize + 1, cy + kIconSize + 1, 0xFF770000u);
        graphics.blit(screen_.iconTexture(index_, graphics), cx, cy, 0.0f, 0.0f, kIconSize, kIconSize, kIconSize, kIconSize);
        const int textX = cx + kIconSize + 3, maxWidth = std::max(20, r.x + r.width - 4 - textX);
        const std::vector<ModsScreen::Badge> badges = ModsScreen::badgesOf(mod);
        const std::string name = ellipsize(font, mod.info.name, std::max(20, maxWidth - badgesWidth(font, badges)));
        graphics.text(name, static_cast<float>(textX), static_cast<float>(cy + 1), kModWhite);
        int badgeX = textX + font.width(name) + 4;
        for (const ModsScreen::Badge& b : badges) badgeX += drawBadge(graphics, font, badgeX, cy, b) + 2;
        // The description, two lines at most (the second cut short with "..." when there is more).
        std::vector<std::string> lines = wrapText(font, mod.info.description.substr(0, mod.info.description.find('\n')), maxWidth);
        if (lines.size() > 2) {
            lines[1] = ellipsize(font, lines[1] + " " + lines[2], maxWidth);
            lines.resize(2);
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            graphics.text(lines[i], static_cast<float>(textX), static_cast<float>(cy + 12 + static_cast<int>(i) * Font::kLineHeight), kModSummary);
        }
    }

private:
    ModsScreen& screen_;
    std::size_t index_;
};

// Lines of text in one colour (a paragraph already wrapped), or a gap.
class LinesEntry : public ScrollList::Entry {
public:
    LinesEntry(std::vector<std::string> lines, std::uint32_t colour) : Entry(std::max(4, static_cast<int>(lines.size()) * kLine)), lines_(std::move(lines)), colour_(colour) {}

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        for (std::size_t i = 0; i < lines_.size(); ++i) graphics.text(lines_[i], static_cast<float>(r.x + 2), static_cast<float>(r.y + 1 + static_cast<int>(i) * kLine), colour_);
    }

private:
    std::vector<std::string> lines_;
    std::uint32_t colour_;
};

// "Label: value", the value wrapped beside the label.
class FactEntry : public ScrollList::Entry {
public:
    FactEntry(const Font& font, std::string label, const std::string& value, int width)
        : Entry(1), label_(std::move(label) + ":"), labelWidth_(font.width(label_) + 4), lines_(wrapped(font, value, std::max(20, width - labelWidth_))) {
        height = std::max(1, static_cast<int>(lines_.size())) * kLine;
    }

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        graphics.text(label_, static_cast<float>(r.x + 2), static_cast<float>(r.y + 1), kModDescription);
        for (std::size_t i = 0; i < lines_.size(); ++i) graphics.text(lines_[i], static_cast<float>(r.x + 2 + labelWidth_), static_cast<float>(r.y + 1 + static_cast<int>(i) * kLine), kModWhite);
    }

private:
    std::string label_;
    int labelWidth_;
    std::vector<std::string> lines_;
};

// A link, in blue and underlined: clicking it opens the address in the browser (an http or https address only).
class LinkEntry : public ScrollList::Entry {
public:
    LinkEntry(const Font& font, std::string label, std::string url, int width) : Entry(kLine), label_(ellipsize(font, label, width)), url_(std::move(url)), labelWidth_(font.width(label_)) {}

    void render(GuiGraphics& graphics, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState&) override {
        const bool over = mouseX >= r.x + 2 && mouseX < r.x + 2 + labelWidth_ && mouseY >= r.y && mouseY < r.y + kLine;
        graphics.text(label_, static_cast<float>(r.x + 2), static_cast<float>(r.y + 1), over ? 0xFFB5CBFFu : kModLink, true, /*underline=*/true);
        if (over) graphics.requestCursor(CursorType::PointingHand);
    }
    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override {
        if (button != 0 || px < r.x + 2 || px >= r.x + 2 + labelWidth_ || py < r.y || py >= r.y + kLine) return {};
        openWebAddress(url_);
        return {true, true};
    }

private:
    std::string label_, url_;
    int labelWidth_;
};

}  // namespace

ModsLayout computeModsLayout(int width, int height) {
    ModsLayout l;
    l.paneWidth = width / 2 - 8;
    l.rightPaneX = width - l.paneWidth;
    l.titleX = l.paneWidth / 2;
    const int searchWidth = std::min(200, l.paneWidth - 16);
    l.search = {(l.paneWidth - searchWidth) / 2, 22, searchWidth, 20};
    l.list = {0, 48, l.paneWidth, std::max(20, height - 36 - 48)};
    l.headerX = l.rightPaneX;
    const int buttonWidth = std::min(200, l.paneWidth / 2 - 2);
    l.website = {l.rightPaneX + l.paneWidth / 4 - buttonWidth / 2, 84, buttonWidth, 20};
    l.issues = {l.rightPaneX + l.paneWidth * 3 / 4 - buttonWidth / 2, 84, buttonWidth, 20};
    l.descriptions = {l.rightPaneX, 108, l.paneWidth, std::max(20, height - 36 - 108)};
    l.folder = {width / 2 - 154, height - 28, 150, 20};
    l.done = {width / 2 + 4, height - 28, 150, 20};
    return l;
}

std::vector<ModsScreen::Badge> ModsScreen::badgesOf(const Listed& mod) {
    std::vector<Badge> out;
    if (mod.verdict.blocked) out.push_back({"Not compatible", 0xFF550E0Eu, 0xFF7C2B2Bu});
    if (mod.info.environment == "client") out.push_back({"Client", 0xFF0E2A55u, 0xFF2B4B7Cu});
    else if (mod.info.environment == "server") out.push_back({"Server", 0xFF0E552Au, 0xFF2B7C4Bu});
    return out;
}

std::vector<std::pair<std::string, std::string>> ModsScreen::details(const ModInfo& m) {
    std::vector<std::pair<std::string, std::string>> out;
    const auto add = [&](const char* label, const std::string& value) {
        if (!value.empty()) out.emplace_back(label, value);
    };
    add("Mod ID", m.id);
    add("Loader", m.loader);
    add("Environment", m.environment == "client" ? "Client only" : m.environment == "server" ? "Server only" : "Client and server");
    add("Minecraft", m.minecraft);
    add("Requires", joined(m.depends, ", "));
    add("File", m.file.filename().string() + " (" + sizeText(m.size) + ")");
    if (m.bundled > 0) add("Bundled", std::to_string(m.bundled) + (m.bundled == 1 ? " library" : " libraries"));
    return out;
}

bool ModsScreen::matches(const ModInfo& m, const std::string& text) {
    if (text.empty()) return true;
    for (const std::string& field : {m.name, m.id, m.description, joined(m.authors, " "), m.loader}) {
        if (lowered(field).find(text) != std::string::npos) return true;
    }
    return false;
}

ModsScreen::ModsScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {
    for (ModInfo& info : readModsFolder(services.modsDir)) {
        std::vector<std::string> needs;
        for (const std::string& dependency : info.depends) needs.push_back(dependency.substr(0, dependency.find(' ')));
        Listed listed;
        listed.verdict = judgeOptimization(info.id, info.name, {}, needs);
        listed.info = std::move(info);
        mods_.push_back(std::move(listed));
    }
    icons_.resize(mods_.size());
    if (!mods_.empty()) selected_ = 0;
}

const Texture2D& ModsScreen::iconTexture(std::size_t index, GuiGraphics& graphics) {
    if (index < mods_.size() && !mods_[index].info.icon.empty()) {
        if (!icons_[index]) {
            if (const auto image = decodePng(mods_[index].info.icon); image && !image->empty()) icons_[index] = std::make_unique<Texture2D>(*image, FilterMode::Linear);
            else mods_[index].info.icon.clear();  // (not a picture: the plain one from now on)
        }
        if (icons_[index]) return *icons_[index];
    }
    return graphics.textures().get("misc/unknown_pack.png");
}

void ModsScreen::showDetails() {
    if (descriptions_ == nullptr) return;
    descriptions_->clearEntries();
    const bool any = selected_ >= 0 && static_cast<std::size_t>(selected_) < mods_.size();
    if (website_ != nullptr) website_->visible = any && !mods_[static_cast<std::size_t>(selected_)].info.homepage.empty();
    if (issues_ != nullptr) issues_->visible = any && !mods_[static_cast<std::size_t>(selected_)].info.issues.empty();
    if (!any) return;
    const Listed& mod = mods_[static_cast<std::size_t>(selected_)];
    const int width = descriptions_->width - 14;  // (a row of the list, less the scrollbar)
    const auto gap = [&] { descriptions_->addEntry<LinesEntry>(std::vector<std::string>{}, kModWhite); };
    const auto heading = [&](const char* text) { descriptions_->addEntry<LinesEntry>(std::vector<std::string>{text}, kModWhite); };
    if (mod.verdict.blocked) {
        descriptions_->addEntry<LinesEntry>(wrapped(font(), mod.verdict.reason, width), kModWarning);
        gap();
    }
    if (!mod.info.description.empty()) {
        descriptions_->addEntry<LinesEntry>(wrapped(font(), mod.info.description, width), kModDescription);
        gap();
    }
    if (!mod.info.sources.empty()) {
        heading("Links:");
        descriptions_->addEntry<LinkEntry>(font(), "Sources", mod.info.sources, width);
        gap();
    }
    if (!mod.info.authors.empty() || !mod.info.contributors.empty()) {
        heading("Credits:");
        std::vector<std::string> people;
        for (const std::string& a : mod.info.authors) people.push_back(a);
        for (const std::string& c : mod.info.contributors) people.push_back(c + " (contributor)");
        std::vector<std::string> lines;
        for (const std::string& p : people) {
            for (const std::string& line : wrapped(font(), p, width - 8)) lines.push_back("  " + line);
        }
        descriptions_->addEntry<LinesEntry>(std::move(lines), kModDescription);
        gap();
    }
    if (!mod.info.license.empty()) {
        descriptions_->addEntry<FactEntry>(font(), "Licensed under", mod.info.license, width);
        gap();
    }
    heading("Details:");
    for (const auto& [label, value] : details(mod.info)) descriptions_->addEntry<FactEntry>(font(), label, value, width);
    gap();
    descriptions_->addEntry<LinesEntry>(wrapped(font(), "Mods are only listed for now: Falcon does not load them yet.", width), kModSummary);
}

void ModsScreen::populate() {
    if (list_ == nullptr) return;
    list_->clearEntries();
    shown_.clear();
    for (std::size_t i = 0; i < mods_.size(); ++i) {
        if (!matches(mods_[i].info, filter_)) continue;
        shown_.push_back(i);
        list_->addEntry<ModRow>(*this, i);
    }
    // The selected mod stays selected while it matches; else the first one that does.
    int row = -1;
    for (std::size_t k = 0; k < shown_.size(); ++k) {
        if (static_cast<int>(shown_[k]) == selected_) row = static_cast<int>(k);
    }
    if (row < 0) {
        row = shown_.empty() ? -1 : 0;
        selected_ = shown_.empty() ? -1 : static_cast<int>(shown_[0]);
    }
    list_->setSelectedIndex(row);
    if (row >= 0) list_->scrollToEntry(row);
    showDetails();
}

void ModsScreen::init() {
    layout_ = computeModsLayout(width_, height_);
    const ModsLayout& l = layout_;
    addCenteredLabel("Mods", l.titleX, l.titleY);
    addButton("Open Mods Folder", l.folder, [this] {
        std::error_code ignored;
        std::filesystem::create_directories(services_.modsDir, ignored);
        openFolderInFileManager(services_.modsDir.string());
    });
    addButton(text("gui.done"), l.done, [this] { onClose(); });

    list_ = nullptr;
    descriptions_ = nullptr;
    website_ = issues_ = nullptr;
    if (mods_.empty()) {
        addCenteredLabel("No mods found", width_ / 2, height_ / 2 - 14, 0xFFFFFFu);
        addCenteredLabel("Mods you install from the launcher are put in", width_ / 2, height_ / 2 - 2, 0xAAAAAAu);
        addCenteredLabel(services_.modsDir.string(), width_ / 2, height_ / 2 + 8, 0xAAAAAAu);
        return;
    }
    EditBox& search = addEditBox(l.search, "Search...", 64);
    search.setText(filter_);
    search.onChange = [this](const std::string& value) {
        filter_ = lowered(value);
        populate();
    };
    list_ = &addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, kRowHeight, /*rowWidth=*/0, ScrollList::ScrollbarPlacement::InsideRight, /*shrinkRowsWhenScrollable=*/true,
                                   /*selectable=*/true);
    descriptions_ = &addWidget<ScrollList>(l.descriptions.x, l.descriptions.y, l.descriptions.width, l.descriptions.height, kLine, 0, ScrollList::ScrollbarPlacement::InsideRight, true, /*selectable=*/false);
    website_ = &addButton("Website", l.website, [this] {
        if (selected_ >= 0) openWebAddress(mods_[static_cast<std::size_t>(selected_)].info.homepage);
    });
    issues_ = &addButton("Issues", l.issues, [this] {
        if (selected_ >= 0) openWebAddress(mods_[static_cast<std::size_t>(selected_)].info.issues);
    });
    list_->onSelectionChanged = [this](int row) {
        selected_ = row >= 0 && static_cast<std::size_t>(row) < shown_.size() ? static_cast<int>(shown_[static_cast<std::size_t>(row)]) : -1;
        showDetails();
    };
    populate();
    setFocus(&search);  // typing searches, as in Mod Menu
}

void ModsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
    if (mods_.empty()) return;
    const ModsLayout& l = layout_;
    if (shown_.empty()) {  // (the search matches nothing)
        const std::string none = "No mods match";
        graphics.text(none, static_cast<float>(l.list.x + (l.list.width - graphics.textWidth(none)) / 2), static_cast<float>(l.list.y + 12), kModDescription);
    }
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= mods_.size()) return;
    // The header of the right pane (ModsScreen.extractRenderState): the icon, the name and its badges, the version and who made it.
    const Listed& mod = mods_[static_cast<std::size_t>(selected_)];
    const Font& f = font();
    if (mod.verdict.blocked) graphics.fill(l.headerX - 1, l.headerY - 1, l.headerX + kIconSize + 1, l.headerY + kIconSize + 1, 0xFF770000u);
    graphics.blit(iconTexture(static_cast<std::size_t>(selected_), graphics), l.headerX, l.headerY, 0.0f, 0.0f, kIconSize, kIconSize, kIconSize, kIconSize);
    const int textX = l.headerX + 36, maxWidth = std::max(20, l.paneWidth - 36 - 4);
    const std::vector<Badge> badges = badgesOf(mod);
    const std::string name = ellipsize(f, mod.info.name, std::max(20, maxWidth - badgesWidth(f, badges)));
    graphics.text(name, static_cast<float>(textX), static_cast<float>(l.headerY + 1), kModWhite);
    int badgeX = textX + f.width(name) + 4;
    for (const Badge& b : badges) badgeX += drawBadge(graphics, f, badgeX, l.headerY, b) + 2;
    if (!mod.info.version.empty()) graphics.text(ellipsize(f, "Version: " + mod.info.version, maxWidth), static_cast<float>(textX), static_cast<float>(l.headerY + 12), kModDescription);
    const std::string by = joined(mod.info.authors, ", ");
    if (!by.empty()) graphics.text(ellipsize(f, "by " + by, maxWidth), static_cast<float>(textX), static_cast<float>(l.headerY + 23), kModDescription);
}

}  // namespace falcon
