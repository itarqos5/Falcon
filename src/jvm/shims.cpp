// The stand-ins that know what a call means: registering something in a registry, an identifier, a text, an event, a key, the mod's logger. Every other call on a class this machine does not have
// is recorded as it is (vm.cpp, shimCall), so these are only the ones whose result the mod's own code goes on to use, or that the client wants to read.
#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <cctype>

namespace falcon::jvm {
namespace {

std::string lowerCase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The text an identifier or a registry key stands for ("examplemod:ruby"), from the object a mod passed: an Identifier, a ResourceKey, or a plain string.
std::string idText(Vm& vm, Object* o) {
    if (o == nullptr) return "null";
    if (o->kind == Kind::String) return o->str;
    if (!o->str.empty()) return o->str;
    return vm.describeObject(o);
}

std::string registryName(Vm& vm, Object* registry) {
    if (registry == nullptr) return "?";
    if (registry->shim && !registry->shim->origin.empty()) {  // "BuiltInRegistries.ITEM" -> "item"
        const std::string& origin = registry->shim->origin;
        return lowerCase(origin.substr(origin.find_last_of('.') + 1));
    }
    if (!registry->str.empty()) return registry->str;
    return vm.describeObject(registry);
}

std::string simpleOf(Object* o) { return o == nullptr ? std::string("null") : o->cls->simpleName(); }

// Identifier.parse("a:b"), fromNamespaceAndPath("a", "b"), withDefaultNamespace("b"): an object that carries its text.
Object* makeIdentifier(Vm& vm, const std::string& className, const std::string& text) {
    Object* o = vm.makeShim(className);
    o->str = text;
    return o;
}

}  // namespace

void installShims(Vm& vm) {
    // ---- registering ----
    // Registry.register(registry, id, thing): what a mod adds to the game. The thing keeps the calls its constructor made (its properties), so the client can read what it is.
    vm.defineShimHandler("net/minecraft/core/Registry.register", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        (void)r;
        Registration reg;
        reg.registry = registryName(v, a[0].a);
        reg.id = idText(v, a[1].a);
        reg.kind = simpleOf(a[2].a);
        reg.text = v.describeObject(a[2].a);
        reg.object = a[2].a;
        v.capture.registrations.push_back(std::move(reg));
        result = a[2];
        return true;
    });
    vm.defineShimHandler("net/minecraft/core/Registry.registerForHolder", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        (void)r;
        Registration reg;
        reg.registry = registryName(v, a[0].a);
        reg.id = idText(v, a[1].a);
        reg.kind = simpleOf(a[2].a);
        reg.text = v.describeObject(a[2].a);
        reg.object = a[2].a;
        v.capture.registrations.push_back(std::move(reg));
        result = valueOfRef(v.makeShim("net/minecraft/core/Holder$Reference"));
        return true;
    });

    // ---- identifiers (the game's class is Identifier in 26.x and was ResourceLocation before) ----
    for (const char* cls : {"net/minecraft/resources/Identifier", "net/minecraft/resources/ResourceLocation"}) {
        const std::string name = cls;
        vm.defineShimHandler(name + ".fromNamespaceAndPath", [name](Vm& v, Value* a, const MethodRef&, Value& result) {
            result = valueOfRef(makeIdentifier(v, name, v.str(a[0].a) + ":" + v.str(a[1].a)));
            return true;
        });
        vm.defineShimHandler(name + ".of", [name](Vm& v, Value* a, const MethodRef& r, Value& result) {
            result = valueOfRef(makeIdentifier(v, name, r.params.size() == 2 ? v.str(a[0].a) + ":" + v.str(a[1].a) : (v.str(a[0].a).find(':') == std::string::npos ? "minecraft:" + v.str(a[0].a) : v.str(a[0].a))));
            return true;
        });
        for (const char* parse : {".parse", ".tryParse", ".withDefaultNamespace"}) {
            vm.defineShimHandler(name + parse, [name](Vm& v, Value* a, const MethodRef&, Value& result) {
                const std::string& s = v.str(a[0].a);
                result = valueOfRef(makeIdentifier(v, name, s.find(':') == std::string::npos ? "minecraft:" + s : s));
                return true;
            });
        }
        vm.defineShimHandler(name + ".getNamespace", [](Vm& v, Value* a, const MethodRef&, Value& result) {
            const std::string& s = a[0].a->str;
            result = valueOfRef(v.newString(s.substr(0, s.find(':'))));
            return true;
        });
        vm.defineShimHandler(name + ".getPath", [](Vm& v, Value* a, const MethodRef&, Value& result) {
            const std::string& s = a[0].a->str;
            result = valueOfRef(v.newString(s.find(':') == std::string::npos ? s : s.substr(s.find(':') + 1)));
            return true;
        });
        vm.defineShimHandler(name + ".toString", [](Vm& v, Value* a, const MethodRef&, Value& result) {
            result = valueOfRef(v.newString(a[0].a->str));
            return true;
        });
    }
    vm.defineShimHandler("net/minecraft/resources/ResourceKey.create", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        Object* key = v.makeShim("net/minecraft/resources/ResourceKey", a[0].a != nullptr && a[0].a->shim ? a[0].a->shim->origin : std::string());
        key->str = idText(v, a[1].a);
        result = valueOfRef(key);
        return true;
    });
    vm.defineShimHandler("net/minecraft/resources/ResourceKey.createRegistryKey", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        Object* key = v.makeShim("net/minecraft/resources/ResourceKey");
        key->str = idText(v, a[0].a);
        result = valueOfRef(key);
        return true;
    });

    // ---- texts ----
    for (const char* method : {"literal", "translatable", "nullToEmpty"}) {
        vm.defineShimHandler(std::string("net/minecraft/network/chat/Component.") + method, [](Vm& v, Value* a, const MethodRef&, Value& result) {
            Object* c = v.makeShim("net/minecraft/network/chat/MutableComponent");
            c->str = a[0].a != nullptr && a[0].a->kind == Kind::String ? a[0].a->str : std::string();
            result = valueOfRef(c);
            return true;
        });
    }

    // ---- events, keys ----
    // An event handler: `register` on an object that came from a static field of an event class (ClientTickEvents.END_CLIENT_TICK): the handler is a lambda or a method of the mod, said by name.
    vm.defineShimHandler("*.register", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (r.params.size() < 1 || a[0].a == nullptr || !a[0].a->shim || a[0].a->shim->origin.empty()) return false;
        v.capture.events.push_back(a[0].a->shim->origin + ": " + v.describe(a[1], 'A'));
        if (a[1].a != nullptr) v.capture.handlers.push_back({a[0].a->shim->origin, a[1].a});
        result = r.ret == 'A' ? a[0] : Value();
        return true;
    });
    // What a mod says to the player: shown in the client's chat.
    for (const char* name : {"*.displayClientMessage", "*.sendSystemMessage", "*.displayMessage"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef& r, Value&) {
            if (r.params.empty() || r.params[0] != 'A' || a[1].a == nullptr) return false;
            if (v.capture.chat.size() < 200) v.capture.chat.push_back(!a[1].a->str.empty() ? a[1].a->str : v.describeObject(a[1].a));
            return true;
        });
    }
    // A key a mod adds (the helper is KeyBindingHelper.registerKeyBinding in older versions of the API and KeyMappingHelper.registerKeyMapping in 26.x).
    for (const char* name : {"*.registerKeyBinding", "*.registerKeyMapping"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef& r, Value& result) {
            if (r.params.size() != 1 || a[0].a == nullptr) return false;
            v.capture.keys.push_back(v.describeObject(a[0].a));
            result = a[0];
            return true;
        });
    }

    // ---- what the mod says ----
    const auto logger = [](const char* level) {
        return [level](Vm& v, Value* a, const MethodRef& r, Value&) {
            if (r.params.empty() || r.params[0] != 'A' || a[1].a == nullptr || a[1].a->kind != Kind::String) return false;
            std::vector<std::string> args;
            int slot = 2;
            for (std::size_t i = 1; i < r.params.size(); ++i) {
                Object* o = a[slot].a;
                if (r.params[i] == 'A' && o != nullptr && o->kind == Kind::Array) {
                    for (const Value& e : o->elems) args.push_back(v.toJavaString(e.a));
                } else {
                    args.push_back(r.params[i] == 'A' ? v.toJavaString(o) : v.describe(a[slot], r.params[i]));
                }
                slot += r.params[i] == 'J' || r.params[i] == 'D' ? 2 : 1;
            }
            std::string text, format = a[1].a->str;
            std::size_t next = 0;
            for (std::size_t i = 0; i < format.size(); ++i) {
                if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '}' && next < args.size()) {
                    text += args[next++];
                    ++i;
                } else {
                    text.push_back(format[i]);
                }
            }
            v.note(std::string("[") + level + "] " + text);
            return true;
        };
    };
    for (const char* owner : {"org/slf4j/Logger", "org/apache/logging/log4j/Logger"}) {
        for (const char* level : {"info", "warn", "error", "debug", "trace"}) vm.defineShimHandler(std::string(owner) + "." + level, logger(level));
    }
    for (const char* method : {"println", "print"}) {
        vm.defineShimHandler(std::string("java/io/PrintStream.") + method, [](Vm& v, Value* a, const MethodRef& r, Value&) {
            if (r.params.empty()) return true;
            v.note("[out] " + v.describe(a[1], r.params[0]));
            return true;
        });
    }
    vm.defineShimHandler("net/fabricmc/loader/api/FabricLoader.isModLoaded", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        const std::string id = v.str(a[1].a);
        result = valueOfInt(id == v.modId || id == "minecraft" || id == "fabricloader" || id == "java" ? 1 : 0);
        return true;
    });
}

}  // namespace falcon::jvm
