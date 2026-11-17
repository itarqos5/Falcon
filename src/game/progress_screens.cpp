#include "falcon/game/progress_screens.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/item_registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace falcon {
namespace {

// A name from the language, or `fallback` when the key has no entry (the lookup answers with the key itself then).
std::string translated(const Screen* /*unused*/, const std::string& value, const std::string& key, const std::string& fallback) {
    return value.empty() || value == key ? fallback : value;
}

std::string prettyName(std::string id) {
    for (char& c : id) {
        if (c == '_') c = ' ';
    }
    if (!id.empty()) id[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(id[0])));
    return id;
}

std::string withCommas(std::int64_t n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), ",");
    return (n < 0 ? "-" : "") + digits;
}

std::string fixed(double v, int decimals) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
    return buffer;
}

// The game's StatFormatters: a time in ticks as days, hours, minutes or seconds, a distance in centimetres as km, m or cm, damage in tenths of a heart.
std::string formatTime(std::int64_t ticks) {
    const double seconds = static_cast<double>(ticks) / 20.0, minutes = seconds / 60.0, hours = minutes / 60.0, days = hours / 24.0;
    if (days > 0.5) return fixed(days, 2) + " d";
    if (hours > 0.5) return fixed(hours, 2) + " h";
    if (minutes > 0.5) return fixed(minutes, 2) + " m";
    return fixed(seconds, 2) + " s";
}
std::string formatDistance(std::int64_t centimetres) {
    const double metres = static_cast<double>(centimetres) / 100.0, km = metres / 1000.0;
    if (km > 0.5) return fixed(km, 2) + " km";
    if (metres > 0.5) return fixed(metres, 2) + " m";
    return std::to_string(centimetres) + " cm";
}

}  // namespace

// ---- Advancements ---------------------------------------------------------------------------------------------------

void AdvancementsScreen::init() {}

void AdvancementsScreen::selectTab(int tab) {
    tab_ = tab;
    centred_ = false;
}

void AdvancementsScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime& time) {
    g.setTimeMillis(time.millis);
    ui().drawBackdrop(g, width_, height_, time.deltaTicks);
    GameSession* session = services_.session;
    const AdvancementTree* tree = session != nullptr ? session->advancementTree() : nullptr;
    const int winX = (width_ - kWindowWidth) / 2, winY = (height_ - kWindowHeight) / 2;
    const int insideX = winX + kInsideX, insideY = winY + kInsideY;
    const std::set<std::string> none;
    const std::set<std::string>& progress = session != nullptr ? session->advancements() : none;
    const int tabs = tree != nullptr ? static_cast<int>(tree->roots().size()) : 0;
    tab_ = std::clamp(tab_, 0, std::max(0, tabs - 1));

    // The tabs (behind the window), the tree inside it, then the window's frame over both.
    for (int i = 0; i < tabs; ++i) {
        const std::string place = i == 0 ? "left" : "middle";
        g.blitSprite(std::string("advancements/tab_above_") + place + (i == tab_ ? "_selected" : ""), winX + 4 + i * 32, winY - 28, 28, 32);
    }
    std::vector<int> nodes;
    int hovered = -1;
    if (tree != nullptr && tabs > 0) {
        const int root = tree->roots()[static_cast<std::size_t>(tab_)];
        nodes = tree->tabOf(root);
        const AdvancementDef& rootDef = tree->all()[static_cast<std::size_t>(root)];
        if (!centred_) {  // the tree starts in the middle of the window
            float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
            for (const int n : nodes) {
                if (!tree->visible(n, progress)) continue;  // (what shows is put in the middle, not the whole tree)
                const AdvancementDef& d = tree->all()[static_cast<std::size_t>(n)];
                minX = std::min(minX, d.x * 28.0f), maxX = std::max(maxX, d.x * 28.0f + 26.0f);
                minY = std::min(minY, d.y * 27.0f), maxY = std::max(maxY, d.y * 27.0f + 26.0f);
            }
            scrollX_ = std::floor(kInsideWidth / 2.0 - (maxX + minX) / 2.0);
            scrollY_ = std::floor(kInsideHeight / 2.0 - (maxY + minY) / 2.0);
            centred_ = true;
        }
        g.enableScissor(insideX, insideY, insideX + kInsideWidth, insideY + kInsideHeight);
        g.fill(insideX, insideY, insideX + kInsideWidth, insideY + kInsideHeight, 0xFF000000u);
        const std::string background = rootDef.background.empty() ? "gui/advancements/backgrounds/stone" : rootDef.background;
        const Texture2D& tile = g.textures().get(background + ".png");
        const int offX = static_cast<int>(std::floor(std::fmod(scrollX_, 16.0) + 16.0)) % 16, offY = static_cast<int>(std::floor(std::fmod(scrollY_, 16.0) + 16.0)) % 16;
        for (int ty = -1; ty * 16 < kInsideHeight; ++ty) {
            for (int tx = -1; tx * 16 < kInsideWidth; ++tx) g.blit(tile, insideX + tx * 16 + offX, insideY + ty * 16 + offY, 0.0f, 0.0f, 16, 16, 16, 16);
        }
        const auto nodeX = [&](const AdvancementDef& d) { return insideX + static_cast<int>(std::floor(scrollX_ + d.x * 28.0f)); };
        const auto nodeY = [&](const AdvancementDef& d) { return insideY + static_cast<int>(std::floor(scrollY_ + d.y * 27.0f)); };
        // The lines from a parent to its children: black under white while it is not done, white when it is.
        for (const int n : nodes) {
            const AdvancementDef& d = tree->all()[static_cast<std::size_t>(n)];
            if (!tree->visible(n, progress)) continue;
            for (const int c : d.children) {
                if (!tree->visible(c, progress)) continue;
                const AdvancementDef& child = tree->all()[static_cast<std::size_t>(c)];
                const bool lit = progress.count(child.id) != 0;
                const int x0 = nodeX(d) + 13, y0 = nodeY(d) + 13, x1 = nodeX(child) + 13, y1 = nodeY(child) + 13, mid = x0 + (x1 - x0) / 2;
                const std::uint32_t outline = 0xFF000000u, colour = lit ? 0xFFFFFFFFu : 0xFF808080u;
                const auto hline = [&](int xa, int xb, int y, std::uint32_t colour2, int half) { g.fill(std::min(xa, xb) - half, y - half, std::max(xa, xb) + 1 + half, y + 1 + half, colour2); };
                const auto vline = [&](int x, int ya, int yb, std::uint32_t colour2, int half) { g.fill(x - half, std::min(ya, yb) - half, x + 1 + half, std::max(ya, yb) + 1 + half, colour2); };
                hline(x0, mid, y0, outline, 1), vline(mid, y0, y1, outline, 1), hline(mid, x1, y1, outline, 1);
                hline(x0, mid, y0, colour, 0), vline(mid, y0, y1, colour, 0), hline(mid, x1, y1, colour, 0);
            }
        }
        for (const int n : nodes) {
            const AdvancementDef& d = tree->all()[static_cast<std::size_t>(n)];
            if (!tree->visible(n, progress)) continue;
            const int nx = nodeX(d), ny = nodeY(d);
            const bool done = progress.count(d.id) != 0;
            g.blitSprite(std::string("advancements/") + d.frame + "_frame_" + (done ? "obtained" : "unobtained"), nx, ny, 26, 26);
            if (session != nullptr) {
                if (const auto item = itemRegistry().find(d.icon)) drawItem(g, session->itemIcons(), ItemStack(*item), nx + 5, ny + 5);
            }
            if (mouseX >= nx && mouseX < nx + 26 && mouseY >= ny && mouseY < ny + 26 && mouseX >= insideX && mouseX < insideX + kInsideWidth && mouseY >= insideY && mouseY < insideY + kInsideHeight) {
                hovered = n;
            }
        }
        g.disableScissor();
    }
    g.blit(g.textures().get("gui/advancements/window.png"), winX, winY, 0.0f, 0.0f, kWindowWidth, kWindowHeight, 256, 256);
    // The icons of the tabs, the title and how much is done.
    for (int i = 0; i < tabs && session != nullptr; ++i) {
        const AdvancementDef& root = tree->all()[static_cast<std::size_t>(tree->roots()[static_cast<std::size_t>(i)])];
        if (const auto item = itemRegistry().find(root.icon)) drawItem(g, session->itemIcons(), ItemStack(*item), winX + 4 + i * 32 + 6, winY - 28 + 9);
    }
    g.text(text("gui.advancements"), static_cast<float>(winX + 8), static_cast<float>(winY + 6), 0xFF404040u, false);
    if (tree != nullptr && tabs > 0) {
        const auto [done, total] = tree->count(tree->roots()[static_cast<std::size_t>(tab_)], progress);
        const std::string count = std::to_string(done) + "/" + std::to_string(total);
        g.text(count, static_cast<float>(winX + kWindowWidth - 8 - g.textWidth(count)), static_cast<float>(winY + 6), 0xFF404040u, false);
    } else {
        const std::string empty = text("advancements.empty");
        g.text(empty, static_cast<float>(winX + kWindowWidth / 2 - g.textWidth(empty) / 2), static_cast<float>(winY + 70), 0xFFFFFFFFu);
        g.text(text("advancements.sad_label"), static_cast<float>(winX + kWindowWidth / 2 - 4), static_cast<float>(winY + 90), 0xFFFFFFFFu);
    }
    if (hovered >= 0) {  // what it is called and what it asks for
        const AdvancementDef& d = tree->all()[static_cast<std::size_t>(hovered)];
        const std::string title = translated(this, text(d.titleKey()), d.titleKey(), d.id), description = translated(this, text(d.descriptionKey()), d.descriptionKey(), "");
        const int nx = insideX + static_cast<int>(std::floor(scrollX_ + d.x * 28.0f)), ny = insideY + static_cast<int>(std::floor(scrollY_ + d.y * 27.0f));
        g.setTooltip(description.empty() ? title : title + "\n" + description, mouseX, mouseY, nx, ny, 26, 26);
    }
    Screen::render(g, mouseX, mouseY, time);
}

bool AdvancementsScreen::mouseClicked(double x, double y, int button) {
    if (button != 0) return false;
    GameSession* session = services_.session;
    const AdvancementTree* tree = session != nullptr ? session->advancementTree() : nullptr;
    const int winX = (width_ - kWindowWidth) / 2, winY = (height_ - kWindowHeight) / 2;
    const int tabs = tree != nullptr ? static_cast<int>(tree->roots().size()) : 0;
    for (int i = 0; i < tabs; ++i) {
        const int tx = winX + 4 + i * 32, ty = winY - 28;
        if (x >= tx && x < tx + 28 && y >= ty && y < ty + 32) {
            navigator_.playButtonClick();
            selectTab(i);
            return true;
        }
    }
    if (x >= winX + kInsideX && x < winX + kInsideX + kInsideWidth && y >= winY + kInsideY && y < winY + kInsideY + kInsideHeight) {
        dragging_ = true;
        lastX_ = x;
        lastY_ = y;
        return true;
    }
    return false;
}

void AdvancementsScreen::mouseDragged(double x, double y) {
    if (!dragging_) return;
    scrollX_ += x - lastX_;
    scrollY_ += y - lastY_;
    lastX_ = x;
    lastY_ = y;
}

void AdvancementsScreen::mouseReleased(double, double, int) { dragging_ = false; }

// ---- Statistics -----------------------------------------------------------------------------------------------------

void StatisticsScreen::init() {
    constexpr int kButtonWidth = 100, kGap = 4;
    const int startX = (width_ - (3 * kButtonWidth + 2 * kGap)) / 2, y = height_ - 52;
    static constexpr const char* kTabs[3] = {"stat.generalButton", "stat.itemsButton", "stat.mobsButton"};
    for (int i = 0; i < 3; ++i) {
        addButton(text(kTabs[i]), {startX + i * (kButtonWidth + kGap), y, kButtonWidth, 20}, [this, i] {
            tab_ = i;
            scroll_ = 0.0;
        });
    }
    addButton(text("gui.done"), {width_ / 2 - 100, height_ - 28, 200, 20}, [this] { onClose(); });
}

std::vector<StatisticsScreen::Row> StatisticsScreen::rows() const {
    std::vector<Row> out;
    const GameSession* session = services_.session;
    if (session == nullptr) return out;
    const auto nameOf = [this](const std::string& id) {
        for (const char* kind : {"block.minecraft.", "item.minecraft."}) {
            const std::string key = std::string(kind) + id;
            const std::string value = text(key);
            if (!value.empty() && value != key) return value;
        }
        return prettyName(id);
    };
    if (tab_ == 0) {
        struct General {
            const char* key;
            char kind;  // t time, d distance, c count, h tenths
        };
        static constexpr General kGeneral[] = {{"custom/play_time", 't'},        {"custom/total_world_time", 't'}, {"custom/time_since_death", 't'},     {"custom/jump", 'c'},
                                               {"custom/deaths", 'c'},           {"custom/mob_kills", 'c'},        {"custom/damage_dealt", 'h'},         {"custom/damage_taken", 'h'},
                                               {"custom/walk_one_cm", 'd'},      {"custom/sprint_one_cm", 'd'},    {"custom/crouch_one_cm", 'd'},        {"custom/swim_one_cm", 'd'},
                                               {"custom/walk_under_water_one_cm", 'd'}, {"custom/fall_one_cm", 'd'},     {"custom/climb_one_cm", 'd'},         {"custom/fly_one_cm", 'd'}};
        for (const General& g : kGeneral) {
            const std::int64_t v = session->stat(g.key);
            const std::string key = std::string("stat.minecraft.") + (g.key + 7);
            Row row;
            row.name = translated(this, text(key), key, prettyName(g.key + 7));
            row.values = {g.kind == 't' ? formatTime(v) : g.kind == 'd' ? formatDistance(v) : g.kind == 'h' ? fixed(static_cast<double>(v) / 10.0, 1) : withCommas(v)};
            out.push_back(std::move(row));
        }
    } else if (tab_ == 1) {
        std::set<std::string> names;
        for (const auto& [key, value] : session->stats()) {
            if (key.compare(0, 6, "mined/") == 0) names.insert(key.substr(6));
            if (key.compare(0, 10, "picked_up/") == 0) names.insert(key.substr(10));
        }
        out.push_back({"", {"Mined", "Picked up"}});
        for (const std::string& n : names) out.push_back({nameOf(n), {withCommas(session->stat("mined/" + n)), withCommas(session->stat("picked_up/" + n))}});
        if (names.empty()) out.push_back({"Nothing mined or picked up yet.", {}});
    } else {
        for (const auto& [key, value] : session->stats()) {
            if (key.compare(0, 7, "killed/") != 0) continue;
            const std::string id = key.substr(7), k = "entity.minecraft." + id;
            out.push_back({translated(this, text(k), k, prettyName(id)), {"Killed " + withCommas(value)}});
        }
        if (out.empty()) out.push_back({"No mob has been killed yet.", {}});
    }
    return out;
}

void StatisticsScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(g, width_, height_, time.deltaTicks);
    const std::string title = text("gui.stats");
    g.text(title, static_cast<float>(width_ / 2 - g.textWidth(title) / 2), 14.0f, kWhite);
    const int listW = std::min(width_ - 40, 360), listX = (width_ - listW) / 2, top = 32, bottom = height_ - 60, rowH = 14;
    g.fill(listX, top, listX + listW, bottom, 0x90000000u);
    const std::vector<Row> list = rows();
    const double maxScroll = std::max(0.0, static_cast<double>(list.size()) * rowH - (bottom - top));
    scroll_ = std::clamp(scroll_, 0.0, maxScroll);
    g.enableScissor(listX, top, listX + listW, bottom);
    int y = top - static_cast<int>(scroll_);
    for (std::size_t i = 0; i < list.size(); ++i, y += rowH) {
        if (y + rowH < top || y > bottom) continue;
        if (i % 2 == 0) g.fill(listX, y, listX + listW, y + rowH, 0x30FFFFFFu);
        g.text(list[i].name, static_cast<float>(listX + 6), static_cast<float>(y + 3), kWhite);
        // The values stand from the right edge: the last one flush right, the one before it a column to the left.
        int x = listX + listW - 6;
        for (auto it = list[i].values.rbegin(); it != list[i].values.rend(); ++it) {
            g.text(*it, static_cast<float>(x - g.textWidth(*it)), static_cast<float>(y + 3), 0xFFE0E0E0u);
            x -= 90;
        }
    }
    g.disableScissor();
    Screen::render(g, mouseX, mouseY, time);
}

bool StatisticsScreen::mouseScrolled(double, double, double dy) {
    scroll_ -= dy * 14.0;
    return true;
}

// ---- World Options and Game Rules -----------------------------------------------------------------------------------

void WorldOptionsScreen::addOptions() {
    GameSession* session = services_.session;
    if (session == nullptr) return;
    addHeader(text("options.worldOptions.general.title"));

    static const std::array<const char*, 4> kDifficulty = {"options.difficulty.peaceful", "options.difficulty.easy", "options.difficulty.normal", "options.difficulty.hard"};
    auto difficulty = makeChoice("options.difficulty", {kDifficulty[0], kDifficulty[1], kDifficulty[2], kDifficulty[3]}, session->difficulty(), kBigWidth, nullptr);
    CycleButton* difficultyRaw = difficulty.get();
    const auto tip = [this](int i) { return text(std::string(kDifficulty[static_cast<std::size_t>(i)]) + ".info"); };
    difficultyRaw->tooltip = tip(session->difficulty());
    difficulty->onChange = [this, difficultyRaw, tip](int i) {
        if (services_.session != nullptr) services_.session->setDifficulty(i);
        difficultyRaw->tooltip = tip(i);
    };
    addBig(std::move(difficulty));

    auto mode = makeChoice("Game Mode", {"gameMode.survival", "gameMode.creative", "gameMode.adventure", "gameMode.spectator"}, session->gameType(), kSmallWidth,
                           [this](int i) {
                               if (services_.session != nullptr) services_.session->requestGameType(i);
                           });
    auto rules = makeLink("editGamerule.inGame.button", kSmallWidth, [this] { return std::make_unique<GameRulesScreen>(navigator_, services_); });
    mode->active = session->allowCommands();
    rules->active = session->allowCommands();
    if (!session->allowCommands()) {
        mode->tooltip = text("options.worldOptions.game_mode.disabled.operator.tooltip");
        rules->tooltip = text("editGamerule.inGame.disabled.tooltip");
    }
    Widget* modeRaw = mode.get();
    Widget* rulesRaw = rules.get();
    addPair(std::move(mode), std::move(rules));

    auto cheats = makeToggle("selectWorld.allowCommands", session->allowCommands(), kBigWidth, [this, modeRaw, rulesRaw](bool on) {
        if (services_.session == nullptr) return;
        services_.session->setAllowCommands(on);
        modeRaw->active = on;
        rulesRaw->active = on;
        modeRaw->tooltip = on ? std::string() : text("options.worldOptions.game_mode.disabled.operator.tooltip");
        rulesRaw->tooltip = on ? std::string() : text("editGamerule.inGame.disabled.tooltip");
    });
    cheats->tooltip = text("options.worldOptions.allow_commands.tooltip");
    addBig(std::move(cheats));
}

void GameRulesScreen::addOptions() {
    GameSession* session = services_.session;
    if (session == nullptr) return;
    std::vector<std::unique_ptr<Widget>> widgets;
    for (const auto& [rule, fallback] : GameSession::knownGameRules()) {
        const std::string label = prettyName(rule);
        const std::string defaults = formatLanguageString(text("editGamerule.default"), {fallback});
        if (fallback == "true" || fallback == "false") {
            auto toggle = makeToggle(label, session->gameRuleValue(rule) == "true", kSmallWidth, [this, rule](bool on) {
                if (services_.session != nullptr) services_.session->setGameRule(rule, on ? "true" : "false");
            });
            toggle->tooltip = defaults;
            widgets.push_back(std::move(toggle));
        } else {
            const int value = std::atoi(session->gameRuleValue(rule).c_str()), base = std::atoi(fallback.c_str());
            const int min = rule == "fire_spread_radius_around_player" ? -1 : 0, max = std::max({100, base * 2, value});
            auto slider = makeIntSlider(min, max, std::clamp(value, min, max), kSmallWidth, [label](int n) { return label + ": " + std::to_string(n); }, [this, rule](int n) {
                if (services_.session != nullptr) services_.session->setGameRule(rule, std::to_string(n));
            });
            slider->tooltip = defaults;
            widgets.push_back(std::move(slider));
        }
    }
    for (std::size_t i = 0; i < widgets.size(); i += 2) {
        std::unique_ptr<Widget> second;
        if (i + 1 < widgets.size()) second = std::move(widgets[i + 1]);
        addPair(std::move(widgets[i]), std::move(second));
    }
}

}  // namespace falcon
