#pragma once

#include "apex/ui/rect.hpp"
#include "apex/ui/screen.hpp"
#include "apex/game/screen_services.hpp"
#include "apex/ui/scroll_list.hpp"
#include "apex/world/level_summary.hpp"

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace apex {

struct SelectWorldLayout {
    Rect title;
    Rect search;
    Rect list;
    Rect play;
    Rect create;
    Rect edit;
    Rect remove;
    Rect recreate;
    Rect back;
};

// SelectWorldScreen's geometry: header 49 (title, search box), footer 60 (two rows of buttons).
[[nodiscard]] SelectWorldLayout computeSelectWorldLayout(int width, int height, int titleWidth);

// Whether `filter` matches a world: a case-insensitive substring of its name or its folder name.
[[nodiscard]] bool worldMatchesFilter(const LevelSummary& summary, const std::string& filter);

// The Singleplayer menu ("Select World"): the worlds in <game dir>/saves, newest first, a search
// box, and the Play / Create / Edit / Delete / Re-Create / Back buttons. The folder is scanned on
// a background thread; the list shows "Loading World List" until it is done.
class SelectWorldScreen : public Screen {
public:
    SelectWorldScreen(ScreenNavigator& navigator, ScreenServices& services);
    ~SelectWorldScreen() override;

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    using ScanResult = std::optional<std::vector<LevelSummary>>;

    void startScan();
    void pollScan();
    void fillEntries();
    void updateButtons();
    [[nodiscard]] const LevelSummary* selectedSummary() const;
    void joinSelected();
    void deleteSelected();
    // Opens Create New World; when a world was made the list is read again and that world is selected.
    void openCreateWorld();
    void notImplemented(const char* what) const;
    [[nodiscard]] std::filesystem::path savesDir() const { return services_.gameDir / "saves"; }

    ScreenServices& services_;
    std::future<ScanResult> scan_;
    std::vector<std::shared_ptr<const LevelSummary>> levels_;  // the newest finished scan
    bool loaded_ = false;
    std::vector<std::shared_ptr<const LevelSummary>> shown_;   // levels_ after the filter = list rows
    std::string filter_;
    std::string selectAfterScan_;  // a world just created: selected once the next scan has found it

    ScrollList* list_ = nullptr;
    Button* play_ = nullptr;
    Button* edit_ = nullptr;
    Button* delete_ = nullptr;
    Button* recreate_ = nullptr;
};

}  // namespace apex
