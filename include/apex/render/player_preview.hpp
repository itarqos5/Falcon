#pragma once

#include "apex/assets/image.hpp"
#include "apex/render/entity_model.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace apex {

class ResourceManager;

// The player drawn in the inventory screens (the game's InventoryScreen.extractEntityInInventoryFollowsMouse): the real player model
// (`entity-models.json`) wearing the default skin, standing in a box with its body and head turned towards the mouse, drawn by the CPU
// at the exact pixel size of the box (no smoothing, as the game draws it) and handed back as an image.
class PlayerPreview {
public:
    struct View {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the box in GUI units
        int guiScale = 1;                     // pixels per GUI unit
        float scale = 30.0f;                  // GUI units per block (30 in the survival inventory, 20 in the creative one)
        float offsetY = 0.0625f;              // the model sits this much (blocks) below the middle of the box
        float mouseX = 0.0f, mouseY = 0.0f;   // the mouse, in GUI units
        double ageTicks = 0.0;                // the player's age: the arms sway a little as they do in the game
        // Cape, Jacket, Left Sleeve, Right Sleeve, Left Pant Leg, Right Pant Leg, Hat: the Skin Customization options.
        std::array<bool, 7> parts = {true, true, true, true, true, true, true};
    };

    // Null (and `error`) when the model or the skin cannot be read.
    [[nodiscard]] static std::unique_ptr<PlayerPreview> create(const ResourceManager& resources, const std::filesystem::path& gameDataDir, std::string& error);

    // The picture of the box: (x1 - x0) * guiScale by (y1 - y0) * guiScale pixels, transparent round the figure.
    [[nodiscard]] Image render(const View& view) const;

private:
    EntityPart model_;
    Image skin_;
};

}  // namespace apex
