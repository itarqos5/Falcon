#include "falcon/text/font.hpp"

#include "falcon/assets/zip_archive.hpp"
#include "falcon/util/text_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace falcon {
using nlohmann::json;

// Builds a FontData (CPU only) from a font definition.
class FontLoader {
public:
    FontLoader(const ResourceManager& resources, FontData& font) : resources_(resources), font_(font) {}

    void loadDefinition(const std::string& name, int depth) {
        if (depth > 8) throw std::runtime_error("font reference nesting is too deep at " + name);
        const ResourceLocation loc("minecraft", "font/" + name + ".json");
        const auto text = resources_.readText(loc);
        if (!text) throw std::runtime_error("missing font definition " + loc.toString());
        const json def = json::parse(*text, nullptr, /*allow_exceptions=*/false);
        if (!def.is_object() || !def.contains("providers")) {
            throw std::runtime_error("malformed font definition " + loc.toString());
        }

        for (const json& provider : def["providers"]) {
            if (!filterAllows(provider)) continue;
            const std::string type = provider.value("type", "");
            if (type == "reference") {
                loadDefinition(ResourceLocation::parse(provider.at("id").get<std::string>()).path,
                               depth + 1);
            } else if (type == "space") {
                loadSpace(provider);
            } else if (type == "bitmap") {
                loadBitmap(provider);
            } else if (type == "unihex") {
                loadUnihex(provider);
            } else {
                font_.warnings.push_back("font provider '" + type + "' in " + loc.toString() +
                                         " is not supported yet");
            }
        }
    }

private:
    // Font options are all off by default ("uniform": false, "jp": false).
    static bool filterAllows(const json& provider) {
        if (!provider.contains("filter")) return true;
        for (const auto& [option, wanted] : provider["filter"].items()) {
            (void)option;
            if (wanted.is_boolean() && wanted.get<bool>()) return false;
        }
        return true;
    }

    void addGlyph(char32_t cp, const Glyph& glyph, int image = -1) {
        font_.glyphs.emplace(cp, FontData::GlyphSpec{glyph, image});
    }

    void loadSpace(const json& provider) {
        for (const auto& [chars, advance] : provider.at("advances").items()) {
            const auto cps = decodeUtf8(chars);
            if (cps.empty()) continue;
            Glyph g;
            g.advance = advance.get<int>();
            addGlyph(cps.front(), g);
        }
    }

    // Keeps the Unifont .zip (nothing is decoded until a character needs it) and its size overrides.
    void loadUnihex(const json& provider) {
        const ResourceLocation file = ResourceLocation::parse(provider.at("hex_file").get<std::string>());
        auto bytes = resources_.readBytes(file);
        if (!bytes) {
            font_.warnings.push_back("missing unihex file " + file.toString());
            return;
        }
        FontData::UnihexProvider spec;
        spec.zip = std::make_shared<const std::vector<unsigned char>>(std::move(*bytes));
        if (provider.contains("size_overrides")) {
            for (const json& o : provider["size_overrides"]) {
                const auto from = decodeUtf8(o.at("from").get<std::string>());
                const auto to = decodeUtf8(o.at("to").get<std::string>());
                if (from.empty() || to.empty()) continue;
                spec.overrides.push_back({from.front(), to.front(), o.at("left").get<int>(), o.at("right").get<int>()});
            }
        }
        font_.unihex.push_back(std::move(spec));
    }

    void loadBitmap(const json& provider) {
        const ResourceLocation file = ResourceLocation::parse(provider.at("file").get<std::string>());
        const ResourceLocation texLoc(file.ns, "textures/" + file.path);
        const auto bytes = resources_.readBytes(texLoc);
        if (!bytes) throw std::runtime_error("missing font texture " + texLoc.toString());
        auto decoded = decodePng(*bytes);
        if (!decoded) throw std::runtime_error("cannot decode font texture " + texLoc.toString());

        const int imageIndex = static_cast<int>(font_.images.size());
        font_.images.push_back(std::move(*decoded));
        const Image* image = &font_.images.back();  // read below before anything else is pushed

        const int ascent = provider.at("ascent").get<int>();
        const int height = provider.value("height", 8);

        std::vector<std::vector<char32_t>> rows;
        for (const json& row : provider.at("chars")) rows.push_back(decodeUtf8(row.get<std::string>()));
        if (rows.empty() || rows.front().empty()) return;

        const int columns = static_cast<int>(rows.front().size());
        const int cellW = image->width / columns;
        const int cellH = image->height / static_cast<int>(rows.size());
        if (cellW <= 0 || cellH <= 0) return;
        const float scale = static_cast<float>(height) / static_cast<float>(cellH);

        for (std::size_t r = 0; r < rows.size(); ++r) {
            for (std::size_t c = 0; c < rows[r].size() && static_cast<int>(c) < columns; ++c) {
                const char32_t cp = rows[r][c];
                if (cp == 0) continue;  // padding cell

                const int cellX = static_cast<int>(c) * cellW;
                const int cellY = static_cast<int>(r) * cellH;

                // Width = rightmost column that has any non-transparent pixel.
                int usedWidth = 0;
                for (int x = cellW - 1; x >= 0 && usedWidth == 0; --x) {
                    for (int y = 0; y < cellH; ++y) {
                        if (image->alphaAt(cellX + x, cellY + y) != 0) {
                            usedWidth = x + 1;
                            break;
                        }
                    }
                }

                Glyph g;
                g.u0 = static_cast<float>(cellX) / static_cast<float>(image->width);
                g.v0 = static_cast<float>(cellY) / static_cast<float>(image->height);
                g.u1 = static_cast<float>(cellX + cellW) / static_cast<float>(image->width);
                g.v1 = static_cast<float>(cellY + cellH) / static_cast<float>(image->height);
                g.x0 = 0.0f;
                g.y0 = 7.0f - static_cast<float>(ascent);  // glyph top sits `ascent` above the baseline
                g.x1 = static_cast<float>(cellW) * scale;
                g.y1 = g.y0 + static_cast<float>(cellH) * scale;
                g.advance = static_cast<int>(0.5f + static_cast<float>(usedWidth) * scale) + 1;
                addGlyph(cp, g, imageIndex);  // first provider that defines a glyph wins
            }
        }
    }

    const ResourceManager& resources_;
    FontData& font_;
};

// ---- Unifont glyphs, decoded on first use -------------------------------------------------------

// The game's unihex provider: each line of a .hex file is "<code point>:<bitmap>", the bitmap being
// 16 rows of 1-4 bytes (8, 16, 24 or 32 pixels wide). A glyph is cropped to the columns it uses (or
// the size override's), drawn at half scale (one source pixel = half a GUI pixel), and advances
// width / 2 + 1.
struct UnihexAtlas {
    struct Bitmap {
        std::uint32_t offset = 0;
        std::uint8_t bytesPerRow = 0;
    };
    struct Source {
        FontData::UnihexProvider provider;
        bool parsed = false;
        std::unordered_map<char32_t, Bitmap> index;
        std::vector<unsigned char> blob;
    };

    static constexpr int kPageSize = 1024;
    static constexpr int kCellWidth = 32;  // the widest glyph
    static constexpr int kCellHeight = 16;
    static constexpr int kCellsPerRow = kPageSize / kCellWidth;
    static constexpr int kCellsPerPage = kCellsPerRow * (kPageSize / kCellHeight);

    std::vector<Source> sources;
    std::vector<std::unique_ptr<Texture2D>> pages;
    int nextCell = 0;
    std::unordered_map<char32_t, Glyph> baked;

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    void parse(Source& source) {
        source.parsed = true;
        auto zip = ZipArchive::fromBytes(*source.provider.zip);
        if (!zip) return;
        for (const std::string& name : zip->entryNames()) {
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".hex") != 0) continue;
            const auto text = zip->read(name);
            if (!text) continue;
            const char* p = reinterpret_cast<const char*>(text->data());
            const char* const end = p + text->size();
            while (p < end) {
                const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
                if (lineEnd == nullptr) lineEnd = end;
                const char* q = p;
                std::uint32_t codePoint = 0;
                while (q < lineEnd && hexValue(*q) >= 0) codePoint = codePoint * 16 + static_cast<std::uint32_t>(hexValue(*q++));
                if (q < lineEnd && *q == ':' && q > p) {
                    ++q;
                    const std::size_t digits = static_cast<std::size_t>(lineEnd - q) - (lineEnd > q && lineEnd[-1] == '\r' ? 1 : 0);
                    const std::size_t bytes = digits / 2;
                    if (digits % 2 == 0 && bytes % 16 == 0 && bytes >= 16 && bytes <= 64) {
                        Bitmap bitmap;
                        bitmap.offset = static_cast<std::uint32_t>(source.blob.size());
                        bitmap.bytesPerRow = static_cast<std::uint8_t>(bytes / 16);
                        for (std::size_t i = 0; i < bytes; ++i) {
                            source.blob.push_back(static_cast<unsigned char>(hexValue(q[2 * i]) * 16 + hexValue(q[2 * i + 1])));
                        }
                        source.index.emplace(static_cast<char32_t>(codePoint), bitmap);
                    }
                }
                p = lineEnd + 1;
            }
        }
    }

    Glyph makeGlyph(const Source& source, char32_t codePoint, const Bitmap& bitmap) {
        const unsigned char* data = source.blob.data() + bitmap.offset;
        const int bytesPerRow = bitmap.bytesPerRow;
        const int bits = bytesPerRow * 8;
        const auto pixel = [&](int row, int column) {
            if (column < 0 || column >= bits) return false;
            return ((data[row * bytesPerRow + column / 8] >> (7 - column % 8)) & 1) != 0;
        };

        int left = bits;
        int right = -1;
        bool overridden = false;
        for (const FontData::UnihexOverride& o : source.provider.overrides) {
            if (codePoint >= o.from && codePoint <= o.to) {
                left = o.left;
                right = o.right;
                overridden = true;
                break;
            }
        }
        if (!overridden) {
            for (int row = 0; row < 16; ++row) {
                for (int column = 0; column < bits; ++column) {
                    if (!pixel(row, column)) continue;
                    left = std::min(left, column);
                    right = std::max(right, column);
                }
            }
        }

        Glyph glyph;
        if (right < left) {  // nothing to draw (a space): just the advance
            glyph.advance = bits / 2 + 1;
            return glyph;
        }
        const int width = std::min(right - left + 1, kCellWidth);
        Image image;
        image.width = width;
        image.height = kCellHeight;
        image.rgba.assign(static_cast<std::size_t>(width) * kCellHeight * 4, 0);
        for (int row = 0; row < 16; ++row) {
            for (int column = 0; column < width; ++column) {
                if (!pixel(row, left + column)) continue;
                unsigned char* texel = &image.rgba[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                                    static_cast<std::size_t>(column)) * 4];
                texel[0] = texel[1] = texel[2] = texel[3] = 255;
            }
        }

        if (pages.empty() || nextCell >= kCellsPerPage) {
            pages.push_back(std::make_unique<Texture2D>(Texture2D::empty(kPageSize, kPageSize, FilterMode::Nearest)));
            nextCell = 0;
        }
        const int originX = (nextCell % kCellsPerRow) * kCellWidth;
        const int originY = (nextCell / kCellsPerRow) * kCellHeight;
        ++nextCell;
        pages.back()->upload(originX, originY, image);

        const auto size = static_cast<float>(kPageSize);
        glyph.texture = pages.back().get();
        glyph.u0 = static_cast<float>(originX) / size;
        glyph.v0 = static_cast<float>(originY) / size;
        glyph.u1 = static_cast<float>(originX + width) / size;
        glyph.v1 = static_cast<float>(originY + kCellHeight) / size;
        glyph.x0 = 0.0f;
        glyph.y0 = 0.0f;
        glyph.x1 = static_cast<float>(width) * 0.5f;
        glyph.y1 = 8.0f;
        glyph.advance = width / 2 + 1;
        return glyph;
    }

    // The glyph for a character, or null if no provider has it.
    const Glyph* bake(char32_t codePoint) {
        if (const auto it = baked.find(codePoint); it != baked.end()) return &it->second;
        for (Source& source : sources) {
            if (!source.parsed) parse(source);
            const auto it = source.index.find(codePoint);
            if (it == source.index.end()) continue;
            return &baked.emplace(codePoint, makeGlyph(source, codePoint, it->second)).first->second;
        }
        return nullptr;
    }
};

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

FontData Font::prepare(const ResourceManager& resources, const std::string& name) {
    FontData data;
    FontLoader(resources, data).loadDefinition(name, 0);
    return data;
}

Font Font::fromData(FontData data) {
    Font font;
    font.warnings_ = std::move(data.warnings);
    font.textures_.reserve(data.images.size());
    for (const Image& image : data.images) {
        font.textures_.push_back(std::make_unique<Texture2D>(image, FilterMode::Nearest));
    }
    font.glyphs_.reserve(data.glyphs.size());
    for (auto& [codePoint, spec] : data.glyphs) {
        if (spec.image >= 0) spec.glyph.texture = font.textures_[static_cast<std::size_t>(spec.image)].get();
        font.glyphs_.emplace(codePoint, spec.glyph);
    }
    font.missing_.advance = 6;  // width of the game's missing-glyph box (5) plus 1
    if (!data.unihex.empty()) {
        font.unihex_ = std::make_unique<UnihexAtlas>();
        for (FontData::UnihexProvider& provider : data.unihex) {
            font.unihex_->sources.push_back({std::move(provider), false, {}, {}});
        }
    }
    return font;
}

Font Font::load(const ResourceManager& resources, const std::string& name) {
    return fromData(prepare(resources, name));
}

int Font::width(std::string_view utf8, bool bold) const {
    int total = 0;
    for (const char32_t cp : decodeUtf8(utf8)) total += glyph(cp).advance + (bold ? 1 : 0);
    return total;
}

const Glyph& Font::glyph(char32_t codePoint) const {
    if (const auto it = glyphs_.find(codePoint); it != glyphs_.end()) return it->second;
    if (unihex_) {  // not in the bitmap sheets: decode it from the Unicode font on first use
        if (const Glyph* glyph = unihex_->bake(codePoint)) return *glyph;
    }
    return missing_;
}

}  // namespace falcon
