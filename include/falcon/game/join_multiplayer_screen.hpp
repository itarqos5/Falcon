#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/net/server_pinger.hpp"
#include "falcon/ui/rect.hpp"
#include "falcon/ui/screen.hpp"
#include "falcon/ui/scroll_list.hpp"
#include "falcon/world/server_list.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

inline constexpr int kMultiplayerHeaderHeight = 33;
inline constexpr int kMultiplayerFooterHeight = 60;
inline constexpr int kServerRowHeight = 36;
inline constexpr int kServerRowWidth = 305;

struct MultiplayerLayout {
    int titleX = 0;
    int titleY = 0;
    Rect list;
    Rect join, direct, add;           // the top row of buttons (100 wide)
    Rect edit, remove, refresh, back; // the bottom row (74 wide)
};

// JoinMultiplayerScreen's geometry: header 33 (the title), footer 60 (two rows of buttons), the list between.
[[nodiscard]] MultiplayerLayout computeMultiplayerLayout(int width, int height, int titleWidth);

// The "Play Multiplayer" screen: the servers saved in <game dir>/servers.dat, each showing its name, message of
// the day, player count, favicon and ping bars as its status ping comes in, with Join Server, Direct Connection,
// Add Server, Edit, Delete, Refresh and Back.
//
// What it does not do: join a server (there is no game to play on one yet; Join Server only logs that), find
// LAN worlds (no "Scanning for games on your local network" row), or follow the game's blocked-server list.
// Each listed server is contacted with one status ping when its row is first drawn.
class JoinMultiplayerScreen : public Screen {
public:
    JoinMultiplayerScreen(ScreenNavigator& navigator, ScreenServices& services);
    ~JoinMultiplayerScreen() override;

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;

protected:
    void init() override;

private:
    void fillRows();
    void updateButtons();
    [[nodiscard]] std::shared_ptr<ServerEntry> selectedServer() const;
    std::shared_ptr<ServerPing> pingFor(const std::shared_ptr<ServerEntry>& server);  // starts it on first use
    void forgetPing(const ServerEntry* server);

    void join(const std::shared_ptr<ServerEntry>& server);
    void joinSelected();
    void openAddServer();
    void openEditServer();
    void openDirectConnection();
    void deleteSelected();
    void refresh();

    ScreenServices& services_;
    ServerList servers_;
    std::map<const ServerEntry*, std::shared_ptr<ServerPing>> pings_;
    std::vector<std::shared_ptr<ServerEntry>> shown_;  // the list's rows, in order
    std::optional<std::pair<int, int>> pendingSwap_;   // applied between frames: a row must not rebuild the list it is in

    ScrollList* list_ = nullptr;
    Button* join_ = nullptr;
    Button* edit_ = nullptr;
    Button* delete_ = nullptr;
};

}  // namespace falcon
