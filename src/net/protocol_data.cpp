#include "falcon/net/protocol_data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace falcon::net {

namespace {

NbtTag readPayload(Reader& r, int type, int depth);

std::string readShortString(Reader& r) {
    const int n = r.u16();
    if (static_cast<std::size_t>(n) > r.left()) {
        r.ok = false;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(r.here()), static_cast<std::size_t>(n));
    r.skip(static_cast<std::size_t>(n));
    return s;
}

NbtTag readPayload(Reader& r, int type, int depth) {
    NbtTag t;
    if (depth > 64 || !r.ok) {
        r.ok = false;
        return t;
    }
    t.type = static_cast<NbtTag::Type>(type);
    switch (type) {
        case 1: t.integer = r.i8(); break;
        case 2: t.integer = r.i16(); break;
        case 3: t.integer = r.i32(); break;
        case 4: t.integer = r.i64(); break;
        case 5: t.real = r.f32(); break;
        case 6: t.real = r.f64(); break;
        case 7: {
            const int n = r.i32();
            for (int i = 0; i < n && r.ok; ++i) t.numbers.push_back(r.i8());
            break;
        }
        case 8: t.text = readShortString(r); break;
        case 9: {
            const int elementType = r.u8();
            const int n = r.i32();
            for (int i = 0; i < n && r.ok; ++i) t.items.push_back(readPayload(r, elementType, depth + 1));
            break;
        }
        case 10:
            for (;;) {
                const int childType = r.u8();
                if (childType == 0 || !r.ok) break;
                std::string name = readShortString(r);
                t.fields.emplace_back(std::move(name), readPayload(r, childType, depth + 1));
            }
            break;
        case 11: {
            const int n = r.i32();
            for (int i = 0; i < n && r.ok; ++i) t.numbers.push_back(r.i32());
            break;
        }
        case 12: {
            const int n = r.i32();
            for (int i = 0; i < n && r.ok; ++i) t.numbers.push_back(r.i64());
            break;
        }
        default: r.ok = false; break;
    }
    return t;
}

std::function<std::string(const std::string&)>& translator() {
    static std::function<std::string(const std::string&)> fn;
    return fn;
}

const NbtTag* field(const NbtTag& t, const char* name);
void flatten(const NbtTag& c, TextSpan style, StyledText& out, int depth);

std::string plainOf(const NbtTag& c) {
    StyledText parts;
    flatten(c, TextSpan{}, parts, 0);
    std::string s;
    for (const TextSpan& span : parts) s += span.text;
    return s;
}

// A translated text with its arguments: %s in turn, %1$s by number.
std::string translated(const NbtTag& c, const std::string& key) {
    std::string pattern = translator() ? translator()(key) : key;
    if (pattern == key) {
        if (const NbtTag* fallback = field(c, "fallback"); fallback != nullptr && fallback->type == NbtTag::Type::String) pattern = fallback->text;
    }
    std::vector<std::string> args;
    if (const NbtTag* with = field(c, "with"); with != nullptr) {
        for (const NbtTag& a : with->items) args.push_back(plainOf(a));
    }
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) { out += pattern[i]; continue; }
        if (pattern[i + 1] == '%') { out += '%'; ++i; }
        else if (pattern[i + 1] == 's') { if (next < args.size()) out += args[next++]; ++i; }
        else if (std::isdigit(static_cast<unsigned char>(pattern[i + 1])) && i + 3 < pattern.size() && pattern[i + 2] == '$' && pattern[i + 3] == 's') {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '1');
            if (n < args.size()) out += args[n];
            i += 3;
        } else out += pattern[i];
    }
    return out;
}

const NbtTag* field(const NbtTag& t, const char* name) {
    for (const auto& [key, value] : t.fields) {
        if (key == name) return &value;
    }
    return nullptr;
}

std::uint32_t namedColor(const std::string& name, bool& known) {
    static const struct { const char* name; std::uint32_t rgb; } kColors[] = {
        {"black", 0x000000}, {"dark_blue", 0x0000AA}, {"dark_green", 0x00AA00}, {"dark_aqua", 0x00AAAA}, {"dark_red", 0xAA0000}, {"dark_purple", 0xAA00AA},
        {"gold", 0xFFAA00}, {"gray", 0xAAAAAA}, {"dark_gray", 0x555555}, {"blue", 0x5555FF}, {"green", 0x55FF55}, {"aqua", 0x55FFFF}, {"red", 0xFF5555},
        {"light_purple", 0xFF55FF}, {"yellow", 0xFFFF55}, {"white", 0xFFFFFF}};
    known = true;
    for (const auto& c : kColors) {
        if (name == c.name) return c.rgb;
    }
    if (name.size() == 7 && name[0] == '#') return static_cast<std::uint32_t>(std::strtoul(name.c_str() + 1, nullptr, 16));
    known = false;
    return 0;
}

void flatten(const NbtTag& c, TextSpan style, StyledText& out, int depth) {
    if (depth > 16) return;
    if (c.type == NbtTag::Type::String) {
        style.text = c.text;
        if (!style.text.empty()) out.push_back(style);
        return;
    }
    if (c.type == NbtTag::Type::List) {
        for (const NbtTag& item : c.items) flatten(item, style, out, depth + 1);
        return;
    }
    if (c.type != NbtTag::Type::Compound) return;
    if (const NbtTag* color = field(c, "color"); color != nullptr && color->type == NbtTag::Type::String) {
        bool known = false;
        const std::uint32_t rgb = namedColor(color->text, known);
        if (known) {
            style.rgb = rgb;
            style.hasColor = true;
        }
    }
    const auto flag = [&](const char* name, bool& into) {
        if (const NbtTag* f = field(c, name); f != nullptr && f->isInteger()) into = f->integer != 0;
    };
    flag("bold", style.bold);
    flag("italic", style.italic);
    flag("underlined", style.underlined);
    flag("strikethrough", style.strikethrough);
    TextSpan own = style;
    if (const NbtTag* text = field(c, "text"); text != nullptr && text->type == NbtTag::Type::String) own.text = text->text;
    else if (const NbtTag* tr = field(c, "translate"); tr != nullptr && tr->type == NbtTag::Type::String) own.text = translated(c, tr->text);
    if (!own.text.empty()) out.push_back(own);
    if (const NbtTag* extra = field(c, "extra"); extra != nullptr) {
        for (const NbtTag& item : extra->items) flatten(item, style, out, depth + 1);
    }
}

// One PalettedContainer: the bits of an entry, the palette (none for a single value or a direct one), and the packed values.
bool readContainer(Reader& r, int size, int maxIndirectBits, int directBits, bool lengthPrefixed, std::vector<std::uint16_t>* out) {
    const int bits = r.u8();
    std::vector<int> palette;
    int effective = bits;
    if (bits == 0) {
        const int value = r.varint();
        if (lengthPrefixed) r.varint();
        if (out != nullptr) out->assign(static_cast<std::size_t>(size), static_cast<std::uint16_t>(value));
        return r.ok;
    }
    if (bits <= maxIndirectBits) {
        const int n = r.varint();
        if (n < 0 || n > 4096) return false;
        for (int i = 0; i < n; ++i) palette.push_back(r.varint());
    } else {
        effective = directBits;
    }
    const int perLong = 64 / effective;
    const int longs = (size + perLong - 1) / perLong;
    if (lengthPrefixed && r.varint() != longs) return false;
    if (static_cast<std::size_t>(longs) * 8 > r.left()) return false;
    std::vector<std::uint64_t> data(static_cast<std::size_t>(longs));
    for (auto& v : data) v = static_cast<std::uint64_t>(r.i64());
    if (out != nullptr) {
        out->resize(static_cast<std::size_t>(size));
        const std::uint64_t mask = (1ull << effective) - 1;
        for (int i = 0; i < size; ++i) {
            const std::uint64_t v = (data[static_cast<std::size_t>(i / perLong)] >> ((i % perLong) * effective)) & mask;
            (*out)[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(palette.empty() ? v : (v < palette.size() ? palette[v] : 0));
        }
    }
    return r.ok;
}

bool readSections(Reader& data, int stateCount, bool lengthPrefixed, Chunk& chunk) {
    int directBits = 1;
    while ((1 << directBits) < stateCount) ++directBits;
    for (int s = 0; s < kSectionCount; ++s) {
        data.i16();  // the number of blocks that are not air
        data.i16();  // and (since 26.x) of fluids
        std::vector<std::uint16_t> blocks;
        if (!readContainer(data, 4096, 8, directBits, lengthPrefixed, &blocks)) return false;
        if (!readContainer(data, 64, 3, 7, lengthPrefixed, nullptr)) return false;  // the biomes are not used
        SectionBuilder builder;
        bool any = false;
        for (int i = 0; i < 4096; ++i) {
            const Block b = static_cast<Block>(blocks[static_cast<std::size_t>(i)]);
            if (b != Block::Air) any = true;
            builder.raw()[static_cast<std::size_t>(i)] = b;
        }
        if (any) chunk.sections[static_cast<std::size_t>(s)] = builder.build();
    }
    return data.ok;
}

}  // namespace

void setComponentTranslator(std::function<std::string(const std::string&)> translate) { translator() = std::move(translate); }

NbtTag readNetworkNbt(Reader& r) {
    const int type = r.u8();
    if (type == 0) return NbtTag{};
    return readPayload(r, type, 0);
}

StyledText componentToStyled(const NbtTag& component) {
    StyledText out;
    flatten(component, TextSpan{}, out, 0);
    return out;
}

StyledText readComponent(Reader& r) { return componentToStyled(readNetworkNbt(r)); }

ItemStack readItemStack(Reader& r, const ComponentIds& ids, bool& known) {
    ItemStack stack;
    const int count = r.varint();
    if (!r.ok || count <= 0) return stack;
    const int item = r.varint();
    stack.item = static_cast<Item>(item);
    stack.count = count;
    const int added = r.varint();
    const int removed = r.varint();
    for (int i = 0; i < added && r.ok; ++i) {
        const int type = r.varint();
        if (type == ids.damage) stack.damage = r.varint();
        else if (type == ids.repairCost || type == ids.maxStackSize || type == ids.maxDamage) r.varint();
        else if (type == ids.unbreakable) {}  // (a unit: nothing is written)
        else if (type == ids.customName) stack.customName = plainText(readComponent(r));
        else if (type == ids.itemName) (void)readComponent(r);
        else if (type == ids.enchantments) {
            const int n = r.varint();
            for (int e = 0; e < n && r.ok; ++e) {
                r.varint();  // the enchantment
                r.varint();  // its level
            }
        } else if (type == ids.glintOverride) r.boolean();
        else if (type == ids.tooltipDisplay) {
            r.boolean();  // hide everything
            const int n = r.varint();
            for (int h = 0; h < n && r.ok; ++h) r.varint();
        } else if (type == ids.customData) (void)readNetworkNbt(r);
        else {
            known = false;
            return stack;
        }
    }
    for (int i = 0; i < removed && r.ok; ++i) r.varint();
    if (!r.ok) known = false;
    return stack;
}

void writeItemStack(Writer& w, const ItemStack& stack) {
    if (stack.isEmpty()) {
        w.varint(0);
        return;
    }
    w.varint(stack.count);
    w.varint(static_cast<int>(stack.item));
    w.varint(0);
    w.varint(0);
}

std::shared_ptr<Chunk> decodeChunk(Reader& r, int stateCount, std::string& error) {
    const int heightmaps = r.varint();
    for (int i = 0; i < heightmaps && r.ok; ++i) {
        r.varint();                                   // which heightmap
        const int n = r.varint();
        if (n < 0) { r.ok = false; break; }
        r.skip(static_cast<std::size_t>(n) * 8);
    }
    const int size = r.varint();
    if (!r.ok || size < 0 || static_cast<std::size_t>(size) > r.left()) {
        error = "the chunk data is cut short";
        return nullptr;
    }
    // Since 1.21.5 the packed values of a palette have no length in front of them; try that first and the older form if it does not fit.
    for (const bool lengthPrefixed : {false, true}) {
        Reader data(r.here(), static_cast<std::size_t>(size));
        auto chunk = std::make_shared<Chunk>();
        if (readSections(data, stateCount, lengthPrefixed, *chunk) && data.left() == 0) {
            r.skip(static_cast<std::size_t>(size));
            return chunk;
        }
    }
    error = "the sections of a chunk do not read";
    return nullptr;
}

}  // namespace falcon::net
