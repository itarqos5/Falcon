#include "falcon/game/join_multiplayer_screen.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/game/server_screens.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/ui/confirm_screen.hpp"
#include "falcon/util/text_util.hpp"

#include <chrono>
#include <cstdio>

namespace falcon {
namespace {

constexpr std::uint32_t kGrey = 0xFF808080u;       // the message of the day, and the default text colour
constexpr std::uint32_t kDarkGrey = 0xFF555555u;   // the "/" of the player count
constexpr std::uint32_t kLightGrey = 0xFFAAAAAAu;  // its numbers
constexpr std::uint32_t kRed = 0xFFFF5555u;        // an incompatible server's version
constexpr std::uint32_t kErrorRed = 0xFFFF0000u;   // "Can't connect to server"
constexpr int kContentPadding = 2;
constexpr int kContentWidth = 301;                 // kServerRowWidth - 2 * padding
constexpr int kIconSize = 32;
constexpr int kStatusIconWidth = 10;
constexpr int kStatusIconHeight = 8;
constexpr int kMaxFaviconSize = 1024;
constexpr auto kDoubleClick = std::chrono::milliseconds(250);
constexpr int kKeyF5 = 294;
constexpr int kKeyDown = 264;
constexpr int kKeyUp = 265;
constexpr int kModShift = 1;

// What a row needs from the screen that owns it.
struct RowContext {
    const Language& language;
    const Font& font;
    std::function<std::shared_ptr<ServerPing>(const std::shared_ptr<ServerEntry>&)> pingFor;
    std::function<void(const std::shared_ptr<ServerEntry>&)> join;
    std::function<void(int from, int to)> swap;
    std::function<void()> persist;  // servers.dat changed (a favicon arrived)
};

// The status icon (ping bars) for a finished ping: faster is more bars.
const char* pingSprite(std::int64_t ms) {
    if (ms < 150) return "server_list/ping_5";
    if (ms < 300) return "server_list/ping_4";
    if (ms < 600) return "server_list/ping_3";
    if (ms < 1000) return "server_list/ping_2";
    return "server_list/ping_1";
}

// One server in the list.
class ServerRow : public ScrollList::Entry {
public:
    ServerRow(std::shared_ptr<ServerEntry> server, int index, int count, std::shared_ptr<RowContext> context)
        : Entry(kServerRowHeight), server_(std::move(server)), index_(index), count_(count), ctx_(std::move(context)) {}

    void render(GuiGraphics& g, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState& state) override {
        const int cx = r.x + kContentPadding;
        const int cy = r.y + kContentPadding;

        // The ping starts the first time the row is drawn, so servers scrolled out of view are left alone.
        const PingSnapshot snap = ctx_->pingFor(server_)->snapshot();
        syncIcon(g, snap);

        g.text(server_->name, static_cast<float>(cx + kIconSize + 3), static_cast<float>(cy + 1), kWhite);
        drawMotd(g, snap, cx + kIconSize + 3, cy + 12);
        drawIcon(g, cx, cy);

        // Status icon at the right, and the text to its left.
        const int iconX = cx + kContentWidth - kStatusIconWidth - 5;
        const char* sprite = nullptr;
        std::string iconTooltip;
        switch (snap.state) {
            case PingState::Pinging: {
                // The bars pulse 1,2,3,4,5,4,3,2 ... one step per 100 ms, each row a little out of step.
                int n = static_cast<int>(((static_cast<long long>(g.timeMillis()) / 100) + index_ * 2) & 7);
                if (n > 4) n = 8 - n;
                static constexpr const char* kPinging[] = {"server_list/pinging_1", "server_list/pinging_2", "server_list/pinging_3",
                                                           "server_list/pinging_4", "server_list/pinging_5"};
                sprite = kPinging[n];
                iconTooltip = ctx_->language.get("multiplayer.status.pinging");
                break;
            }
            case PingState::Unreachable:
                sprite = "server_list/unreachable";
                iconTooltip = ctx_->language.get("multiplayer.status.no_connection");
                break;
            case PingState::Incompatible:
                sprite = "server_list/incompatible";
                iconTooltip = ctx_->language.get("multiplayer.status.incompatible");
                break;
            case PingState::Successful:
                sprite = pingSprite(snap.pingMillis);
                iconTooltip = formatLanguageString(ctx_->language.get("multiplayer.status.ping"), {std::to_string(snap.pingMillis)});
                break;
        }
        g.blitSprite(sprite, iconX, cy, kStatusIconWidth, kStatusIconHeight);

        std::string playersTooltip;
        int textX = iconX - 5;
        int textW = 0;
        if (snap.message == PingMessage::None && snap.state != PingState::Unreachable) {
            if (snap.state == PingState::Incompatible) {
                const std::string version = snap.versionIsOld ? ctx_->language.get("multiplayer.status.old") : snap.versionName;
                textW = g.textWidth(version);
                textX = iconX - textW - 5;
                g.text(version, static_cast<float>(textX), static_cast<float>(cy + 1), kRed);
            } else if (snap.hasPlayers) {
                const std::string online = std::to_string(snap.playersOnline);
                const std::string max = std::to_string(snap.playersMax);
                textW = g.textWidth(online) + g.textWidth("/") + g.textWidth(max);
                textX = iconX - textW - 5;
                float x = static_cast<float>(textX);
                g.text(online, x, static_cast<float>(cy + 1), kLightGrey);
                x += static_cast<float>(g.textWidth(online));
                g.text("/", x, static_cast<float>(cy + 1), kDarkGrey);
                x += static_cast<float>(g.textWidth("/"));
                g.text(max, x, static_cast<float>(cy + 1), kLightGrey);
            } else {
                const std::string unknown = ctx_->language.get("multiplayer.status.unknown");  // "???"
                textW = g.textWidth(unknown);
                textX = iconX - textW - 5;
                g.text(unknown, static_cast<float>(textX), static_cast<float>(cy + 1), kDarkGrey);
            }
            // Who is online, shown while the cursor is over the count.
            for (const std::string& name : snap.playerSample) {
                if (!playersTooltip.empty()) playersTooltip += '\n';
                playersTooltip += name.empty() ? ctx_->language.get("multiplayer.status.anonymous_player") : name;
            }
            if (snap.playersNotListed > 0) {
                if (!playersTooltip.empty()) playersTooltip += '\n';
                playersTooltip += formatLanguageString(ctx_->language.get("multiplayer.status.and_more"), {std::to_string(snap.playersNotListed)});
            }
        }

        if (mouseX >= iconX && mouseX <= iconX + kStatusIconWidth && mouseY >= cy && mouseY <= cy + kStatusIconHeight) {
            g.setTooltip(iconTooltip, mouseX, mouseY, iconX, cy, kStatusIconWidth, kStatusIconHeight);
        } else if (!playersTooltip.empty() && mouseX >= textX && mouseX <= textX + textW && mouseY >= cy && mouseY <= cy + 8) {
            g.setTooltip(playersTooltip, mouseX, mouseY, textX, cy, textW, 9);
        }

        if (state.hovered) drawOverlay(g, cx, cy, mouseX, mouseY);
    }

    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override {
        if (button != 0) return {};
        const int mx = static_cast<int>(px) - (r.x + kContentPadding);
        const int my = static_cast<int>(py) - (r.y + kContentPadding);
        if (overRightHalf(mx, my)) {  // the join arrow
            ctx_->join(server_);
            return {true, true};
        }
        if (index_ > 0 && overTopLeft(mx, my)) {
            ctx_->swap(index_, index_ - 1);
            return {true, true};
        }
        if (index_ < count_ - 1 && overBottomLeft(mx, my)) {
            ctx_->swap(index_, index_ + 1);
            return {true, true};
        }
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClick = lastClick_ && now - *lastClick_ < kDoubleClick;
        lastClick_ = now;
        if (!doubleClick) return {};
        lastClick_.reset();
        ctx_->join(server_);
        return {true, true};
    }

    [[nodiscard]] const std::shared_ptr<ServerEntry>& server() const { return server_; }

private:
    static bool overRightHalf(int mx, int my) { return mx >= kIconSize / 2 && mx < kIconSize && my >= 0 && my < kIconSize; }
    static bool overTopLeft(int mx, int my) { return mx >= 0 && mx < kIconSize / 2 && my >= 0 && my < kIconSize / 2; }
    static bool overBottomLeft(int mx, int my) { return mx >= 0 && mx < kIconSize / 2 && my >= kIconSize / 2 && my < kIconSize; }

    // The favicon the server sent replaces the saved one; a saved or received icon that is not a 64x64 PNG is dropped.
    void syncIcon(GuiGraphics&, const PingSnapshot& snap) {
        if (snap.revision != lastRevision_) {
            lastRevision_ = snap.revision;
            if (!snap.faviconPng.empty() && snap.faviconPng != server_->iconPng) {
                server_->iconPng = snap.faviconPng;
                ctx_->persist();
            }
        }
        if (server_->iconPng == iconSource_) return;
        iconSource_ = server_->iconPng;
        icon_.reset();
        if (iconSource_.empty()) return;
        const auto image = decodePng(iconSource_);
        if (image && image->width == 64 && image->height == 64 && iconSource_.size() <= static_cast<std::size_t>(kMaxFaviconSize) * kMaxFaviconSize) {
            icon_.emplace(*image, FilterMode::Nearest);
        } else {
            server_->iconPng.clear();  // "Invalid icon": forget it
            iconSource_.clear();
            ctx_->persist();
        }
    }

    void drawIcon(GuiGraphics& g, int cx, int cy) {
        if (icon_) {
            g.blit(*icon_, cx, cy, 0.0f, 0.0f, kIconSize, kIconSize, 64, 64, 64, 64);
        } else {
            const Texture2D& unknown = g.textures().get("misc/unknown_server.png");
            g.blit(unknown, cx, cy, 0.0f, 0.0f, kIconSize, kIconSize, unknown.width(), unknown.height(), unknown.width(),
                   unknown.height());
        }
    }

    // The message of the day: up to two lines, in the server's own colours (grey where it gives none). While
    // the ping has not produced one, a status line in its place.
    void drawMotd(GuiGraphics& g, const PingSnapshot& snap, int x, int y) {
        if (snap.message != PingMessage::None) {
            const char* key = snap.message == PingMessage::Pinging ? "multiplayer.status.pinging"
                              : snap.message == PingMessage::CannotConnect ? "multiplayer.status.cannot_connect"
                                                                           : "multiplayer.status.cannot_resolve";
            g.text(ctx_->language.get(key), static_cast<float>(x), static_cast<float>(y),
                   snap.message == PingMessage::Pinging ? kGrey : kErrorRed);
            return;
        }
        if (snap.revision != wrappedRevision_) {
            wrappedRevision_ = snap.revision;
            wrapped_ = wrapStyledText(ctx_->font, snap.motd, kContentWidth - kIconSize - 2, 2);
        }
        int lineY = y;
        for (const StyledText& line : wrapped_) {
            float lineX = static_cast<float>(x);
            for (const TextSpan& span : line) {
                const std::uint32_t argb = span.hasColor ? (0xFF000000u | span.rgb) : kGrey;
                g.text(span.text, lineX, static_cast<float>(lineY), argb, true, span.underlined, span.bold);
                lineX += static_cast<float>(g.textWidth(span.text, span.bold));
            }
            lineY += 9;
        }
    }

    // While the row is hovered: the icon is greyed and shows the join arrow and the move up / down arrows.
    void drawOverlay(GuiGraphics& g, int cx, int cy, int mouseX, int mouseY) const {
        g.fill(cx, cy, cx + kIconSize, cy + kIconSize, 0xA0909090u);
        const int mx = mouseX - cx;
        const int my = mouseY - cy;
        const bool join = overRightHalf(mx, my);
        g.blitSprite(join ? "server_list/join_highlighted" : "server_list/join", cx, cy, kIconSize, kIconSize);
        if (join) g.requestCursor(CursorType::PointingHand);
        if (index_ > 0) {
            const bool up = overTopLeft(mx, my);
            g.blitSprite(up ? "server_list/move_up_highlighted" : "server_list/move_up", cx, cy, kIconSize, kIconSize);
            if (up) g.requestCursor(CursorType::PointingHand);
        }
        if (index_ < count_ - 1) {
            const bool down = overBottomLeft(mx, my);
            g.blitSprite(down ? "server_list/move_down_highlighted" : "server_list/move_down", cx, cy, kIconSize, kIconSize);
            if (down) g.requestCursor(CursorType::PointingHand);
        }
    }

    std::shared_ptr<ServerEntry> server_;
    int index_;
    int count_;
    std::shared_ptr<RowContext> ctx_;
    std::optional<Texture2D> icon_;
    std::vector<unsigned char> iconSource_;  // the PNG icon_ was made from
    std::uint64_t lastRevision_ = 0;
    std::uint64_t wrappedRevision_ = ~std::uint64_t{0};
    std::vector<StyledText> wrapped_;
    std::optional<std::chrono::steady_clock::time_point> lastClick_;
};

}  // namespace

MultiplayerLayout computeMultiplayerLayout(int width, int height, int titleWidth) {
    MultiplayerLayout l;
    l.titleX = static_cast<int>(0.5f * static_cast<float>(width - titleWidth));
    l.titleY = static_cast<int>(0.5f * static_cast<float>(kMultiplayerHeaderHeight - 9));  // 12
    l.list = {0, kMultiplayerHeaderHeight, width, height - kMultiplayerHeaderHeight - kMultiplayerFooterHeight};

    const int x0 = static_cast<int>(0.5f * static_cast<float>(width - 308));
    const int top = height - kMultiplayerFooterHeight + static_cast<int>(0.5f * static_cast<float>(kMultiplayerFooterHeight - 44));
    l.join = {x0, top, 100, 20};
    l.direct = {x0 + 104, top, 100, 20};
    l.add = {x0 + 208, top, 100, 20};
    l.edit = {x0, top + 24, 74, 20};
    l.remove = {x0 + 78, top + 24, 74, 20};
    l.refresh = {x0 + 156, top + 24, 74, 20};
    l.back = {x0 + 234, top + 24, 74, 20};
    return l;
}

JoinMultiplayerScreen::JoinMultiplayerScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator), services_(services), servers_(services.gameDir) {
    servers_.load();
}

JoinMultiplayerScreen::~JoinMultiplayerScreen() {
    for (auto& [server, ping] : pings_) ping->cancel();  // nothing is left running against a screen that is gone
}

std::shared_ptr<ServerPing> JoinMultiplayerScreen::pingFor(const std::shared_ptr<ServerEntry>& server) {
    auto& ping = pings_[server.get()];
    if (!ping) ping = ServerPing::start(server->ip);
    return ping;
}

void JoinMultiplayerScreen::forgetPing(const ServerEntry* server) {
    if (const auto it = pings_.find(server); it != pings_.end()) {
        it->second->cancel();
        pings_.erase(it);
    }
}

std::shared_ptr<ServerEntry> JoinMultiplayerScreen::selectedServer() const {
    if (list_ == nullptr) return nullptr;
    const int index = list_->selectedIndex();
    return index >= 0 && index < static_cast<int>(shown_.size()) ? shown_[static_cast<std::size_t>(index)] : nullptr;
}

void JoinMultiplayerScreen::fillRows() {
    if (list_ == nullptr) return;
    const std::shared_ptr<ServerEntry> keep = selectedServer();  // the same server stays selected across a rebuild
    list_->clearEntries();
    shown_ = servers_.visible();

    auto context = std::make_shared<RowContext>(RowContext{
        services_.language, services_.font, [this](const std::shared_ptr<ServerEntry>& s) { return pingFor(s); },
        [this](const std::shared_ptr<ServerEntry>& s) { join(s); },
        [this](int from, int to) { pendingSwap_ = std::make_pair(from, to); }, [this] { servers_.save(); }});
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        list_->addEntry<ServerRow>(shown_[i], static_cast<int>(i), static_cast<int>(shown_.size()), context);
    }
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        if (keep && shown_[i] == keep) list_->setSelectedIndex(static_cast<int>(i));
    }
    updateButtons();
}

void JoinMultiplayerScreen::updateButtons() {
    const bool any = selectedServer() != nullptr;
    if (join_ != nullptr) join_->active = any;
    if (edit_ != nullptr) edit_->active = any;
    if (delete_ != nullptr) delete_->active = any;
}

void JoinMultiplayerScreen::join(const std::shared_ptr<ServerEntry>& server) {
    // There is no game to join a server with yet.
    std::printf("[falcon] join server '%s' (%s) pressed (not implemented yet)\n", server->name.c_str(), server->ip.c_str());
    std::fflush(stdout);
}

void JoinMultiplayerScreen::joinSelected() {
    if (const auto server = selectedServer()) join(server);
}

void JoinMultiplayerScreen::openAddServer() {
    ServerEntry blank;  // an empty name (the box shows "Minecraft Server" as a hint), nothing yet in the address
    navigator_.push(std::make_unique<ManageServerScreen>(
        navigator_, services_, "manageServer.add.title", blank, [this](bool saved, const ServerEntry& edited) {
            if (!saved) return;
            if (const auto known = servers_.unhide(edited.ip)) {  // the address was a Direct Connection before: it moves to the list
                known->name = edited.name;
                known->ip = edited.ip;
            } else {
                servers_.add(std::make_shared<ServerEntry>(edited), false);
            }
            servers_.save();
            if (list_ != nullptr) list_->setSelectedIndex(-1);
            fillRows();
        }));
}

void JoinMultiplayerScreen::openEditServer() {
    const auto server = selectedServer();
    if (!server) return;
    navigator_.push(std::make_unique<ManageServerScreen>(
        navigator_, services_, "manageServer.edit.title", *server, [this, server](bool saved, const ServerEntry& edited) {
            if (!saved) return;
            const bool addressChanged = server->ip != edited.ip;
            server->name = edited.name;
            server->ip = edited.ip;
            server->packStatus = edited.packStatus;  // the icon stays
            if (addressChanged) forgetPing(server.get());  // a new address is pinged afresh
            servers_.save();
            fillRows();
        }));
}

void JoinMultiplayerScreen::openDirectConnection() {
    navigator_.push(std::make_unique<DirectJoinScreen>(navigator_, services_, [this](const std::string& address) {
        auto server = servers_.find(address);
        if (!server) {  // a new address is remembered (hidden from the list) so that it is known next time
            auto fresh = std::make_shared<ServerEntry>();
            fresh->name = services_.language.get("selectServer.defaultName");
            fresh->ip = address;
            servers_.add(fresh, true);
            servers_.save();
            server = fresh;
        }
        join(server);
    }));
}

void JoinMultiplayerScreen::deleteSelected() {
    const auto server = selectedServer();
    if (!server) return;
    const Language& lang = services_.language;
    navigator_.push(std::make_unique<ConfirmScreen>(
        navigator_, lang.get("selectServer.deleteQuestion"),
        formatLanguageString(lang.get("selectServer.deleteWarning"), {server->name}), lang.get("selectServer.deleteButton"),
        lang.get("gui.cancel"), [this, server](bool yes) {
            if (!yes) return;
            forgetPing(server.get());
            servers_.remove(server);
            servers_.save();
            if (list_ != nullptr) list_->setSelectedIndex(-1);
            fillRows();
        }));
}

void JoinMultiplayerScreen::refresh() {
    // A fresh screen reads servers.dat again and pings everything again.
    navigator_.pop();
    navigator_.push(std::make_unique<JoinMultiplayerScreen>(navigator_, services_));
}

void JoinMultiplayerScreen::init() {
    const std::string title = text("multiplayer.title");
    const MultiplayerLayout l = computeMultiplayerLayout(width_, height_, font().width(title));

    addLabel(title, l.titleX, l.titleY);
    list_ = &addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, kServerRowHeight, kServerRowWidth,
                                   ScrollList::ScrollbarPlacement::RightOfRow,
                                   /*shrinkRowsWhenScrollable=*/false, /*selectable=*/true);
    list_->onSelectionChanged = [this](int) { updateButtons(); };
    list_->onActivate = [this](int) { joinSelected(); };

    join_ = &addButton(text("selectServer.select"), l.join, [this] { joinSelected(); });
    addButton(text("selectServer.direct"), l.direct, [this] { openDirectConnection(); });
    addButton(text("selectServer.add"), l.add, [this] { openAddServer(); });
    edit_ = &addButton(text("selectServer.edit"), l.edit, [this] { openEditServer(); });
    delete_ = &addButton(text("selectServer.delete"), l.remove, [this] { deleteSelected(); });
    addButton(text("selectServer.refresh"), l.refresh, [this] { refresh(); });
    addButton(text("gui.back"), l.back, [this] { onClose(); });

    fillRows();
}

bool JoinMultiplayerScreen::keyPressed(int key, int mods) {
    if (key == kKeyF5) {
        refresh();
        return true;
    }
    if ((mods & kModShift) != 0 && (key == kKeyUp || key == kKeyDown) && list_ != nullptr) {  // Shift+Up / Down reorders
        const int index = list_->selectedIndex();
        const int target = key == kKeyUp ? index - 1 : index + 1;
        if (index >= 0 && target >= 0 && target < static_cast<int>(shown_.size())) pendingSwap_ = std::make_pair(index, target);
        return true;
    }
    return Screen::keyPressed(key, mods);
}

void JoinMultiplayerScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    if (pendingSwap_) {  // a row asked for a move: done here, because a row must not rebuild the list it is drawn in
        const auto [from, to] = *pendingSwap_;
        pendingSwap_.reset();
        servers_.swap(static_cast<std::size_t>(from), static_cast<std::size_t>(to));  // saves
        fillRows();
        list_->select(to, /*scrollIntoView=*/true);  // the moved server stays selected
    }
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
