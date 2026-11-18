// Reflection for the platform's java.lang.reflect (modapi/jdk/java/lang/reflect, modapi/src/net/falcon/jdk/ClassExt.java): finding a method, a field or a constructor by name, calling it, reading and
// writing a field, by class name. What a mod does with these is mostly to look for another mod's classes (a compatibility probe, answered with ClassNotFoundException when they are not there) and
// to call into its own or the game's.
#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <string>

namespace falcon::jvm {
namespace {

Value ref(Object* o) { return valueOfRef(o); }

std::string textOf(Vm& v, Object* o) { return o == nullptr ? std::string() : v.str(o); }

// "int" -> "I", "java.lang.String" -> "Ljava/lang/String;", "[I" stays, "[Ljava.lang.String;" -> "[Ljava/lang/String;"
std::string descriptorOfName(const std::string& name) {
    static const std::pair<const char*, const char*> kPrimitives[] = {{"int", "I"}, {"long", "J"}, {"float", "F"}, {"double", "D"}, {"boolean", "Z"}, {"byte", "B"}, {"char", "C"}, {"short", "S"}, {"void", "V"}};
    for (const auto& [n, d] : kPrimitives) {
        if (name == n) return d;
    }
    std::string out = name;
    std::replace(out.begin(), out.end(), '.', '/');
    return !out.empty() && out[0] == '[' ? out : "L" + out + ";";
}

// the class name of a descriptor: "I" -> "int", "Ljava/lang/String;" -> "java.lang.String", "[I" -> "[I"
std::string nameOfDescriptor(const std::string& desc) {
    if (desc.empty()) return "void";
    switch (desc[0]) {
        case 'I': return "int";
        case 'J': return "long";
        case 'F': return "float";
        case 'D': return "double";
        case 'Z': return "boolean";
        case 'B': return "byte";
        case 'C': return "char";
        case 'S': return "short";
        case 'V': return "void";
        case '[': {
            std::string out = desc;
            std::replace(out.begin(), out.end(), '/', '.');
            return out;
        }
        default: {
            std::string out = desc.size() > 2 ? desc.substr(1, desc.size() - 2) : desc;
            std::replace(out.begin(), out.end(), '/', '.');
            return out;
        }
    }
}

std::string paramsOf(Vm& v, Object* names) {
    std::string out = "(";
    if (names != nullptr) {
        for (const Value& e : names->elems) out += descriptorOfName(textOf(v, e.a));
    }
    return out + ")";
}

// a value of an Object[] argument as the method's parameter of that kind
Value argumentValue(Vm& v, Object* boxed, char kind) {
    if (kind == 'A') return ref(boxed);
    return boxed != nullptr ? v.unbox(boxed) : Value();
}

Object* boxedResult(Vm& v, const std::string& desc, Value result) {
    const std::size_t close = desc.find(')');
    const char r = close == std::string::npos || close + 1 >= desc.size() ? 'V' : desc[close + 1];
    switch (r) {
        case 'V': return nullptr;
        case 'I': case 'S': case 'B': case 'C': case 'Z': case 'J': case 'F': case 'D': return v.box(r, result);
        default: return result.a;
    }
}

}  // namespace

void installReflectNatives(Vm& vm) {
    const std::string R = "net/falcon/Reflect";
    const auto nat = [&](const char* name, const char* desc, NativeFn fn) { vm.defineHostNative(R, name, desc, std::move(fn)); };

    nat("methodInfo", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Ljava/lang/String;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        const std::string name = textOf(v, a[1].a), params = paramsOf(v, a[2].a);
        const std::function<Method*(Class*)> find = [&](Class* k) -> Method* {
            if (k == nullptr) return nullptr;
            for (auto& e : k->methods) {
                Method* m = e.second.get();
                if (m->name == name && m->desc.compare(0, params.size(), params) == 0) return m;
            }
            for (Class* i : k->interfaces) {
                if (Method* m = find(i)) return m;
            }
            return find(k->super);
        };
        Method* m = c->shim ? nullptr : find(c);
        if (m == nullptr) return ref(nullptr);
        const std::string ret = m->desc.substr(m->desc.find(')') + 1);
        return ref(v.newString(m->owner->name + '\0' + m->desc + '\0' + std::to_string(m->access) + '\0' + nameOfDescriptor(ret)));
    });
    nat("fieldInfo", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", [](Vm& v, Value* a) {
        const std::string name = textOf(v, a[1].a);
        for (Class* k = v.loadClass(textOf(v, a[0].a)); k != nullptr && !k->shim; k = k->super) {
            if (const auto it = k->fields.find(name); it != k->fields.end()) {
                return ref(v.newString(k->name + '\0' + it->second.desc + '\0' + std::to_string(it->second.access) + '\0' + nameOfDescriptor(it->second.desc)));
            }
        }
        return ref(nullptr);
    });
    nat("constructorInfo", "(Ljava/lang/String;[Ljava/lang/String;)Ljava/lang/String;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        const std::string params = paramsOf(v, a[1].a);
        if (c->shim) return ref(nullptr);
        for (auto& e : c->methods) {
            const Method* m = e.second.get();
            if (m->name == "<init>" && m->desc.compare(0, params.size(), params) == 0) return ref(v.newString(m->desc));
        }
        return ref(nullptr);
    });
    nat("invoke", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        const std::string name = textOf(v, a[1].a), desc = textOf(v, a[2].a);
        Object* target = a[3].a;
        Method* m = target != nullptr ? v.findMethod(target->cls, name, desc) : nullptr;
        if (m == nullptr) m = v.findMethod(c, name, desc);
        if (m == nullptr) v.throwNew("java/lang/NoSuchMethodError", c->name + "." + name + desc);
        std::vector<Value> args;
        if (!m->isStatic()) {
            if (target == nullptr) v.throwNew("java/lang/NullPointerException");
            args.push_back(ref(target));
        }
        Object* given = a[4].a;
        for (std::size_t i = 0; i < m->params.size(); ++i) args.push_back(given != nullptr && i < given->elems.size() ? argumentValue(v, given->elems[i].a, m->params[i]) : Value());
        v.ensureInitialized(m->owner);
        return ref(boxedResult(v, desc, v.invoke(m, args.data(), static_cast<int>(args.size()))));
    });
    nat("construct", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        const std::string desc = textOf(v, a[1].a);
        if (c->shim) v.throwNew("java/lang/InstantiationError", c->name);
        v.ensureInitialized(c);
        Object* o = v.newObject(c);
        Method* init = v.findMethod(c, "<init>", desc);
        if (init == nullptr) return ref(o);
        std::vector<Value> args{ref(o)};
        Object* given = a[2].a;
        for (std::size_t i = 0; i < init->params.size(); ++i) args.push_back(given != nullptr && i < given->elems.size() ? argumentValue(v, given->elems[i].a, init->params[i]) : Value());
        v.invoke(init, args.data(), static_cast<int>(args.size()));
        return ref(o);
    });
    nat("getAny", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;", [](Vm& v, Value* a) {
        const std::string name = textOf(v, a[1].a);
        for (Class* k = a[2].a != nullptr ? a[2].a->cls : v.loadClass(textOf(v, a[0].a)); k != nullptr && !k->shim; k = k->super) {
            const auto it = k->fields.find(name);
            if (it == k->fields.end()) continue;
            const FieldInfo& f = it->second;
            Value raw;
            if (f.isStatic) {
                v.ensureInitialized(k);
                raw = static_cast<std::size_t>(f.index) < k->statics.size() ? k->statics[static_cast<std::size_t>(f.index)] : Value();
            } else if (a[2].a != nullptr && static_cast<std::size_t>(f.index) < a[2].a->fields.size()) {
                raw = a[2].a->fields[static_cast<std::size_t>(f.index)];
            }
            const char kind = f.desc.empty() ? 'L' : f.desc[0];
            return ref(kind == 'L' || kind == '[' ? raw.a : v.box(kind, raw));
        }
        return ref(nullptr);
    });
    nat("setAny", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;)V", [](Vm& v, Value* a) {
        const std::string name = textOf(v, a[1].a);
        for (Class* k = a[2].a != nullptr ? a[2].a->cls : v.loadClass(textOf(v, a[0].a)); k != nullptr && !k->shim; k = k->super) {
            const auto it = k->fields.find(name);
            if (it == k->fields.end()) continue;
            const FieldInfo& f = it->second;
            const char kind = f.desc.empty() ? 'L' : f.desc[0];
            const Value value = kind == 'L' || kind == '[' ? ref(a[3].a) : (a[3].a != nullptr ? v.unbox(a[3].a) : Value());
            if (f.isStatic) {
                v.ensureInitialized(k);
                if (static_cast<std::size_t>(f.index) < k->statics.size()) k->statics[static_cast<std::size_t>(f.index)] = value;
            } else if (a[2].a != nullptr && static_cast<std::size_t>(f.index) < a[2].a->fields.size()) {
                a[2].a->fields[static_cast<std::size_t>(f.index)] = value;
            }
            return Value();
        }
        return Value();
    });
    nat("isInterface", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { return valueOfInt(v.loadClass(textOf(v, a[0].a))->isInterface ? 1 : 0); });
    nat("isAssignable", "(Ljava/lang/String;Ljava/lang/String;)Z", [](Vm& v, Value* a) {
        Class* base = v.loadClass(textOf(v, a[0].a));
        Class* other = v.loadClass(textOf(v, a[1].a));
        const std::function<bool(Class*)> walk = [&](Class* k) -> bool {
            if (k == nullptr) return false;
            if (k == base) return true;
            for (Class* i : k->interfaces) {
                if (walk(i)) return true;
            }
            return walk(k->super);
        };
        return valueOfInt(walk(other) ? 1 : 0);
    });
    nat("resource", "(Ljava/lang/String;)[B", [](Vm& v, Value* a) {
        const auto bytes = v.readResource(textOf(v, a[0].a));
        if (!bytes) return ref(nullptr);
        Object* out = v.newArray('B', static_cast<int>(bytes->size()));
        for (std::size_t i = 0; i < bytes->size(); ++i) out->elems[i] = valueOfInt(static_cast<signed char>((*bytes)[i]));
        return ref(out);
    });
}

}  // namespace falcon::jvm
