#pragma once

// The compiler of the material system (material.cpp): reads the game's surface rules, density functions and ore veins (worldgen JSON) into the nodes the system runs.
#include "material_internal.hpp"

#include "falcon/worldgen/java_math.hpp"

namespace falcon::worldgen {
namespace material_detail {

class Compiler {
public:
    Compiler(MaterialSystem::Impl& m, std::string& error) : m_(m), error_(error) {}

    [[nodiscard]] bool compileRoot(const std::string& ruleName) {
        const std::int32_t r = rule(JsonValueRef(ruleName));
        if (r < 0) return false;
        m_.rootRule = r;
        return true;
    }

private:
    // A rule or condition is either an inline object or a string naming a registry entry.
    struct JsonValueRef {
        explicit JsonValueRef(std::string n) : name(std::move(n)) {}
        std::string name;
    };

    bool fail(const std::string& msg) {
        if (error_.empty()) error_ = msg;
        return false;
    }

    bool loadNamed(const char* registry, const std::string& id, JsonValue& out) {
        const std::filesystem::path file = m_.worldgenDir / registry / (stripNs(id) + ".json");
        std::string err;
        if (!parseJsonFile(file, out, err)) return fail(std::string(registry) + " '" + id + "': " + err);
        return true;
    }

    bool intField(const JsonValue& o, const char* key, std::int32_t& out, bool required, std::int32_t def = 0) {
        const JsonValue* v = o.find(key);
        if (!v) {
            if (required) return fail(std::string("missing field ") + key);
            out = def;
            return true;
        }
        if (!v->toInt(out)) return fail(std::string("field ") + key + " is not an integer");
        return true;
    }
    bool boolField(const JsonValue& o, const char* key, bool& out, bool required, bool def = false) {
        const JsonValue* v = o.find(key);
        if (!v) {
            if (required) return fail(std::string("missing field ") + key);
            out = def;
            return true;
        }
        if (v->kind != JsonValue::Kind::Bool) return fail(std::string("field ") + key + " is not a boolean");
        out = v->boolean;
        return true;
    }

    bool anchor(const JsonValue* v, std::int32_t& out) {
        if (!v || !v->isObject()) return fail("missing vertical anchor");
        std::int32_t n = 0;
        if (const JsonValue* a = v->find("absolute")) {
            if (!a->toInt(n)) return fail("bad anchor");
            out = n;
        } else if (const JsonValue* a2 = v->find("above_bottom")) {
            if (!a2->toInt(n)) return fail("bad anchor");
            out = m_.minY + n;
        } else if (const JsonValue* a3 = v->find("below_top")) {
            if (!a3->toInt(n)) return fail("bad anchor");
            out = m_.height - 1 + m_.minY - n;
        } else {
            return fail("unknown vertical anchor type");
        }
        return true;
    }

    bool blockState(const JsonValue* v, Block& out) {
        if (!v) return fail("missing block state");
        std::string text;
        if (v->isString()) {
            text = v->text;
        } else if (v->isObject()) {
            const JsonValue* name = v->find("Name");
            if (!name || !name->isString()) return fail("block state without Name");
            text = name->text;
            if (const JsonValue* props = v->find("Properties"); props && props->isObject() && !props->members.empty()) {
                text += '[';
                bool first = true;
                for (const auto& [k, val] : props->members) {
                    if (!first) text += ',';
                    first = false;
                    text += k;
                    text += '=';
                    text += val.kind == JsonValue::Kind::Bool ? (val.boolean ? "true" : "false") : val.text;
                }
                text += ']';
            }
        } else {
            return fail("bad block state");
        }
        const auto b = m_.blocks->parse(text);
        if (!b) return fail("unknown block state '" + text + "'");
        out = *b;
        return true;
    }

    // biome_is: "id", "#tag" or [ids/tags]
    bool addBiomeEntry(const std::string& entry, std::array<std::uint64_t, 4>& set, int depth) {
        if (!entry.empty() && entry[0] == '#') {
            if (depth > 8) return fail("biome tag nesting too deep");
            const std::filesystem::path file = m_.worldgenDir.parent_path() / "tags" / "worldgen" / "biome" / (stripNs(entry.substr(1)) + ".json");
            JsonValue tag;
            std::string err;
            if (!parseJsonFile(file, tag, err)) return fail("biome tag " + entry + ": " + err);
            const JsonValue* values = tag.find("values");
            if (!values || !values->isArray()) return fail("biome tag " + entry + " has no values");
            for (const JsonValue& v : values->items) {
                const std::string* s = v.isString() ? &v.text : nullptr;
                if (!s && v.isObject()) {
                    if (const JsonValue* id = v.find("id"); id && id->isString()) s = &id->text;
                }
                if (!s) return fail("bad entry in biome tag " + entry);
                if (!addBiomeEntry(*s, set, depth + 1)) return false;
            }
            return true;
        }
        const BiomeId id = m_.biomeRegistry->find(entry);
        if (id == kNoBiome) return fail("unknown biome '" + entry + "' in a material rule");
        set[id >> 6] |= std::uint64_t{1} << (id & 63);
        return true;
    }

    std::int32_t addCond(CondNode&& n) {
        m_.conds.push_back(std::move(n));
        return static_cast<std::int32_t>(m_.conds.size() - 1);
    }
    std::int32_t addRule(const RuleNode& n) {
        m_.rules.push_back(n);
        return static_cast<std::int32_t>(m_.rules.size() - 1);
    }

    const NormalNoise* noiseFor(const std::string& key) {
        std::string err;
        const NormalNoise* n = m_.router->randomState()->noise(key, err);
        if (!n) fail("noise " + key + ": " + err);
        return n;
    }

    // A slot for `f` that covers at least [lo, hi] (widened when the function is used by several veins).
    std::int32_t volumeSlot(const DensityFunction& f, int lo, int hi) {
        for (std::size_t i = 0; i < m_.volumeSlots.size(); ++i) {
            VolumeSlot& s = m_.volumeSlots[i];
            if (s.fn.nodeId() == f.nodeId()) {
                s.yLo = std::min(s.yLo, lo);
                s.yHi = std::max(s.yHi, hi);
                return static_cast<std::int32_t>(i);
            }
        }
        VolumeSlot s;
        s.fn = f;
        s.yLo = lo;
        s.yHi = hi;
        m_.volumeSlots.push_back(s);
        return static_cast<std::int32_t>(m_.volumeSlots.size() - 1);
    }

    // The y range outside which a vein density function is a constant <= 0: its root is `range_choice` on `minecraft:y` with a
    // constant "out of range" branch (true of the shipped copper and iron densities); anything else keeps the full height.
    void veinYRange(const std::string& ref, int& lo, int& hi) {
        JsonValue js;
        std::string err;
        if (!parseJsonFile(m_.worldgenDir / "density_function" / (stripNs(ref) + ".json"), js, err) || !js.isObject()) return;
        const JsonValue* type = js.find("type");
        const JsonValue* input = js.find("input");
        const JsonValue* out = js.find("when_out_of_range");
        const JsonValue* mn = js.find("min_inclusive");
        const JsonValue* mx = js.find("max_exclusive");
        float outside = 0.0f;
        double a = 0.0, b = 0.0;
        if (!type || !type->isString() || stripNs(type->text) != "range_choice" || !input || !input->isString() || stripNs(input->text) != "y" || !out || !out->toFloat(outside) ||
            !(outside <= 0.0f) || !mn || !mx || !mn->toDouble(a) || !mx->toDouble(b)) {
            return;
        }
        lo = std::max(lo, static_cast<int>(std::ceil(a)));
        hi = std::min(hi, static_cast<int>(std::ceil(b)) - 1);
    }

    // ---- conditions ---------------------------------------------------------------------------------------------
    std::int32_t condition(const JsonValue& js) {
        if (js.isString()) {
            const auto it = condNames_.find(js.text);
            if (it != condNames_.end()) return it->second;
            JsonValue file;
            if (!loadNamed("material_condition", js.text, file)) return -1;
            const std::int32_t c = condition(file);
            if (c >= 0) condNames_[js.text] = c;
            return c;
        }
        if (!js.isObject()) {
            fail("material condition is neither a name nor an object");
            return -1;
        }
        const JsonValue* type = js.find("type");
        if (!type || !type->isString()) {
            fail("material condition without type");
            return -1;
        }
        const std::string t = stripNs(type->text);
        CondNode n;
        if (t == "biome") {
            n.kind = CondKind::Biome;
            const JsonValue* is = js.find("biome_is");
            if (!is) {
                fail("biome condition without biome_is");
                return -1;
            }
            if (is->isString()) {
                if (!addBiomeEntry(is->text, n.biomes, 0)) return -1;
            } else if (is->isArray()) {
                for (const JsonValue& e : is->items) {
                    if (!e.isString() || !addBiomeEntry(e.text, n.biomes, 0)) {
                        fail("bad biome_is entry");
                        return -1;
                    }
                }
            } else {
                fail("bad biome_is");
                return -1;
            }
        } else if (t == "stone_depth") {
            n.kind = CondKind::StoneDepth;
            std::string surfaceType = "floor";
            if (const JsonValue* st = js.find("surface_type"); st && st->isString()) surfaceType = st->text;
            n.flagB = surfaceType == "ceiling";
            if (!intField(js, "offset", n.i0, true) || !boolField(js, "add_surface_depth", n.flagA, true) || !intField(js, "secondary_depth_range", n.i1, true)) return -1;
        } else if (t == "y_above") {
            n.kind = CondKind::YAbove;
            std::int32_t a = 0, mult = 0;
            if (!anchor(js.find("anchor"), a) || !intField(js, "surface_depth_multiplier", mult, true) || !boolField(js, "add_stone_depth", n.flagA, true)) return -1;
            n.i0 = a;
            n.i1 = mult;
        } else if (t == "water") {
            n.kind = CondKind::Water;
            if (!intField(js, "offset", n.i0, true) || !intField(js, "surface_depth_multiplier", n.i1, true) || !boolField(js, "add_stone_depth", n.flagA, true)) return -1;
        } else if (t == "vertical_gradient") {
            n.kind = CondKind::VerticalGradient;
            const JsonValue* name = js.find("random_name");
            if (!name || !name->isString() || !anchor(js.find("true_at_and_below"), n.i0) || !anchor(js.find("false_at_and_above"), n.i1)) {
                fail("bad vertical_gradient condition");
                return -1;
            }
            n.factory = m_.router->randomState()->randomFactoryAny(name->text.find(':') == std::string::npos ? "minecraft:" + name->text : name->text);
        } else if (t == "noise_threshold") {
            n.kind = CondKind::NoiseThreshold;
            const JsonValue* noise = js.find("noise");
            const JsonValue* mn = js.find("min_threshold");
            const JsonValue* mx = js.find("max_threshold");
            if (!noise || !noise->isString() || !mn || !mx || !mn->toDouble(n.d0) || !mx->toDouble(n.d1)) {
                fail("bad noise_threshold condition");
                return -1;
            }
            if (!boolField(js, "is_3d", n.flagA, false, false)) return -1;
            n.noise = noiseFor(noise->text);
            if (!n.noise) return -1;
        } else if (t == "not") {
            n.kind = CondKind::Not;
            const JsonValue* inv = js.find("invert");
            if (!inv) {
                fail("not condition without invert");
                return -1;
            }
            n.child = condition(*inv);
            if (n.child < 0) return -1;
        } else if (t == "steep") {
            n.kind = CondKind::Steep;
        } else if (t == "hole") {
            n.kind = CondKind::Hole;
        } else if (t == "above_preliminary_surface") {
            n.kind = CondKind::AbovePreliminarySurface;
        } else if (t == "temperature") {
            n.kind = CondKind::Temperature;
        } else {
            fail("unsupported material condition type " + type->text);
            return -1;
        }
        return addCond(std::move(n));
    }

    // ---- rules --------------------------------------------------------------------------------------------------
    std::int32_t rule(const JsonValueRef& ref) {
        const auto it = ruleNames_.find(ref.name);
        if (it != ruleNames_.end()) return it->second;
        JsonValue file;
        if (!loadNamed("material_rule", ref.name, file)) return -1;
        const std::int32_t r = rule(file);
        if (r >= 0) ruleNames_[ref.name] = r;
        return r;
    }

    std::int32_t rule(const JsonValue& js) {
        if (js.isString()) return rule(JsonValueRef(js.text));
        if (!js.isObject()) {
            fail("material rule is neither a name nor an object");
            return -1;
        }
        const JsonValue* type = js.find("type");
        if (!type || !type->isString()) {
            fail("material rule without type");
            return -1;
        }
        const std::string t = stripNs(type->text);
        RuleNode n;
        if (t == "sequence") {
            const JsonValue* seq = js.find("sequence");
            if (!seq || !seq->isArray()) {
                fail("sequence rule without sequence");
                return -1;
            }
            std::vector<std::int32_t> kids;
            for (const JsonValue& e : seq->items) {
                const std::int32_t k = rule(e);
                if (k < 0) return -1;
                kids.push_back(k);
            }
            n.kind = RuleKind::Sequence;
            n.a = static_cast<std::int32_t>(m_.childIndex.size());
            n.b = static_cast<std::int32_t>(kids.size());
            m_.childIndex.insert(m_.childIndex.end(), kids.begin(), kids.end());
        } else if (t == "condition") {
            const JsonValue* ifTrue = js.find("if_true");
            const JsonValue* thenRun = js.find("then_run");
            if (!ifTrue || !thenRun) {
                fail("condition rule needs if_true and then_run");
                return -1;
            }
            const std::int32_t c = condition(*ifTrue);
            if (c < 0) return -1;
            const std::int32_t r = rule(*thenRun);
            if (r < 0) return -1;
            n.kind = RuleKind::Condition;
            n.a = c;
            n.b = r;
        } else if (t == "block") {
            n.kind = RuleKind::BlockResult;
            if (!blockState(js.find("result_state"), n.block)) return -1;
        } else if (t == "bandlands") {
            n.kind = RuleKind::Bandlands;
        } else if (t == "ore_vein") {
            n.kind = RuleKind::OreVein;
            OreVein v;
            const auto fn = [&](const char* key, DensityFunction& out) {
                const JsonValue* ref = js.find(key);
                if (!ref || !ref->isString()) return fail(std::string("ore_vein without ") + key);
                out = m_.router->function(ref->text);
                if (!out.valid()) return fail("density function " + ref->text + " was not built into the router (pass collectDensityRefs() to NoiseRouter::create)");
                return true;
            };
            DensityFunction density, richness;
            if (!fn("density", density) || !fn("richness", richness) || !fn("filler_gap", v.gap)) return -1;
            int lo = m_.minY, hi = m_.minY + m_.height - 1;
            if (const JsonValue* ref = js.find("density"); ref && ref->isString()) veinYRange(ref->text, lo, hi);
            v.yLo = lo;
            v.yHi = hi;
            v.densitySlot = volumeSlot(density, lo, hi);
            v.richnessSlot = volumeSlot(richness, lo, hi);
            if (!blockState(js.find("ore_block"), v.ore) || !blockState(js.find("raw_ore_block"), v.raw) || !blockState(js.find("filler_block"), v.filler)) return -1;
            const JsonValue* chance = js.find("raw_ore_chance");
            if (!chance || !chance->toFloat(v.rawChance)) {
                fail("ore_vein without raw_ore_chance");
                return -1;
            }
            v.factory = m_.router->randomState()->randomFactoryAny("minecraft:ore");
            n.a = static_cast<std::int32_t>(m_.veins.size());
            m_.veins.push_back(std::move(v));
        } else {
            fail("unsupported material rule type " + type->text);
            return -1;
        }
        return addRule(n);
    }

    MaterialSystem::Impl& m_;
    std::string& error_;
    std::map<std::string, std::int32_t> ruleNames_, condNames_;
};

// ---- collecting the density functions of ore veins (before the router is built)
inline bool collectRefs(const std::filesystem::path& worldgenDir, const JsonValue& js, std::set<std::string>& visitedRules, std::vector<std::string>& refs, std::string& error, int depth) {
    if (depth > 64) {
        error = "material rules nest too deeply";
        return false;
    }
    if (js.isString()) {
        if (!visitedRules.insert(js.text).second) return true;
        JsonValue file;
        if (!parseJsonFile(worldgenDir / "material_rule" / (stripNs(js.text) + ".json"), file, error)) return false;
        return collectRefs(worldgenDir, file, visitedRules, refs, error, depth + 1);
    }
    if (!js.isObject()) return true;
    const JsonValue* type = js.find("type");
    if (!type || !type->isString()) return true;
    const std::string t = stripNs(type->text);
    if (t == "sequence") {
        if (const JsonValue* seq = js.find("sequence"); seq && seq->isArray()) {
            for (const JsonValue& e : seq->items) {
                if (!collectRefs(worldgenDir, e, visitedRules, refs, error, depth + 1)) return false;
            }
        }
    } else if (t == "condition") {
        if (const JsonValue* r = js.find("then_run")) return collectRefs(worldgenDir, *r, visitedRules, refs, error, depth + 1);
    } else if (t == "ore_vein") {
        for (const char* key : {"density", "richness", "filler_gap"}) {
            if (const JsonValue* ref = js.find(key); ref && ref->isString() && std::find(refs.begin(), refs.end(), ref->text) == refs.end()) refs.push_back(ref->text);
        }
    }
    return true;
}

}  // namespace material_detail
using namespace material_detail;
}  // namespace falcon::worldgen
