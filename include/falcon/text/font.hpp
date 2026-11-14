#pragma once

#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/texture.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace falcon {

// One drawable glyph: a rectangle of a font texture plus its placement and advance.
struct Glyph {
    const Texture2D* texture = nullptr;  // null for blank glyphs such as the space
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // quad corners relative to the pen position
    int advance = 0;
};

// Everything a Font is made of except its GL textures, so it can be prepared on a background
// thread (no GL context needed) and turned into a Font on the thread that owns the context.
struct FontData {
    struct GlyphSpec {
        Glyph glyph;       // `texture` is filled in later from `image`
        int image = -1;    // index into `images`; -1 for glyphs without pixels
    };
    std::vector<Image> images;                      // one per bitmap provider
    std::unordered_map<char32_t, GlyphSpec> glyphs;
    std::vector<std::string> warnings;

    // The game's "unihex" provider: a .zip holding Unifont .hex files, used for every character
    // the bitmap sheets lack. Its glyphs are only decoded when something first needs one.
    struct UnihexOverride {  // size_overrides: fixed pixel columns for a range of characters
        char32_t from = 0;
        char32_t to = 0;
        int left = 0;
        int right = 15;
    };
    struct UnihexProvider {
        std::shared_ptr<const std::vector<unsigned char>> zip;
        std::vector<UnihexOverride> overrides;
    };
    std::vector<UnihexProvider> unihex;
};

struct UnihexAtlas;  // the lazily decoded Unifont glyphs and the texture pages they are drawn from

// Bitmap font built from a font definition (minecraft:font/<name>.json): resolves
// "reference" providers, applies provider filters and reads "space" and "bitmap" providers.
class Font {
public:
    static constexpr int kLineHeight = 9;

    Font();
    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    // Throws std::runtime_error if the definition or a font texture is missing.
    static Font load(const ResourceManager& resources, const std::string& name);
    // The same in two steps: parse and decode anywhere (thread-safe), then upload on the GL thread.
    static FontData prepare(const ResourceManager& resources, const std::string& name);
    static Font fromData(FontData data);

    // Text width in pixels. Bold text is one pixel wider per glyph (the glyph is drawn twice).
    [[nodiscard]] int width(std::string_view utf8, bool bold = false) const;
    [[nodiscard]] const Glyph& glyph(char32_t codePoint) const;

    // Providers that were skipped (e.g. unihex), for the startup log.
    [[nodiscard]] const std::vector<std::string>& warnings() const { return warnings_; }

private:
    friend class FontLoader;

    std::vector<std::unique_ptr<Texture2D>> textures_;
    std::unordered_map<char32_t, Glyph> glyphs_;
    Glyph missing_;
    std::vector<std::string> warnings_;
    // Characters outside the bitmap sheets come from here, created the first time they are drawn
    // (so the GL context must be current when text is measured or drawn, as it always is).
    mutable std::unique_ptr<UnihexAtlas> unihex_;
};

}  // namespace falcon
