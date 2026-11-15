#include "falcon/game/remote_container_screen.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {

namespace {
constexpr std::uint32_t kLabelColor = 0xFF404040u;
}

RemoteContainerScreen::RemoteContainerScreen(ScreenNavigator& navigator, ScreenServices& services, RemoteMenu& menu, std::string title)
    : ContainerScreen(navigator, services, 176, menu.imageHeight()), menu_(menu), title_(std::move(title)) {}

std::vector<SlotView> RemoteContainerScreen::slotViews() {
    std::vector<SlotView> views;
    for (int i = 0; i < menu_.slotCount(); ++i) views.push_back({&menu_.slot(i), menu_.slot(i).x, menu_.slot(i).y});
    return views;
}

void RemoteContainerScreen::drawBackground(GuiGraphics& g, int, int) {
    const auto picture = [&](const char* name) { g.blit(g.textures().get(name), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 256, 256); };
    switch (menu_.kind()) {
        case RemoteMenuKind::Chest: {  // ContainerScreen: the top (the rows) and the bottom (the inventory) of the same picture
            const Texture2D& texture = g.textures().get("gui/container/generic_54.png");
            const int rowsHeight = menu_.rows() * 18 + 17;
            g.blit(texture, leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, rowsHeight, 256, 256);
            g.blit(texture, leftPos_, topPos_ + rowsHeight, 0.0f, 126.0f, imageWidth_, 96, 256, 256);
            break;
        }
        case RemoteMenuKind::Shulker: picture("gui/container/shulker_box.png"); break;
        case RemoteMenuKind::Dispenser: picture("gui/container/dispenser.png"); break;
        case RemoteMenuKind::Hopper: picture("gui/container/hopper.png"); break;
        case RemoteMenuKind::Crafting: picture("gui/container/crafting_table.png"); break;
        case RemoteMenuKind::Furnace:
        case RemoteMenuKind::BlastFurnace:
        case RemoteMenuKind::Smoker: {
            const char* texture = menu_.kind() == RemoteMenuKind::Furnace ? "gui/container/furnace.png" : menu_.kind() == RemoteMenuKind::BlastFurnace ? "gui/container/blast_furnace.png" : "gui/container/smoker.png";
            const char* sprites = menu_.kind() == RemoteMenuKind::Furnace ? "container/furnace/" : menu_.kind() == RemoteMenuKind::BlastFurnace ? "container/blast_furnace/" : "container/smoker/";
            picture(texture);
            // AbstractFurnaceScreen: the flame (14 x 14, shrinking from the top as the fuel runs down) and the arrow (24 x 16, filling from the left as the food cooks).
            if (menu_.data(0) > 0) {
                const int total = menu_.data(1) == 0 ? 200 : menu_.data(1);
                const float lit = std::clamp(static_cast<float>(menu_.data(0)) / static_cast<float>(total), 0.0f, 1.0f);
                const int k = static_cast<int>(std::ceil(lit * 13.0f)) + 1;
                g.pushScissor(leftPos_ + 56, topPos_ + 36 + 14 - k, leftPos_ + 56 + 14, topPos_ + 36 + 14);
                g.blitSprite(std::string(sprites) + "lit_progress", leftPos_ + 56, topPos_ + 36, 14, 14);
                g.popScissor();
            }
            const int cookingTotal = menu_.data(3) == 0 ? 200 : menu_.data(3);
            const float burn = std::clamp(static_cast<float>(menu_.data(2)) / static_cast<float>(cookingTotal), 0.0f, 1.0f);
            const int width = static_cast<int>(std::ceil(burn * 24.0f));
            if (width > 0) {
                g.pushScissor(leftPos_ + 79, topPos_ + 34, leftPos_ + 79 + width, topPos_ + 34 + 16);
                g.blitSprite(std::string(sprites) + "burn_progress", leftPos_ + 79, topPos_ + 34, 24, 16);
                g.popScissor();
            }
            break;
        }
    }
}

void RemoteContainerScreen::drawLabels(GuiGraphics& g, int, int) {
    const bool centered = menu_.kind() == RemoteMenuKind::Dispenser || menu_.kind() == RemoteMenuKind::Furnace || menu_.kind() == RemoteMenuKind::BlastFurnace ||
                          menu_.kind() == RemoteMenuKind::Smoker;
    const int titleX = centered ? (imageWidth_ - g.textWidth(title_)) / 2 : 8;
    g.text(title_, static_cast<float>(leftPos_ + titleX), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
    const std::string inventory = text("container.inventory");
    g.text(inventory, static_cast<float>(leftPos_ + 8), static_cast<float>(topPos_ + imageHeight_ - 94), kLabelColor, /*shadow=*/false);
}

}  // namespace falcon
