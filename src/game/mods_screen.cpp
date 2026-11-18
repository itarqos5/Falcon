#include "falcon/game/mods_screen.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/platform/dialog.hpp"
#include "falcon/text/text_layout.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace falcon {
namespace {

constexpr int kHeaderHeight = 40;
constexpr int kFooterHeight = 33;
constexpr int kRowHeight = 36;
constexpr int kIconSize = 32;
constexpr int kButtonWidth = 150;
constexpr int kButtonHeight = 20;
constexpr int kLine = 10;  // a line of the panel on the right: the font's 9 and one
constexpr std::uint32_t kModWhite = 0xFFFFFFFFu, kModLight = 0xFFD0D0D0u, kModGrey = 0xFFA0A0A0u, kModRed = 0xFFFF5555u;

std::string joined(const std::vector<std::string>& items, const char* separator) {
    std::string out;
    for (const std::string& item : items) {
        if (item.empty()) continue;
        out += out.empty() ? item : separator + item;
    }
    return out;
}

std::string sizeText(std::uint64_t bytes) {
    char buffer[32];
    if (bytes >= 1024ull * 1024ull) std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull) std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

// "version - Loader", the second line of a mod.
std::string versionLine(const ModInfo& m) {
    std::string out = m.version;
    if (!m.loader.empty()) out += out.empty() ? m.loader : " - " + m.loader;
    return out;
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

// One mod in the list: its icon, its name, its version and loader, and who made it.
class ModRow : public ScrollList::Entry {
public:
    ModRow(ModsScreen& screen, std::size_t index) : Entry(kRowHeight), screen_(screen), index_(index) {}

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        const ModsScreen::Listed& mod = screen_.mods()[index_];
        const Font& font = screen_.services().font;
        const int cx = r.x + 2, cy = r.y + 2;
        if (mod.verdict.blocked) graphics.fill(cx - 1, cy - 1, cx + kIconSize + 1, cy + kIconSize + 1, 0xFF770000u);
        graphics.blit(screen_.iconTexture(index_, graphics), cx, cy, 0.0f, 0.0f, kIconSize, kIconSize, kIconSize, kIconSize);
        const int textX = cx + kIconSize + 2, maxWidth = r.width - 39;
        graphics.text(ellipsize(font, mod.info.name, maxWidth), static_cast<float>(textX), static_cast<float>(cy + 1), mod.verdict.blocked ? kModRed : kModWhite);
        graphics.text(ellipsize(font, mod.verdict.blocked ? std::string("Not compatible") : versionLine(mod.info), maxWidth), static_cast<float>(textX), static_cast<float>(cy + 12),
                      mod.verdict.blocked ? kModRed : kModGrey);
        const std::string by = joined(mod.info.authors, ", ");
        if (!by.empty()) graphics.text(ellipsize(font, "by " + by, maxWidth), static_cast<float>(textX), static_cast<float>(cy + 12 + Font::kLineHeight), kModGrey);
    }

private:
    ModsScreen& screen_;
    std::size_t index_;
};

// The top of the panel on the right: a big icon, the name, the version and loader, and who made it.
class HeaderEntry : public ScrollList::Entry {
public:
    HeaderEntry(ModsScreen& screen, std::size_t index) : Entry(54), screen_(screen), index_(index) {}

    void render(GuiGraphics& graphics, const Rect& r, int, int, const ScrollList::RowState&) override {
        const ModsScreen::Listed& mod = screen_.mods()[index_];
        const Font& font = screen_.services().font;
        const int cx = r.x + 2, cy = r.y + 2, textX = cx + 52, maxWidth = r.width - 56;
        graphics.blit(screen_.iconTexture(index_, graphics), cx, cy, 0.0f, 0.0f, 48, 48, 48, 48);
        graphics.text(ellipsize(font, mod.info.name, maxWidth), static_cast<float>(textX), static_cast<float>(cy + 4), kModWhite, true, false, /*bold=*/true);
        graphics.text(ellipsize(font, versionLine(mod.info), maxWidth), static_cast<float>(textX), static_cast<float>(cy + 17), kModGrey);
        const std::string by = joined(mod.info.authors, ", ");
        if (!by.empty()) graphics.text(ellipsize(font, "by " + by, maxWidth), static_cast<float>(textX), static_cast<float>(cy + 28), kModLight);
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
        graphics.text(label_, static_cast<float>(r.x + 2), static_cast<float>(r.y + 1), kModGrey);
        for (std::size_t i = 0; i < lines_.size(); ++i) graphics.text(lines_[i], static_cast<float>(r.x + 2 + labelWidth_), static_cast<float>(r.y + 1 + static_cast<int>(i) * kLine), kModWhite);
    }

private:
    std::string label_;
    int labelWidth_;
    std::vector<std::string> lines_;
};

}  // namespace

std::vector<std::pair<std::string, std::string>> ModsScreen::facts(const ModInfo& m) {
    std::vector<std::pair<std::string, std::string>> out;
    const auto add = [&](const char* label, const std::string& value) {
        if (!value.empty()) out.emplace_back(label, value);
    };
    add("Authors", joined(m.authors, ", "));
    add("Contributors", joined(m.contributors, ", "));
    add("License", m.license);
    add("Mod ID", m.id);
    add("Loader", m.loader);
    add("Environment", m.environment == "client" ? "Client only" : m.environment == "server" ? "Server only" : "Client and server");
    add("Minecraft", m.minecraft);
    add("Requires", joined(m.depends, ", "));
    add("Homepage", m.homepage);
    add("Source", m.sources);
    add("Issues", m.issues);
    add("File", m.file.filename().string() + " (" + sizeText(m.size) + ")");
    if (m.bundled > 0) add("Bundled", std::to_string(m.bundled) + (m.bundled == 1 ? " library" : " libraries"));
    return out;
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

void ModsScreen::showDetails(int index) {
    if (details_ == nullptr) return;
    details_->clearEntries();
    selected_ = index;
    if (index < 0 || static_cast<std::size_t>(index) >= mods_.size()) return;
    const Listed& mod = mods_[static_cast<std::size_t>(index)];
    const int width = details_->width - 14;  // (a row of the list, less the scrollbar)
    details_->addEntry<HeaderEntry>(*this, static_cast<std::size_t>(index));
    details_->addEntry<LinesEntry>(std::vector<std::string>{}, kModWhite);  // a gap
    if (mod.verdict.blocked) {
        details_->addEntry<LinesEntry>(wrapped(font(), mod.verdict.reason, width), kModRed);
        details_->addEntry<LinesEntry>(std::vector<std::string>{}, kModWhite);
    }
    if (!mod.info.description.empty()) {
        details_->addEntry<LinesEntry>(wrapped(font(), mod.info.description, width), kModLight);
        details_->addEntry<LinesEntry>(std::vector<std::string>{}, kModWhite);
    }
    for (const auto& [label, value] : facts(mod.info)) details_->addEntry<FactEntry>(font(), label, value, width);
}

void ModsScreen::init() {
    addCenteredLabel("Mods", width_ / 2, 10);
    addCenteredLabel("Mods are only listed for now: Falcon does not load them yet.", width_ / 2, 23, 0xAAAAAAu);

    const int footerY = height_ - kFooterHeight + (kFooterHeight - kButtonHeight) / 2;
    const auto footer = layout::centeredRow(width_, footerY, {kButtonWidth, kButtonWidth}, 8, kButtonHeight);
    addButton("Open Mods Folder", footer[0], [this] {
        std::error_code ignored;
        std::filesystem::create_directories(services_.modsDir, ignored);
        openFolderInFileManager(services_.modsDir.string());
    });
    addButton(text("gui.done"), footer[1], [this] { onClose(); });

    list_ = nullptr;
    details_ = nullptr;
    if (mods_.empty()) {
        addCenteredLabel("No mods found", width_ / 2, height_ / 2 - 14, 0xFFFFFFu);
        addCenteredLabel("Mods you install from the launcher are put in", width_ / 2, height_ / 2 - 2, 0xAAAAAAu);
        addCenteredLabel(services_.modsDir.string(), width_ / 2, height_ / 2 + 8, 0xAAAAAAu);
        return;
    }
    const int top = kHeaderHeight, listHeight = height_ - kHeaderHeight - kFooterHeight;
    const int listWidth = std::clamp(width_ * 2 / 5, 150, 210);
    list_ = &addWidget<ScrollList>(10, top, listWidth, listHeight, kRowHeight, /*rowWidth=*/0, ScrollList::ScrollbarPlacement::InsideRight, /*shrinkRowsWhenScrollable=*/true, /*selectable=*/true);
    for (std::size_t i = 0; i < mods_.size(); ++i) list_->addEntry<ModRow>(*this, i);
    const int panelX = 10 + listWidth + 10;
    details_ = &addWidget<ScrollList>(panelX, top, std::max(100, width_ - panelX - 10), listHeight, kLine, 0, ScrollList::ScrollbarPlacement::InsideRight, true, /*selectable=*/false);
    list_->onSelectionChanged = [this](int index) { showDetails(index); };
    list_->setSelectedIndex(selected_);
    showDetails(selected_);
    setFocus(list_);
}

void ModsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
