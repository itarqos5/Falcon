#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/block_models.hpp"

#include <unordered_map>

namespace falcon {

// The small isometric pictures of blocks that menus and the hotbar show, made from the same baked models the world
// is drawn with (so any block has one, stairs and fences included): the model is turned like the game's "gui" display
// transform (30 degrees tilt, 225 degrees turn, 0.625 scale) and rasterised on the CPU. Pictures are made the first
// time they are asked for and kept in one texture atlas.
class BlockIcons {
public:
    static constexpr int kSize = 32;   // pixels per icon (drawn 16 GUI units wide)
    static constexpr int kColumns = 32;
    static constexpr int kRows = 16;   // room for 512 icons; the oldest are reused after that

    explicit BlockIcons(BlockModels& models);

    struct Slot {
        float u = 0.0f;  // top-left of the icon in the atlas texture, in pixels
        float v = 0.0f;
    };
    // Where the icon of a block state is (made now if it is new). Needs a GL context.
    [[nodiscard]] Slot slot(Block block);
    [[nodiscard]] const Texture2D& texture() const { return atlas_; }

private:
    BlockModels& models_;
    Texture2D atlas_;
    std::unordered_map<std::uint16_t, int> cell_;
    int next_ = 0;
};

// The icon of a block as a picture (no GL needed): `size` x `size`, transparent around the block.
[[nodiscard]] Image renderBlockIcon(const BlockModels& models, Block block, int size);

}  // namespace falcon
