#include "falcon/net/protocol_data.hpp"

#include <nlohmann/json.hpp>

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
std::string substituteArguments(const std::string& pattern, const std::vector<std::string>& args) {
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

std::string translated(const NbtTag& c, const std::string& key) {
    std::string pattern = translator() ? translator()(key) : key;
    if (pattern == key) {
        if (const NbtTag* fallback = field(c, "fallback"); fallback != nullptr && fallback->type == NbtTag::Type::String) pattern = fallback->text;
    }
    std::vector<std::string> args;
    if (const NbtTag* with = field(c, "with"); with != nullptr) {
        for (const NbtTag& a : with->items) args.push_back(plainOf(a));
    }
    return substituteArguments(pattern, args);
}

// The same for a component written as JSON (the text of a login disconnect).
void jsonText(const nlohmann::json& j, std::string& out, int depth) {
    if (depth > 16) return;
    if (j.is_string()) {
        out += j.get<std::string>();
    } else if (j.is_array()) {
        for (const nlohmann::json& e : j) jsonText(e, out, depth + 1);
    } else if (j.is_object()) {
        if (j.contains("text") && j["text"].is_string()) {
            out += j["text"].get<std::string>();
        } else if (j.contains("translate") && j["translate"].is_string()) {
            const std::string key = j["translate"].get<std::string>();
            std::string pattern = translator() ? translator()(key) : key;
            if (pattern == key && j.contains("fallback") && j["fallback"].is_string()) pattern = j["fallback"].get<std::string>();
            std::vector<std::string> args;
            if (j.contains("with") && j["with"].is_array()) {
                for (const nlohmann::json& a : j["with"]) {
                    std::string text;
                    jsonText(a, text, depth + 1);
                    args.push_back(text);
                }
            }
            out += substituteArguments(pattern, args);
        }
        if (j.contains("extra") && j["extra"].is_array()) {
            for (const nlohmann::json& e : j["extra"]) jsonText(e, out, depth + 1);
        }
    }
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

std::string componentJsonToPlain(const std::string& json) {
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return json;
    std::string out;
    jsonText(j, out, 0);
    return out;
}

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

// An LpVec3 as it is: the velocity of an entity (LpVec3.read): a byte that is 0 for no movement, else six bytes (three values of 15 bits that are fractions of a scale) and when
// bit 2 of the first byte is set a VarInt that makes the scale bigger.
void readLpVec3(Reader& r, double& x, double& y, double& z) {
    x = y = z = 0.0;
    const std::uint32_t lowest = static_cast<std::uint32_t>(r.u8());
    if (lowest == 0) return;
    const std::uint32_t middle = static_cast<std::uint32_t>(r.u8());
    const std::uint64_t highest = static_cast<std::uint32_t>(r.i32());
    const std::uint64_t buffer = (highest << 16) | (middle << 8) | lowest;
    std::uint64_t scale = lowest & 3u;
    if ((lowest & 4u) == 4u) scale |= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(r.varint())) & 0xFFFFFFFFull) << 2;
    const auto unpack = [](std::uint64_t value) { return std::min(static_cast<double>(value & 32767ull), 32766.0) * 2.0 / 32766.0 - 1.0; };
    x = unpack(buffer >> 3) * static_cast<double>(scale);
    y = unpack(buffer >> 18) * static_cast<double>(scale);
    z = unpack(buffer >> 33) * static_cast<double>(scale);
}

// LpVec3.write, the other way of readLpVec3: three numbers in 15 bits each, as fractions of a scale (the nearest whole number above the biggest of them).
void writeLpVec3(Writer& w, double x, double y, double z) {
    const auto clean = [](double v) { return std::isnan(v) ? 0.0 : std::clamp(v, -1.7179869183E10, 1.7179869183E10); };
    x = clean(x);
    y = clean(y);
    z = clean(z);
    const double biggest = std::max({std::abs(x), std::abs(y), std::abs(z)});
    if (biggest < 3.051944088384301E-5) {
        w.u8(0);
        return;
    }
    const std::uint64_t scale = static_cast<std::uint64_t>(std::ceil(biggest));
    const bool more = (scale & 3ull) != scale;
    const std::uint64_t scaleBits = more ? ((scale & 3ull) | 4ull) : scale;
    const auto pack = [scale](double v) { return static_cast<std::uint64_t>(std::llround((v / static_cast<double>(scale) * 0.5 + 0.5) * 32766.0)); };
    const std::uint64_t buffer = scaleBits | (pack(x) << 3) | (pack(y) << 18) | (pack(z) << 33);
    w.u8(static_cast<std::uint8_t>(buffer & 0xFF));
    w.u8(static_cast<std::uint8_t>((buffer >> 8) & 0xFF));
    w.i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(buffer >> 16)));
    if (more) w.varint(static_cast<int>(scale >> 2));
}

ComponentKind componentKindOf(const std::string& rawName) {
    static const std::unordered_map<std::string, ComponentKind> kTable = {
        {"custom_data", ComponentKind::Nbt}, {"max_stack_size", ComponentKind::VarInt}, {"max_damage", ComponentKind::VarInt}, {"damage", ComponentKind::VarInt},
        {"unbreakable", ComponentKind::Unit}, {"use_effects", ComponentKind::UseCooldown /* placeholder, replaced below */},
        {"custom_name", ComponentKind::CustomName}, {"minimum_attack_charge", ComponentKind::Float}, {"damage_type", ComponentKind::VarInt}, {"item_name", ComponentKind::ItemName},
        {"item_model", ComponentKind::Identifier}, {"lore", ComponentKind::Lore}, {"rarity", ComponentKind::VarInt}, {"enchantments", ComponentKind::Enchantments},
        {"attribute_modifiers", ComponentKind::AttributeModifiers}, {"custom_model_data", ComponentKind::CustomModelData}, {"tooltip_display", ComponentKind::TooltipDisplay},
        {"repair_cost", ComponentKind::VarInt}, {"enchantment_glint_override", ComponentKind::Bool}, {"food", ComponentKind::Food}, {"consumable", ComponentKind::Consumable},
        {"use_remainder", ComponentKind::ItemTemplate}, {"use_cooldown", ComponentKind::UseCooldown}, {"damage_resistant", ComponentKind::HolderSet}, {"tool", ComponentKind::Tool},
        {"weapon", ComponentKind::Weapon}, {"attack_range", ComponentKind::AttackRange}, {"enchantable", ComponentKind::VarInt}, {"equippable", ComponentKind::Equippable},
        {"repairable", ComponentKind::HolderSet}, {"glider", ComponentKind::Unit}, {"tooltip_style", ComponentKind::Identifier}, {"death_protection", ComponentKind::ConsumeEffects},
        {"piercing_weapon", ComponentKind::PiercingWeapon}, {"attack_animation", ComponentKind::SwingAnimation}, {"interact_animation", ComponentKind::SwingAnimation},
        {"additional_trade_cost", ComponentKind::VarInt}, {"villager_food", ComponentKind::VarInt}, {"stored_enchantments", ComponentKind::Enchantments}, {"dye", ComponentKind::VarInt},
        {"dyed_color", ComponentKind::DyedColor}, {"map_id", ComponentKind::VarInt}, {"map_post_processing", ComponentKind::VarInt}, {"charged_projectiles", ComponentKind::ItemTemplateList},
        {"bundle_contents", ComponentKind::ItemTemplateList}, {"potion_contents", ComponentKind::PotionContents}, {"potion_duration_scale", ComponentKind::Float},
        {"suspicious_stew_effects", ComponentKind::SuspiciousStew}, {"writable_book_content", ComponentKind::WritableBook}, {"written_book_content", ComponentKind::WrittenBook},
        {"entity_data", ComponentKind::TypedEntity}, {"bucket_entity_data", ComponentKind::Nbt}, {"block_entity_data", ComponentKind::TypedEntity},
        {"ominous_bottle_amplifier", ComponentKind::VarInt}, {"lodestone_tracker", ComponentKind::LodestoneTracker}, {"profile", ComponentKind::Profile},
        {"note_block_sound", ComponentKind::Identifier}, {"banner_patterns", ComponentKind::BannerPatterns}, {"base_color", ComponentKind::VarInt},
        {"pot_decorations", ComponentKind::PotDecorations}, {"container", ComponentKind::ItemTemplateList}, {"block_state", ComponentKind::BlockState},
        {"break_sound", ComponentKind::SoundHolder}, {"waxed", ComponentKind::Unit},
    };
    std::string name = rawName;
    if (name.compare(0, 10, "minecraft:") == 0) name.erase(0, 10);
    if (name == "use_effects") return ComponentKind::Unknown;  // (replaced below by its own layout)
    const auto it = kTable.find(name);
    return it == kTable.end() ? ComponentKind::Unknown : it->second;
}

namespace {

bool readComponentValueDepth(Reader& r, int type, const ComponentIds& ids, ItemStack* stack, int depth);

// A list's length, refused when it is absurd (a misread stream, or a server that does not mean it).
bool readCount(Reader& r, int maxCount, int& n) {
    n = r.varint();
    return r.ok && n >= 0 && n <= maxCount;
}

// HolderSet: 0 then a tag's name, or the number of entries plus one and the entries (by number).
bool skipHolderSet(Reader& r) {
    const int n = r.varint();
    if (!r.ok || n < 0 || n > 65537) return false;
    if (n == 0) {
        (void)r.string();
        return r.ok;
    }
    for (int i = 0; i < n - 1 && r.ok; ++i) r.varint();
    return r.ok;
}

// A sound event: 0 then its name and an optional fixed range, or its number plus one.
bool skipSoundHolder(Reader& r) {
    if (r.varint() != 0) return r.ok;
    (void)r.string();
    if (r.boolean()) r.f32();
    return r.ok;
}

// Optional<T>: a bool, then the value when it is true.
template <typename Fn>
bool optionally(Reader& r, Fn&& fn) {
    if (!r.boolean()) return r.ok;
    return fn();
}

// MobEffectInstance: the effect by number, then its details (the amplifier, the duration, three flags and, optionally, the effect that this one hides).
bool skipEffectDetails(Reader& r, int depth) {
    if (depth > 8) return false;
    r.varint();
    r.varint();
    r.boolean();
    r.boolean();
    r.boolean();
    return optionally(r, [&] { return skipEffectDetails(r, depth + 1); });
}
bool skipEffectInstance(Reader& r) {
    r.varint();
    return skipEffectDetails(r, 0) && r.ok;
}

// One ConsumeEffect: its type's number, then what that type writes.
bool skipConsumeEffect(Reader& r) {
    const int type = r.varint();
    int n = 0;
    switch (type) {
        case 0:  // apply_effects: the effects and the chance
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipEffectInstance(r)) return false;
            }
            r.f32();
            return r.ok;
        case 1: return skipHolderSet(r);  // remove_effects
        case 2: return r.ok;              // clear_all_effects
        case 3:                           // teleport_randomly: the diameter and a flag
            r.f32();
            r.boolean();
            return r.ok;
        case 4: return skipSoundHolder(r);  // play_sound
        default: return false;
    }
}

// ItemStackTemplate: the item (by number), the count and the changes to its components.
bool readItemTemplate(Reader& r, const ComponentIds& ids, int depth) {
    if (depth > 6) return false;
    r.varint();
    r.varint();
    const int added = r.varint(), removed = r.varint();
    if (!r.ok || added < 0 || removed < 0 || added > 128 || removed > 128) return false;
    for (int i = 0; i < added; ++i) {
        if (!readComponentValueDepth(r, r.varint(), ids, nullptr, depth + 1)) return false;
    }
    for (int i = 0; i < removed && r.ok; ++i) r.varint();
    return r.ok;
}

// What a Filterable is: the text, and optionally the same text as it reads to someone with the filter on.
bool skipFilterableString(Reader& r) {
    (void)r.string();
    return optionally(r, [&] {
        (void)r.string();
        return r.ok;
    });
}

bool readComponentKind(Reader& r, ComponentKind kind, const ComponentIds& ids, ItemStack* stack, int depth) {
    int n = 0;
    switch (kind) {
        case ComponentKind::Unknown: return false;
        case ComponentKind::Unit: return r.ok;
        case ComponentKind::Float: r.f32(); return r.ok;
        case ComponentKind::VarInt: r.varint(); return r.ok;
        case ComponentKind::Int4: r.i32(); return r.ok;
        case ComponentKind::Identifier: (void)r.string(); return r.ok;
        case ComponentKind::Bool: r.boolean(); return r.ok;
        case ComponentKind::Component: (void)readComponent(r); return r.ok;
        case ComponentKind::CustomName: {
            const std::string name = plainText(readComponent(r));
            if (stack != nullptr) stack->customName = name;
            return r.ok;
        }
        case ComponentKind::ItemName: (void)readComponent(r); return r.ok;
        case ComponentKind::Nbt: (void)readNetworkNbt(r); return r.ok;
        case ComponentKind::HolderSet: return skipHolderSet(r);
        case ComponentKind::SoundHolder: return skipSoundHolder(r);
        case ComponentKind::Enchantments:
            if (!readCount(r, 1024, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) {
                r.varint();  // the enchantment
                r.varint();  // its level
            }
            return r.ok;
        case ComponentKind::TooltipDisplay:
            r.boolean();  // hide everything
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) r.varint();
            return r.ok;
        case ComponentKind::Lore:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) (void)readComponent(r);
            return r.ok;
        case ComponentKind::AttributeModifiers:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) {
                r.varint();     // the attribute
                (void)r.string();  // the modifier's id
                r.f64();        // the amount
                r.varint();     // how it is added
                r.varint();     // in which slots
                const int display = r.varint();  // how the tooltip shows it: by default, hidden, or by a text of its own
                if (display == 2) (void)readComponent(r);
                else if (display != 0 && display != 1) return false;
            }
            return r.ok;
        case ComponentKind::CustomModelData: {
            for (int part = 0; part < 4; ++part) {
                if (!readCount(r, 4096, n)) return false;
                for (int i = 0; i < n && r.ok; ++i) {
                    if (part == 0) r.f32();
                    else if (part == 1) r.boolean();
                    else if (part == 2) (void)r.string();
                    else r.i32();
                }
            }
            return r.ok;
        }
        case ComponentKind::Food:
            r.varint();
            r.f32();
            r.boolean();
            return r.ok;
        case ComponentKind::Consumable:
            r.f32();
            r.varint();
            if (!skipSoundHolder(r)) return false;
            r.boolean();
            if (!readCount(r, 64, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipConsumeEffect(r)) return false;
            }
            return r.ok;
        case ComponentKind::ConsumeEffects:
            if (!readCount(r, 64, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipConsumeEffect(r)) return false;
            }
            return r.ok;
        case ComponentKind::ItemTemplate: return readItemTemplate(r, ids, depth);
        case ComponentKind::ItemTemplateList:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!readItemTemplate(r, ids, depth)) return false;
            }
            return r.ok;
        case ComponentKind::UseCooldown:
            r.f32();
            return optionally(r, [&] {
                (void)r.string();
                return r.ok;
            });
        case ComponentKind::Tool:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipHolderSet(r)) return false;
                if (!optionally(r, [&] { r.f32(); return r.ok; })) return false;
                if (!optionally(r, [&] { r.boolean(); return r.ok; })) return false;
            }
            r.f32();
            r.varint();
            r.boolean();
            return r.ok;
        case ComponentKind::Weapon:
            r.varint();
            r.f32();
            return r.ok;
        case ComponentKind::AttackRange:
            for (int i = 0; i < 6; ++i) r.f32();
            return r.ok;
        case ComponentKind::Equippable:
            r.varint();  // the slot
            if (!skipSoundHolder(r)) return false;
            if (!optionally(r, [&] { (void)r.string(); return r.ok; })) return false;  // the asset
            if (!optionally(r, [&] { (void)r.string(); return r.ok; })) return false;  // the camera overlay
            if (!optionally(r, [&] { return skipHolderSet(r); })) return false;         // who may wear it
            for (int i = 0; i < 5; ++i) r.boolean();
            return skipSoundHolder(r);
        case ComponentKind::PiercingWeapon:
            r.boolean();
            r.boolean();
            if (!optionally(r, [&] { return skipSoundHolder(r); })) return false;
            return optionally(r, [&] { return skipSoundHolder(r); });
        case ComponentKind::SwingAnimation:
            r.varint();
            r.varint();
            return r.ok;
        case ComponentKind::DyedColor: {
            const std::int32_t rgb = r.i32();
            if (stack != nullptr && r.ok) stack->tintColor = rgb & 0xFFFFFF;
            return r.ok;
        }
        case ComponentKind::PotionContents: {
            if (!optionally(r, [&] { r.varint(); return r.ok; })) return false;  // the potion
            const bool hasColor = r.boolean();
            const std::int32_t color = hasColor ? r.i32() : 0;
            if (hasColor && stack != nullptr && r.ok) stack->tintColor = color & 0xFFFFFF;
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipEffectInstance(r)) return false;
            }
            return optionally(r, [&] { (void)r.string(); return r.ok; });  // its name
        }
        case ComponentKind::SuspiciousStew:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) {
                r.varint();
                r.varint();
            }
            return r.ok;
        case ComponentKind::WritableBook:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {
                if (!skipFilterableString(r)) return false;
            }
            return r.ok;
        case ComponentKind::WrittenBook:
            if (!skipFilterableString(r)) return false;  // the title
            (void)r.string();                            // the author
            r.varint();                                  // the generation
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n; ++i) {  // the pages: a text and what a filtered reader sees instead
                (void)readComponent(r);
                if (!optionally(r, [&] { (void)readComponent(r); return r.ok; })) return false;
            }
            r.boolean();
            return r.ok;
        case ComponentKind::TypedEntity:
            r.varint();
            (void)readNetworkNbt(r);
            return r.ok;
        case ComponentKind::Profile: {
            const auto properties = [&]() {  // name, value, and an optional signature, each
                if (!readCount(r, 64, n)) return false;
                for (int i = 0; i < n && r.ok; ++i) {
                    (void)r.string();
                    (void)r.string();
                    if (r.boolean()) (void)r.string();
                }
                return r.ok;
            };
            if (r.boolean()) {  // a whole profile: id, name, properties
                r.skip(16);
                (void)r.string();
                if (!properties()) return false;
            } else {  // a part of one: a name, an id, properties
                if (!optionally(r, [&] { (void)r.string(); return r.ok; })) return false;
                if (!optionally(r, [&] { r.skip(16); return r.ok; })) return false;
                if (!properties()) return false;
            }
            for (int i = 0; i < 3; ++i) {  // the skin's own pictures: body, cape, elytra
                if (!optionally(r, [&] { (void)r.string(); return r.ok; })) return false;
            }
            return optionally(r, [&] { r.boolean(); return r.ok; });  // the arm model
        }
        case ComponentKind::BannerPatterns:
            if (!readCount(r, 64, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) {
                if (r.varint() == 0) {  // a pattern written out: its picture and its name
                    (void)r.string();
                    (void)r.string();
                }
                r.varint();  // the colour
            }
            return r.ok;
        case ComponentKind::BlockState:
            if (!readCount(r, 256, n)) return false;
            for (int i = 0; i < n && r.ok; ++i) {
                (void)r.string();
                (void)r.string();
            }
            return r.ok;
        case ComponentKind::PotDecorations:
            for (int i = 0; i < 4; ++i) {
                if (!optionally(r, [&] { return readItemTemplate(r, ids, depth); })) return false;
            }
            return r.ok;
        case ComponentKind::LodestoneTracker:
            if (!optionally(r, [&] {  // where: the dimension and a position
                    (void)r.string();
                    r.i64();
                    return r.ok;
                }))
                return false;
            r.boolean();
            return r.ok;
    }
    return false;
}

bool readComponentValueDepth(Reader& r, int type, const ComponentIds& ids, ItemStack* stack, int depth) {
    if (depth > 8) return false;
    if (type == ids.damage) {
        const int damage = r.varint();
        if (stack != nullptr) stack->damage = damage;
        return r.ok;
    }
    if (type == ids.repairCost || type == ids.maxStackSize || type == ids.maxDamage) {
        r.varint();
        return r.ok;
    }
    if (type == ids.unbreakable) return r.ok;  // (a unit: nothing is written)
    if (type == ids.customName) return readComponentKind(r, ComponentKind::CustomName, ids, stack, depth);
    if (type == ids.itemName) return readComponentKind(r, ComponentKind::ItemName, ids, stack, depth);
    if (type == ids.enchantments) return readComponentKind(r, ComponentKind::Enchantments, ids, stack, depth);
    if (type == ids.glintOverride) {
        r.boolean();
        return r.ok;
    }
    if (type == ids.tooltipDisplay) return readComponentKind(r, ComponentKind::TooltipDisplay, ids, stack, depth);
    if (type == ids.customData) return readComponentKind(r, ComponentKind::Nbt, ids, stack, depth);
    const auto kind = ids.kinds.find(type);
    if (kind == ids.kinds.end()) return false;
    return readComponentKind(r, kind->second, ids, stack, depth);
}

}  // namespace

ParticleData particleDataOf(const std::string& rawName) {
    static const std::unordered_map<std::string, ParticleData> kTable = {
        {"block", ParticleData::BlockState}, {"block_marker", ParticleData::BlockState}, {"falling_dust", ParticleData::BlockState}, {"dust_pillar", ParticleData::BlockState},
        {"block_crumble", ParticleData::BlockState}, {"geyser", ParticleData::Int}, {"geyser_plume", ParticleData::Int}, {"geyser_base", ParticleData::IntFloat},
        {"geyser_poof", ParticleData::IntFloat}, {"dragon_breath", ParticleData::Float}, {"dust", ParticleData::IntFloat}, {"dust_color_transition", ParticleData::TwoIntsFloat},
        {"effect", ParticleData::IntFloat}, {"instant_effect", ParticleData::IntFloat}, {"entity_effect", ParticleData::Int}, {"tinted_leaves", ParticleData::Int},
        {"flash", ParticleData::Int}, {"sculk_charge", ParticleData::Float}, {"shriek", ParticleData::VarInt}, {"trail", ParticleData::Trail}, {"item", ParticleData::Item},
        {"vibration", ParticleData::Vibration},
    };
    std::string name = rawName;
    if (name.compare(0, 10, "minecraft:") == 0) name.erase(0, 10);
    const auto it = kTable.find(name);
    return it == kTable.end() ? ParticleData::None : it->second;
}

bool skipParticle(Reader& r, const std::unordered_map<int, ParticleData>& kinds, const ComponentIds& ids) {
    const int type = r.varint();
    const auto it = kinds.find(type);
    switch (it == kinds.end() ? ParticleData::None : it->second) {
        case ParticleData::None: break;
        case ParticleData::BlockState:
        case ParticleData::VarInt: r.varint(); break;
        case ParticleData::Int: r.i32(); break;
        case ParticleData::IntFloat:
            r.i32();
            r.f32();
            break;
        case ParticleData::Float: r.f32(); break;
        case ParticleData::TwoIntsFloat:
            r.i32();
            r.i32();
            r.f32();
            break;
        case ParticleData::Trail:
            r.f64();
            r.f64();
            r.f64();
            r.i32();
            r.varint();
            break;
        case ParticleData::Item: return readItemTemplate(r, ids, 0);
        case ParticleData::Vibration: {
            const int source = r.varint();  // where it goes: to a block, or to an entity
            if (source == 0) {
                r.i64();
            } else if (source == 1) {
                r.varint();
                r.f32();
            } else {
                return false;
            }
            r.varint();
            break;
        }
    }
    return r.ok;
}

bool skipResolvableProfile(Reader& r) { return readComponentKind(r, ComponentKind::Profile, ComponentIds{}, nullptr, 0); }

// The value of a component of this type (the type's number has been read): kept in the stack where the stack has a place for it (the damage, a name, a colour), stepped over otherwise.
// False for a type whose layout is not known here: nothing more of the stack (or of the list it is in) can be read.
bool readComponentValue(Reader& r, int type, const ComponentIds& ids, ItemStack* stack) { return readComponentValueDepth(r, type, ids, stack, 0); }

// An ItemCost: the item, how many, and a predicate on its components (a list of components that must be there, which is empty for nearly every trade).
bool readItemCost(Reader& r, const ComponentIds& ids, ItemStack& out) {
    const int item = r.varint();
    const int count = r.varint();
    const int predicates = r.varint();
    if (!r.ok || count <= 0 || predicates < 0 || predicates > 64) return false;
    out = ItemStack(static_cast<Item>(item), count);
    for (int i = 0; i < predicates; ++i) {
        if (!readComponentValue(r, r.varint(), ids, nullptr)) return false;
    }
    return r.ok;
}

bool readTradeOffer(Reader& r, const ComponentIds& ids, TradeOffer& out) {
    if (!readItemCost(r, ids, out.costA)) return false;
    bool known = true;
    out.result = readItemStack(r, ids, known);
    if (!known || !r.ok) return false;
    if (r.boolean() && !readItemCost(r, ids, out.costB)) return false;
    out.outOfStock = r.boolean();
    out.uses = r.i32();
    out.maxUses = r.i32();
    out.xp = r.i32();
    out.specialPriceDiff = r.i32();
    out.priceMultiplier = r.f32();
    out.demand = r.i32();
    return r.ok;
}

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
        if (!readComponentValue(r, r.varint(), ids, &stack)) {
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
