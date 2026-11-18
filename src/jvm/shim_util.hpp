#pragma once

// Small helpers the stand-in handlers and the reading of what a mod registered share (internal to src/jvm).
#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace falcon::jvm {

[[nodiscard]] inline bool shimClassHas(const Object* o, const char* needle) { return o != nullptr && o->shim && o->shim->className.find(needle) != std::string::npos; }

[[nodiscard]] inline bool isPoseStack(const Object* o) { return shimClassHas(o, "PoseStack") || shimClassHas(o, "Matrix3x2f") || shimClassHas(o, "MatrixStack"); }

// The player, an entity, or a position that came from one: what the client's HostState answers for.
[[nodiscard]] inline bool isPlayerLike(const Object* o) {
    return o != nullptr && o->shim && (o->shim->origin == "playerPos" || shimClassHas(o, "Player") || shimClassHas(o, "/Entity") || shimClassHas(o, "LivingEntity"));
}

// The arguments of a stand-in call, one each, with what each is.
struct Args {
    std::vector<Value> v;
    std::string kinds;
    [[nodiscard]] std::size_t size() const { return v.size(); }
    [[nodiscard]] double num(std::size_t i) const {
        switch (kinds[i]) {
            case 'F': return v[i].f;
            case 'D': return v[i].d;
            case 'J': return static_cast<double>(v[i].j);
            default: return v[i].i;
        }
    }
};

[[nodiscard]] inline Args unpack(Value* a, const MethodRef& r, bool isStatic) {
    Args out;
    int slot = isStatic ? 0 : 1;
    for (const char k : r.params) {
        out.v.push_back(a[slot]);
        out.kinds.push_back(k);
        slot += k == 'J' || k == 'D' ? 2 : 1;
    }
    return out;
}

[[nodiscard]] inline std::vector<std::size_t> objectIndexes(const Args& g) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < g.kinds.size(); ++i) {
        if (g.kinds[i] == 'A') out.push_back(i);
    }
    return out;
}
[[nodiscard]] inline std::vector<std::size_t> numberIndexes(const Args& g) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < g.kinds.size(); ++i) {
        if (g.kinds[i] != 'A') out.push_back(i);
    }
    return out;
}

// The text a Component or a String carries; a translatable text is looked up when the client lent the VM its language.
[[nodiscard]] inline std::string textOf(Vm& vm, Object* o) {
    if (o == nullptr) return {};
    if (o->kind == Kind::String) return o->str;
    if (o->shim && o->shim->origin == "translatable") return vm.host.translate ? vm.host.translate(o->str) : o->str;
    return o->str;
}

[[nodiscard]] inline std::string lowerCaseOf(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The first recorded call of this name on a stand-in (the constructor is "<init>"), or null.
[[nodiscard]] inline const ShimCall* firstCall(const Object* o, const char* method) {
    if (o == nullptr || !o->shim) return nullptr;
    for (const ShimCall& c : o->shim->calls) {
        if (c.method == method) return &c;
    }
    return nullptr;
}
[[nodiscard]] inline const ShimCall* lastCall(const Object* o, const char* method) {
    if (o == nullptr || !o->shim) return nullptr;
    for (auto it = o->shim->calls.rbegin(); it != o->shim->calls.rend(); ++it) {
        if (it->method == method) return &*it;
    }
    return nullptr;
}

// A key mapping's name ("key.examplemod.cast"): the first text its constructor was given.
[[nodiscard]] inline std::string keyNameOf(const Object* mapping) {
    const ShimCall* init = firstCall(mapping, "<init>");
    if (init == nullptr) return {};
    for (std::size_t i = 0; i < init->raw.size(); ++i) {
        if (init->kinds[i] == 'A' && init->raw[i].a != nullptr && init->raw[i].a->kind == Kind::String) return init->raw[i].a->str;
    }
    return {};
}

// The id of an item a mod passed to a draw call: a stack (its first constructor argument is the item), a registered item, or one of the game's ("Items.DIAMOND" is minecraft:diamond).
[[nodiscard]] inline std::string itemIdOf(Vm& vm, Object* o) {
    if (o == nullptr || !o->shim) return {};
    if (shimClassHas(o, "ItemStack")) {
        const ShimCall* init = firstCall(o, "<init>");
        if (init == nullptr) return {};
        for (std::size_t i = 0; i < init->raw.size(); ++i) {
            if (init->kinds[i] == 'A' && init->raw[i].a != nullptr) return itemIdOf(vm, init->raw[i].a);
        }
        return {};
    }
    for (const Registration& reg : vm.capture.registrations) {
        if (reg.object == o && (reg.registry == "item" || reg.registry == "block")) return reg.id;
    }
    const std::string& origin = o->shim->origin;
    const std::size_t dot = origin.find_last_of('.');
    if (dot == std::string::npos) return {};
    return "minecraft:" + lowerCaseOf(origin.substr(dot + 1));
}

}  // namespace falcon::jvm
