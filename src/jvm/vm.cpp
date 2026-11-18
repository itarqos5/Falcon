#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace falcon::jvm {

// A call site made by invokedynamic: a lambda, a string concatenation or a record's method, worked out once.
struct IndySite {
    enum class Kind { Lambda, Concat, RecordToString, RecordEquals, RecordHash, Unsupported } kind = Kind::Unsupported;
    std::vector<char> params;  // the arguments it takes, as I J F D A
    std::string types;         // and as the descriptor writes them (Z C B S I J F D L)
    int argSlots = 0;
    char ret = 'A';
    Class* iface = nullptr;    // a lambda: the interface it makes
    MethodRef* handle = nullptr;
    int handleKind = 0;
    std::string recipe;        // a concatenation
    std::vector<Object*> constants;
    std::vector<std::string> components;  // a record's fields
    Class* record = nullptr;
    std::string why;
};

namespace {

constexpr int kStaticOp = 0xB8, kSpecialOp = 0xB7, kVirtualOp = 0xB6, kInterfaceOp = 0xB9;

char kindOfType(char c) { return c == 'J' || c == 'F' || c == 'D' ? c : (c == 'L' || c == '[') ? 'A' : 'I'; }

std::string paramTypes(const std::string& desc) {
    std::string out;
    std::size_t i = 1;
    while (i < desc.size() && desc[i] != ')') {
        const char c = desc[i];
        if (c == 'L') {
            i = desc.find(';', i) + 1;
            out.push_back('L');
        } else if (c == '[') {
            while (desc[i] == '[') ++i;
            i = desc[i] == 'L' ? desc.find(';', i) + 1 : i + 1;
            out.push_back('L');
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::string javaDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", d);
    std::string s = buffer;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

void appendUtf8(std::string& out, unsigned c) {
    if (c < 0x80) out.push_back(static_cast<char>(c));
    else if (c < 0x800) out += {static_cast<char>(0xC0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3F))};
    else out += {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)), static_cast<char>(0x80 | (c & 0x3F))};
}

}  // namespace

// The game, the mod loader and its API are the platform: a mod carries copies of some of them (jar-in-jar), but those are not run: their classes stay stand-ins, so that what a mod does with
// them (an event it registers for, a key it adds) is seen instead of being worked out by the library's own code.
static bool isPlatformClass(const std::string& name) {
    static const char* const kPrefixes[] = {"net/minecraft/", "com/mojang/", "net/fabricmc/", "org/quiltmc/", "org/spongepowered/", "org/slf4j/", "org/apache/logging/", "com/google/common/", "io/netty/",
                                           "com/llamalad7/mixinextras/", "org/objectweb/asm/", "it/unimi/dsi/fastutil/"};
    for (const char* prefix : kPrefixes) {
        if (name.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::string Class::simpleName() const {
    const std::size_t at = name.find_last_of("/$");
    return at == std::string::npos ? name : name.substr(at + 1);
}

void Vm::parseDescriptor(const std::string& desc, std::vector<char>& params, char& ret, int& slots) {
    params.clear();
    slots = 0;
    std::size_t i = 1;
    while (i < desc.size() && desc[i] != ')') {
        const char c = desc[i];
        if (c == 'L') {
            i = desc.find(';', i) + 1;
            params.push_back('A');
            ++slots;
        } else if (c == '[') {
            while (desc[i] == '[') ++i;
            i = desc[i] == 'L' ? desc.find(';', i) + 1 : i + 1;
            params.push_back('A');
            ++slots;
        } else {
            ++i;
            const char k = kindOfType(c);
            params.push_back(k);
            slots += k == 'J' || k == 'D' ? 2 : 1;
        }
    }
    const char r = i + 1 < desc.size() ? desc[i + 1] : 'V';
    ret = r == 'V' ? 'V' : kindOfType(r);
}

std::string Vm::stripKey(const std::string& desc) { return desc; }

Vm::Vm() : slab_(1u << 18) {
    top_ = slab_.data();
    defineClass("java/lang/Object", "");
    objectClass_ = classes_["java/lang/Object"].get();
    installNatives(*this);
    installShims(*this);
    stringClass_ = loadClass("java/lang/String");
    classClass_ = loadClass("java/lang/Class");
}

Vm::~Vm() = default;

void Vm::addJar(std::shared_ptr<const ZipArchive> jar) { jars_.push_back(std::move(jar)); }

// ---- classes -------------------------------------------------------------------------------------------------------------------------------------------------

Class* Vm::makeClass(const std::string& name) {
    auto c = std::make_unique<Class>();
    c->name = name;
    Class* raw = c.get();
    classes_[name] = std::move(c);
    return raw;
}

Method* Vm::makeMethod(Class* cls, const std::string& name, const std::string& desc, std::uint16_t access) {
    auto m = std::make_unique<Method>();
    m->owner = cls;
    m->name = name;
    m->desc = desc;
    m->access = access;
    int slots = 0;
    parseDescriptor(desc, m->params, m->ret, slots);
    m->argSlots = slots + (m->isStatic() ? 0 : 1);
    Method* raw = m.get();
    cls->methods[name + desc] = std::move(m);
    return raw;
}

void Vm::defineClass(const std::string& name, const std::string& super, const std::vector<std::string>& interfaces, bool isInterface) {
    Class* c = makeClass(name);
    c->super = super.empty() ? nullptr : loadClass(super);
    for (const std::string& i : interfaces) c->interfaces.push_back(loadClass(i));
    c->isInterface = isInterface;
    c->state = Class::State::Ready;
    c->instanceSize = c->super ? c->super->instanceSize : 0;
}

void Vm::defineNative(const std::string& cls, const std::string& name, const std::string& desc, bool isStatic, NativeFn fn) {
    Class* c = loadClass(cls);
    Method* m = makeMethod(c, name, desc, static_cast<std::uint16_t>(0x0001 | (isStatic ? 0x0008 : 0)));
    m->native = std::move(fn);
}

Class* Vm::defineFromFile(std::shared_ptr<const ClassFile> file) {
    Class* c = makeClass(file->name);
    c->file = file;
    c->isInterface = file->isInterface();
    c->super = file->superName.empty() ? nullptr : loadClass(file->superName);
    for (const std::string& i : file->interfaces) c->interfaces.push_back(loadClass(i));
    int index = c->super ? c->super->instanceSize : 0, staticIndex = 0;
    for (const Member& f : file->fields) {
        FieldInfo info;
        info.name = f.name;
        info.desc = f.desc;
        info.isStatic = (f.access & 0x0008) != 0;
        info.index = info.isStatic ? staticIndex++ : index++;
        info.constantIndex = f.constantIndex;
        c->fields[f.name] = info;
    }
    c->instanceSize = index;
    c->statics.assign(static_cast<std::size_t>(staticIndex), Value());
    for (const Member& m : file->methods) {
        Method* method = makeMethod(c, m.name, m.desc, m.access);
        if (m.hasCode) method->code = &m.code;
    }
    c->cache.assign(file->cp.size(), nullptr);
    return c;
}

Class* Vm::loadClass(const std::string& name) {
    if (const auto it = classes_.find(name); it != classes_.end()) return it->second.get();
    if (!name.empty() && name[0] == '[') {  // an array's class: an Object, for what is asked of it
        Class* c = makeClass(name);
        c->super = objectClass_;
        c->state = Class::State::Ready;
        return c;
    }
    for (const auto& jar : jars_) {
        if (isPlatformClass(name)) break;
        const auto bytes = jar->read(name + ".class");
        if (!bytes) continue;
        if (auto parsed = ClassFile::parse(bytes->data(), bytes->size())) return defineFromFile(std::make_shared<const ClassFile>(std::move(*parsed)));
    }
    Class* c = makeClass(name);  // not anywhere: the game's, the loader's or a library's: a stand-in
    c->shim = true;
    c->super = objectClass_;
    c->state = Class::State::Ready;
    return c;
}

Method* Vm::findMethod(Class* cls, const std::string& name, const std::string& desc) {
    const std::string key = name + desc;
    const std::function<Method*(Class*)> inInterfaces = [&](Class* c) -> Method* {
        for (Class* i : c->interfaces) {
            if (const auto it = i->methods.find(key); it != i->methods.end() && it->second->callable()) return it->second.get();
            if (Method* m = inInterfaces(i)) return m;
        }
        return nullptr;
    };
    for (Class* c = cls; c != nullptr; c = c->super) {
        if (const auto it = c->methods.find(key); it != c->methods.end() && it->second->callable()) return it->second.get();
    }
    for (Class* c = cls; c != nullptr; c = c->super) {
        if (Method* m = inInterfaces(c)) return m;
    }
    return nullptr;
}

void Vm::ensureInitialized(Class* cls) {
    if (cls->state != Class::State::Uninitialized) return;
    cls->state = Class::State::Initializing;
    if (cls->super) ensureInitialized(cls->super);
    if (cls->file) {
        const ClassFile& cf = *cls->file;
        for (const auto& [name, f] : cls->fields) {  // the constants (a static final with a ConstantValue)
            if (!f.isStatic || f.constantIndex == 0 || f.constantIndex >= cf.cp.size()) continue;
            const CpEntry& e = cf.cp[f.constantIndex];
            Value v;
            switch (e.tag) {
                case kInteger: v = valueOfInt(e.i); break;
                case kLong: v = valueOfLong(e.j); break;
                case kFloat: v = valueOfFloat(e.f); break;
                case kDouble: v = valueOfDouble(e.d); break;
                case kString: v = valueOfRef(intern(cf.utf8(e.a))); break;
                default: break;
            }
            cls->statics[static_cast<std::size_t>(f.index)] = v;
        }
        if (Method* clinit = findMethod(cls, "<clinit>", "()V"); clinit != nullptr && clinit->owner == cls) {
            try {
                invoke(clinit, nullptr, 0);
            } catch (...) {
                cls->state = Class::State::Ready;
                throw;
            }
        }
    }
    cls->state = Class::State::Ready;
}

Object* Vm::instantiate(Class* cls) {
    ensureInitialized(cls);
    Object* o = newObject(cls);
    if (Method* init = findMethod(cls, "<init>", "()V")) {
        Value args[1];
        args[0] = valueOfRef(o);
        invoke(init, args, 1);
    }
    return o;
}

// ---- objects ---------------------------------------------------------------------------------------------------------------------------------------------------

Object* Vm::newObject(Class* cls) {
    heap_.push_back(std::make_unique<Object>());
    Object* o = heap_.back().get();
    o->cls = cls;
    o->fields.assign(static_cast<std::size_t>(cls->instanceSize), Value());
    return o;
}

Object* Vm::newString(std::string text) {
    Object* o = newObject(stringClass_ != nullptr ? stringClass_ : loadClass("java/lang/String"));
    o->kind = Kind::String;
    o->str = std::move(text);
    return o;
}

Object* Vm::intern(const std::string& text) {
    if (const auto it = interned_.find(text); it != interned_.end()) return it->second;
    Object* o = newString(text);
    interned_[text] = o;
    return o;
}

Object* Vm::newArray(char elemType, int length) {
    if (length < 0) throwNew("java/lang/NegativeArraySizeException", std::to_string(length));
    Object* o = newObject(objectClass_);
    o->kind = Kind::Array;
    o->elemType = elemType;
    o->elems.assign(static_cast<std::size_t>(length), Value());
    return o;
}

Object* Vm::box(char type, Value v) {
    static const std::map<char, const char*> kClasses = {{'I', "java/lang/Integer"}, {'J', "java/lang/Long"}, {'F', "java/lang/Float"}, {'D', "java/lang/Double"},
                                                         {'Z', "java/lang/Boolean"}, {'C', "java/lang/Character"}, {'B', "java/lang/Byte"}, {'S', "java/lang/Short"}};
    Object* o = newObject(loadClass(kClasses.at(type)));
    o->kind = Kind::Box;
    o->prim = v;
    o->primType = type;
    return o;
}

Object* Vm::classObject(const std::string& name) {
    if (const auto it = classObjects_.find(name); it != classObjects_.end()) return it->second;
    Object* o = newObject(classClass_ != nullptr ? classClass_ : loadClass("java/lang/Class"));
    o->kind = Kind::Class;
    o->str = name;
    classObjects_[name] = o;
    return o;
}

void Vm::throwNew(const std::string& className, const std::string& message) {
    Object* e = newObject(loadClass(className));
    e->str = message;
    e->primType = message.empty() ? 0 : 1;  // (a message was given)
    throw JavaThrow{e};
}

std::string& Vm::str(Object* o) {
    if (o == nullptr) throwNew("java/lang/NullPointerException");
    return o->str;
}

Object* Vm::makeShim(const std::string& className, const std::string& origin) {
    Object* o = newObject(loadClass(className));
    o->shim = std::make_unique<Shim>();
    o->shim->className = className;
    o->shim->origin = origin;
    return o;
}

Value Vm::shimField(Object* o, const std::string& name, const std::string& desc) {
    Shim& shim = shimOf(o);
    if (desc.empty() || (desc[0] != 'L' && desc[0] != '[')) {
        if (const auto set = shim.values.find(name); set != shim.values.end()) return set->second;
        if (name == "lineHeight") return valueOfInt(9);  // Font.lineHeight
        return Value();
    }
    if (name == "screen") return valueOfRef(nullptr);  // (a HUD runs with no screen open)
    if (const auto it = shim.fields.find(name); it != shim.fields.end()) return valueOfRef(it->second);
    Object* field = desc[0] == '[' ? newArray('L', 0) : makeDefault(desc.substr(1, desc.size() - 2), (o->cls->simpleName()) + "." + name);
    shim.fields[name] = field;
    return valueOfRef(field);
}

Shim& Vm::shimOf(Object* o) {
    if (!o->shim) {
        o->shim = std::make_unique<Shim>();
        o->shim->className = o->cls->name;
    }
    return *o->shim;
}

Object* Vm::makeDefault(const std::string& name, const std::string& origin) {
    if (name == "java/lang/String") return intern("");
    if (name == "java/util/List" || name == "java/util/Collection" || name == "java/lang/Iterable" || name == "java/util/Queue" || name == "java/util/Deque" || name == "java/util/ArrayList") return newObject("java/util/ArrayList");
    if (name == "java/util/Set") return newObject("java/util/HashSet");
    if (name == "java/util/Map" || name == "java/util/concurrent/ConcurrentMap") return newObject("java/util/HashMap");
    if (name == "java/util/Optional") return newObject("java/util/Optional");
    return makeShim(name, origin);
}

void Vm::collectGarbage() {
    if (depth_ != 0) return;
    std::unordered_set<const void*> live;
    live.reserve(heap_.size() * 2);
    for (const auto& o : heap_) {
        o->marked = false;
        live.insert(o.get());
    }
    std::vector<Object*> work;
    const auto reach = [&](Object* o) {
        if (o != nullptr && live.count(o) != 0 && !o->marked) {
            o->marked = true;
            work.push_back(o);
        }
    };
    const auto reachValue = [&](const Value& v) { reach(reinterpret_cast<Object*>(static_cast<std::uintptr_t>(v.raw))); };
    for (const auto& [name, cls] : classes_) {
        for (const Value& v : cls->statics) reachValue(v);
        for (const auto& [field, o] : cls->shimStatics) reach(o);
    }
    for (const auto& [text, o] : interned_) reach(o);
    for (const auto& [name, o] : classObjects_) reach(o);
    for (const auto& site : sites_) {
        for (Object* o : site->constants) reach(o);
    }
    reach(client_);
    for (Object* o : pins_) reach(o);
    for (const Registration& r : capture.registrations) reach(r.object);
    for (const EventHandler& h : capture.handlers) reach(h.handler);
    for (const GroupItem& g : capture.groupItems) reach(g.item);
    for (Object* o : capture.keyObjects) reach(o);
    while (!work.empty()) {
        Object* o = work.back();
        work.pop_back();
        for (const Value& v : o->fields) reachValue(v);
        for (const Value& v : o->elems) reachValue(v);
        for (const auto& [k, v] : o->map) {
            reachValue(k);
            reachValue(v);
        }
        for (const Value& v : o->captured) reachValue(v);
        reachValue(o->prim);
        if (o->shim) {
            for (const auto& [name, f] : o->shim->fields) reach(f);
            for (const auto& [name, v] : o->shim->values) reachValue(v);
            for (const ShimCall& c : o->shim->calls) {
                for (const Value& v : c.raw) reachValue(v);
            }
        }
    }
    heap_.erase(std::remove_if(heap_.begin(), heap_.end(), [](const std::unique_ptr<Object>& o) { return !o->marked; }), heap_.end());
}

Object* Vm::client() {
    if (client_ == nullptr) client_ = makeShim("net/minecraft/client/Minecraft");
    return client_;
}

// What a game class is known to be, for the instanceof operator: the base classes mods test against.
bool Vm::isInstance(Object* o, Class* target) {
    if (o == nullptr) return false;
    if (o->shim && target->shim && o->cls->shim) {
        const std::string& have = o->shim->className;
        if (have == target->name) return true;
        const auto endsWith = [](const std::string& s, const char* suffix) {
            const std::string t = suffix;
            return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
        };
        const std::string& want = target->name;
        if (endsWith(want, "/Screen")) return endsWith(have, "Screen");
        if (endsWith(want, "/Entity") || endsWith(want, "/LivingEntity")) return endsWith(have, "Player") || endsWith(have, "Entity") || endsWith(have, "Mob");
        if (endsWith(want, "/Player")) return endsWith(have, "Player");
        if (endsWith(want, "/Component")) return endsWith(have, "Component");
        if (endsWith(want, "/AbstractWidget")) return endsWith(have, "Button") || endsWith(have, "Widget") || endsWith(have, "Checkbox") || endsWith(have, "EditBox");
        return false;
    }
    return instanceOf(o, target);
}

bool Vm::instanceOf(Object* o, Class* target) {
    if (o == nullptr) return false;
    if (target == objectClass_) return true;
    if (o->shim && !target->name.empty() && target->name[0] == '[') return true;  // (a stand-in is let through a cast to an array)
    if (o->kind == Kind::Array) return target->name.empty() || target->name[0] == '[' || target->name == "java/lang/Cloneable";
    const std::function<bool(Class*)> walk = [&](Class* c) {
        if (c == target) return true;
        for (Class* i : c->interfaces) {
            if (walk(i)) return true;
        }
        return c->super != nullptr && walk(c->super);
    };
    if (walk(o->cls)) return true;
    return target->shim;  // (what the game's classes extend is not known: a cast to one is let through)
}

// ---- text of objects -------------------------------------------------------------------------------------------------------------------------------------------

namespace {

std::string boxText(Object* o) {
    switch (o->primType) {
        case 'Z': return o->prim.i != 0 ? "true" : "false";
        case 'J': return std::to_string(o->prim.j);
        case 'F': return javaDouble(static_cast<double>(o->prim.f));
        case 'D': return javaDouble(o->prim.d);
        case 'C': {
            std::string s;
            appendUtf8(s, static_cast<unsigned>(o->prim.i) & 0xFFFF);
            return s;
        }
        default: return std::to_string(o->prim.i);
    }
}

}  // namespace

std::string Vm::describe(Value v, char kind, int depth) {
    switch (kind) {
        case 'I': return std::to_string(v.i);
        case 'J': return std::to_string(v.j);
        case 'F': return javaDouble(static_cast<double>(v.f));
        case 'D': return javaDouble(v.d);
        default: return describeObject(v.a, depth);
    }
}

std::string Vm::describeObject(Object* o, int depth) {
    if (o == nullptr) return "null";
    switch (o->kind) {
        case Kind::String: return "\"" + o->str + "\"";
        case Kind::Box: return boxText(o);
        case Kind::Class: return o->str + ".class";
        case Kind::Lambda: return "lambda(" + (o->handle != nullptr ? o->handle->cls->simpleName() + "::" + o->handle->name : std::string("?")) + ")";
        case Kind::Array:
        case Kind::Iterator: {
            std::string out = "[";
            for (std::size_t i = 0; i < o->elems.size() && i < 8; ++i) out += (i ? ", " : "") + describe(o->elems[i], o->elemType != 0 && o->elemType != 'L' ? (o->elemType == 'J' || o->elemType == 'F' || o->elemType == 'D' ? o->elemType : 'I') : 'A', depth + 1);
            return out + (o->elems.size() > 8 ? ", ...]" : "]");
        }
        default: break;
    }
    if (o->cls != nullptr && !o->cls->shim && !o->shim) {
        if (o->cls->name == "java/util/ArrayList" || o->cls->name == "java/util/HashSet" || o->cls->name == "java/util/LinkedHashSet") {
            std::string out = "[";
            for (std::size_t i = 0; i < o->elems.size() && i < 8; ++i) out += (i ? ", " : "") + describeObject(o->elems[i].a, depth + 1);
            return out + "]";
        }
        if (o->cls->name == "java/lang/StringBuilder") return "\"" + o->str + "\"";
        return o->cls->simpleName() + "@" + std::to_string(o->hash);
    }
    const std::string simple = o->cls != nullptr ? o->cls->simpleName() : std::string("?");
    if (!o->str.empty() && (simple == "Identifier" || simple == "ResourceLocation")) return "id(" + o->str + ")";
    if (!o->str.empty()) return simple + "(\"" + o->str + "\")";
    if (!o->shim || depth > 3) return simple;
    std::string out = simple + "[";
    for (std::size_t i = 0; i < o->shim->calls.size() && i < 10; ++i) {
        const ShimCall& c = o->shim->calls[i];
        out += (i ? ", " : "") + c.method + "(";
        for (std::size_t k = 0; k < c.args.size(); ++k) out += (k ? ", " : "") + c.args[k];
        out += ")";
    }
    return out + (o->shim->calls.size() > 10 ? ", ...]" : "]");
}

std::string Vm::toJavaString(Object* o) {
    if (o == nullptr) return "null";
    if (o->kind == Kind::String) return o->str;
    if (o->kind == Kind::Box) return boxText(o);
    if (o->cls != nullptr && !o->cls->shim) {
        if (Method* m = findMethod(o->cls, "toString", "()Ljava/lang/String;")) {
            Value args[1];
            args[0] = valueOfRef(o);
            Value r = invoke(m, args, 1);
            return r.a == nullptr ? "null" : r.a->str;
        }
    }
    if (o->shim && !o->str.empty()) return o->str;
    return describeObject(o);
}

bool Vm::equalsJava(Object* a, Object* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    if (a->kind == Kind::String && b->kind == Kind::String) return a->str == b->str;
    if (a->kind == Kind::Box && b->kind == Kind::Box) return a->primType == b->primType && a->prim.raw == b->prim.raw;
    if (a->kind == Kind::Class && b->kind == Kind::Class) return a->str == b->str;
    if (a->shim && b->shim && !a->str.empty()) return a->str == b->str && a->cls == b->cls;  // (identifiers and the like are values)
    if (a->cls != nullptr && !a->cls->shim) {
        if (Method* m = findMethod(a->cls, "equals", "(Ljava/lang/Object;)Z"); m != nullptr && !m->native) {
            Value args[2];
            args[0] = valueOfRef(a);
            args[1] = valueOfRef(b);
            return invoke(m, args, 2).i != 0;
        }
    }
    return false;
}

int Vm::hashJava(Object* o) {
    if (o == nullptr) return 0;
    if (o->kind == Kind::String) {
        std::uint32_t h = 0;
        for (const unsigned char c : o->str) h = h * 31 + c;
        return static_cast<int>(h);
    }
    if (o->kind == Kind::Box) return o->primType == 'J' ? static_cast<int>(o->prim.j ^ (o->prim.j >> 32)) : static_cast<int>(o->prim.raw);
    if (o->shim && !o->str.empty()) return static_cast<int>(std::hash<std::string>{}(o->str));
    if (o->cls != nullptr && !o->cls->shim) {
        if (Method* m = findMethod(o->cls, "hashCode", "()I"); m != nullptr && !m->native) {
            Value args[1];
            args[0] = valueOfRef(o);
            return invoke(m, args, 1).i;
        }
    }
    if (o->hash == 0) o->hash = nextHash_++ * 0x9E3779B1;
    return o->hash;
}

void Vm::note(const std::string& text) {
    if (capture.log.size() < 500) capture.log.push_back(text);
}

// ---- constant pool references ----------------------------------------------------------------------------------------------------------------------------------

MethodRef& Vm::methodRef(Class* cls, int index) {
    if (cls->cache[static_cast<std::size_t>(index)] != nullptr) return *static_cast<MethodRef*>(cls->cache[static_cast<std::size_t>(index)]);
    const ClassFile& cf = *cls->file;
    const CpEntry& e = cf.cp[static_cast<std::size_t>(index)];
    const CpEntry& nt = cf.cp[e.b];
    MethodRef& r = methodRefs_.emplace_back();
    r.cls = loadClass(cf.className(e.a));
    r.name = cf.utf8(nt.a);
    r.desc = cf.utf8(nt.b);
    r.key = r.name + r.desc;
    parseDescriptor(r.desc, r.params, r.ret, r.argSlots);
    cls->cache[static_cast<std::size_t>(index)] = &r;
    return r;
}

FieldRef& Vm::fieldRef(Class* cls, int index) {
    if (cls->cache[static_cast<std::size_t>(index)] != nullptr) return *static_cast<FieldRef*>(cls->cache[static_cast<std::size_t>(index)]);
    const ClassFile& cf = *cls->file;
    const CpEntry& e = cf.cp[static_cast<std::size_t>(index)];
    const CpEntry& nt = cf.cp[e.b];
    FieldRef& r = fieldRefs_.emplace_back();
    r.cls = loadClass(cf.className(e.a));
    r.name = cf.utf8(nt.a);
    r.desc = cf.utf8(nt.b);
    r.kind = kindOfType(r.desc.empty() ? 'I' : r.desc[0]);
    const std::function<bool(Class*)> search = [&](Class* c) {
        if (const auto it = c->fields.find(r.name); it != c->fields.end()) {
            r.declaring = c;
            r.info = &it->second;
            return true;
        }
        for (Class* i : c->interfaces) {
            if (search(i)) return true;
        }
        return c->super != nullptr && search(c->super);
    };
    search(r.cls);
    cls->cache[static_cast<std::size_t>(index)] = &r;
    return r;
}

Value Vm::fieldDefault(char) { return Value(); }

// A static field of a class that is not in a jar: the game's or a library's. One object, the same every time, remembering where it came from.
Value Vm::shimStatic(Class* cls, const FieldRef& f) {
    if (f.kind != 'A') return Value();
    if (const auto it = cls->shimStatics.find(f.name); it != cls->shimStatics.end()) return valueOfRef(it->second);
    Object* o = nullptr;
    o = makeDefault(f.desc.size() > 2 ? f.desc.substr(1, f.desc.size() - 2) : std::string("java/lang/Object"), cls->simpleName() + "." + f.name);
    cls->shimStatics[f.name] = o;
    return valueOfRef(o);
}

// ---- calls ---------------------------------------------------------------------------------------------------------------------------------------------------------

Value Vm::invoke(Method* m, const Value* args, int count) {
    (void)count;  // (the method's own descriptor says how many)
    Value slots[96];
    int s = 0, c = 0;
    if (!m->isStatic()) slots[s++] = args[c++];
    for (const char k : m->params) {
        if (s >= 90) break;
        slots[s] = args[c++];
        s += k == 'J' || k == 'D' ? 2 : 1;
    }
    return call(m, slots);
}

Value Vm::call(Method* m, Value* args) {
    if (m->native) {
        Value compact[48];
        int s = 0, c = 0;
        if (!m->isStatic()) compact[c++] = args[s++];
        for (const char k : m->params) {
            if (c >= 47) break;
            compact[c++] = args[s];
            s += k == 'J' || k == 'D' ? 2 : 1;
        }
        return m->native(*this, compact);
    }
    if (m->code == nullptr) throwNew("java/lang/AbstractMethodError", m->owner->name + "." + m->name + m->desc);
    if (depth_ >= 380) throw BudgetExceeded{"the stack is too deep"};
    const Code& code = *m->code;
    const std::size_t frame = static_cast<std::size_t>(code.maxLocals) + code.maxStack + 4;
    if (top_ + frame >= slab_.data() + slab_.size()) throw BudgetExceeded{"the stack is full"};
    Value* locals = top_;
    for (int i = 0; i < m->argSlots; ++i) locals[i] = args[i];
    for (std::size_t i = static_cast<std::size_t>(m->argSlots); i < frame; ++i) locals[i] = Value();
    top_ += frame;
    ++depth_;
    struct Guard {
        Vm& vm;
        Value* base;
        ~Guard() {
            vm.top_ = base;
            --vm.depth_;
        }
    } guard{*this, locals};
    return run(m, locals);
}

Value Vm::defaultReturn(char ret, const std::string& desc, Object* receiver) {
    if (ret != 'A') return Value();
    const std::size_t close = desc.find(')');
    const std::string type = close == std::string::npos ? std::string() : desc.substr(close + 1);
    if (!type.empty() && type[0] == '[') return valueOfRef(newArray('L', 0));
    const std::string name = type.size() > 2 ? type.substr(1, type.size() - 2) : std::string("java/lang/Object");
    if (receiver != nullptr && receiver->shim && receiver->shim->className == name) return valueOfRef(receiver);  // a builder gives itself back
    return valueOfRef(makeDefault(name));
}

// A call that no class of the mod, and nothing built in, answers: a call into the game, the mod loader or a library. The handlers that know one (registering something, an identifier) answer
// it; any other is recorded on the object it was made on, and answered with what a caller will not trip on.
Value Vm::shimCall(MethodRef& r, int op, Value* args) {
    const std::string owner = r.cls->name;
    for (const std::string& key : {owner + "." + r.name, "*." + r.name}) {
        if (const auto it = shimHandlers_.find(key); it != shimHandlers_.end()) {
            Value result;
            if (it->second(*this, args, r, result)) return result;
        }
    }
    Object* self = op == kStaticOp ? nullptr : args[0].a;
    std::vector<std::string> described;
    int slot = op == kStaticOp ? 0 : 1;
    for (const char k : r.params) {
        described.push_back(describe(args[slot], k));
        slot += k == 'J' || k == 'D' ? 2 : 1;
    }
    if (owner.rfind("java/", 0) == 0 || owner.rfind("javax/", 0) == 0) unsupported(owner + "." + r.name);
    if (op == kSpecialOp && r.name == "<init>") {  // the constructor of a stand-in, run for an object of the mod's or for one of its own
        ShimCall call{"<init>", std::move(described), {}, {}};
        slot = 1;
        for (const char k : r.params) {
            call.raw.push_back(args[slot]);
            call.kinds.push_back(k);
            slot += k == 'J' || k == 'D' ? 2 : 1;
        }
        shimOf(self).calls.push_back(std::move(call));
        return Value();
    }
    if (self != nullptr && (self->shim || self->cls->shim || !self->cls->file)) {
        ShimCall call{r.name, described, {}, {}};
        slot = 1;
        for (const char k : r.params) {
            call.raw.push_back(args[slot]);
            call.kinds.push_back(k);
            slot += k == 'J' || k == 'D' ? 2 : 1;
        }
        shimOf(self).calls.push_back(std::move(call));
    } else if (capture.calls.size() < 400) {
        std::string line = r.cls->simpleName() + "." + r.name + "(";
        for (std::size_t i = 0; i < described.size(); ++i) line += (i ? ", " : "") + described[i];
        capture.calls.push_back(line + ")");
    }
    return defaultReturn(r.ret, r.desc, self);
}

Value Vm::dispatch(MethodRef& r, int op, Value* args) {
    if (op == kStaticOp) {
        Class* c = r.cls;
        if (c->state != Class::State::Ready) ensureInitialized(c);
        if (r.direct == nullptr) r.direct = findMethod(c, r.name, r.desc);
        if (r.direct != nullptr) return call(r.direct, args);
        return shimCall(r, op, args);
    }
    Object* self = args[0].a;
    if (self == nullptr) throwNew("java/lang/NullPointerException", "Cannot invoke \"" + r.cls->simpleName() + "." + r.name + "()\" because value is null");
    if (op == kSpecialOp) {
        if (r.direct == nullptr) r.direct = findMethod(r.cls, r.name, r.desc);
        if (r.direct != nullptr) return call(r.direct, args);
        return shimCall(r, op, args);
    }
    Class* c = self->cls;
    Method* m;
    if (r.lastClass == c) {
        m = r.lastMethod;
    } else {
        m = findMethod(c, r.name, r.desc);
        if (m == nullptr) m = findMethod(r.cls, r.name, r.desc);
        r.lastClass = c;
        r.lastMethod = m;
    }
    if (m != nullptr) return call(m, args);
    if (self->kind == Kind::Lambda) return lambdaInvoke(self, r, args);
    return shimCall(r, op, args);
}

// A call on a lambda's object (the interface's one abstract method): the lambda's target is called with what the lambda carries, then what the caller gave, the values boxed or unboxed
// where the two sides disagree (LambdaMetafactory does the same).
Value Vm::lambdaInvoke(Object* lambda, const MethodRef& ref, Value* args) {
    MethodRef& target = *lambda->handle;
    std::vector<Value> given;  // one Value each, with the kind the caller passes it in
    std::vector<char> kinds;
    for (const Value& v : lambda->captured) given.push_back(v);
    kinds.insert(kinds.end(), lambda->str.begin(), lambda->str.end());  // (the kinds of what it carries: written when it was made)
    int slot = 1;
    for (const char k : ref.params) {
        given.push_back(args[slot]);
        kinds.push_back(k);
        slot += k == 'J' || k == 'D' ? 2 : 1;
    }
    const int kind = lambda->handleKind;
    const bool hasReceiver = kind == 5 || kind == 7 || kind == 9;
    Object* made = nullptr;
    std::vector<Value> out;
    std::size_t g = 0;
    if (kind == 8) {  // a constructor reference: the object is made, then constructed
        made = newObject(target.cls);
        if (target.cls->shim) (void)shimOf(made);
        out.push_back(valueOfRef(made));
    } else if (hasReceiver) {
        if (g < given.size()) out.push_back(given[g++]);
    }
    const auto unboxTo = [&](Value v, char have, char want) {
        if (want != 'A' && have == 'A') return v.a != nullptr ? unbox(v.a) : Value();
        if (want == 'A' && have != 'A') {
            const char type = have == 'J' ? 'J' : have == 'F' ? 'F' : have == 'D' ? 'D' : 'I';
            return valueOfRef(box(type, v));
        }
        return v;
    };
    for (const char want : target.params) {
        if (g >= given.size()) break;
        const Value v = given[g];
        const char have = g < kinds.size() ? kinds[g] : 'A';
        ++g;
        const Value adapted = unboxTo(v, have, want);
        out.push_back(adapted);
        if (want == 'J' || want == 'D') out.push_back(Value());
    }
    // `out` holds the target's arguments in frame layout, except that the two slots of a long are written above.
    const int op = kind == 6 ? kStaticOp : kind == 5 ? kVirtualOp : kind == 9 ? kInterfaceOp : kSpecialOp;
    Value result = dispatch(target, op, out.data());
    if (kind == 8) return valueOfRef(made);
    if (ref.ret != 'V' && target.ret == 'V') return Value();
    if (ref.ret == 'A' && target.ret != 'A' && target.ret != 'V') {
        const char type = target.desc.back() == 'Z' ? 'Z' : target.desc.back() == 'C' ? 'C' : target.desc.back() == 'B' ? 'B' : target.desc.back() == 'S' ? 'S' : target.ret;
        return valueOfRef(box(type, result));
    }
    if (ref.ret != 'A' && target.ret == 'A') return result.a != nullptr ? unbox(result.a) : Value();
    return result;
}

// Calls the one method of a functional object (a lambda, or a mod's object that implements the interface) with arguments one Value each.
Value Vm::callInterface(Object* fn, const std::string& name, const std::string& desc, const Value* args, int count) {
    if (fn == nullptr) throwNew("java/lang/NullPointerException");
    if (fn->kind != Kind::Lambda) {
        Method* m = findMethod(fn->cls, name, desc);
        if (m == nullptr) return Value();
        std::vector<Value> full;
        full.push_back(valueOfRef(fn));
        for (int i = 0; i < count; ++i) full.push_back(args[i]);
        return invoke(m, full.data(), count + 1);
    }
    MethodRef ref;
    ref.name = name;
    ref.desc = desc;
    parseDescriptor(desc, ref.params, ref.ret, ref.argSlots);
    Value slots[48];
    slots[0] = valueOfRef(fn);
    int k = 1;
    for (std::size_t i = 0; i < ref.params.size() && static_cast<int>(i) < count; ++i) {
        slots[k] = args[i];
        k += ref.params[i] == 'J' || ref.params[i] == 'D' ? 2 : 1;
    }
    return lambdaInvoke(fn, ref, slots);
}

Value Vm::callHandler(Object* fn, const std::vector<std::string>& names, const std::string& kinds, const Value* args) {
    if (fn == nullptr) return Value();
    const int count = static_cast<int>(kinds.size());
    if (fn->kind == Kind::Lambda) {
        // the lambda's own target does the work: the descriptor only has to say how many arguments there are, and of what kind (an object each)
        std::string desc = "(";
        for (const char k : kinds) desc += k == "A"[0] ? std::string("Ljava/lang/Object;") : std::string(1, k);
        return callInterface(fn, names.empty() ? std::string("run") : names[0], desc + ")V", args, count);
    }
    for (Class* c = fn->cls; c != nullptr; c = c->super) {
        for (auto& entry : c->methods) {
            Method* m = entry.second.get();
            if (m->isStatic() || !m->callable() || static_cast<int>(m->params.size()) != count) continue;
            if (std::find(names.begin(), names.end(), m->name) == names.end()) continue;
            std::vector<Value> full;
            full.push_back(valueOfRef(fn));
            for (int i = 0; i < count; ++i) full.push_back(args[i]);
            return invoke(m, full.data(), count + 1);
        }
    }
    return Value();
}

// ---- invokedynamic -----------------------------------------------------------------------------------------------------------------------------------------------

Value Vm::invokeDynamic(Class* cls, int index, Value* sp, int& slots, char& ret) {
    IndySite* site = static_cast<IndySite*>(cls->cache[static_cast<std::size_t>(index)]);
    if (site == nullptr) {
        sites_.push_back(std::make_unique<IndySite>());
        site = sites_.back().get();
        cls->cache[static_cast<std::size_t>(index)] = site;
        const ClassFile& cf = *cls->file;
        const CpEntry& e = cf.cp[static_cast<std::size_t>(index)];
        const CpEntry& nt = cf.cp[e.b];
        const std::string name = cf.utf8(nt.a), desc = cf.utf8(nt.b);
        parseDescriptor(desc, site->params, site->ret, site->argSlots);
        site->types = paramTypes(desc);
        const Bootstrap& bsm = cf.bootstraps.at(e.a);
        const CpEntry& handle = cf.cp[bsm.method];
        const CpEntry& ref = cf.cp[handle.b];
        const std::string bsmOwner = cf.className(ref.a), bsmName = cf.utf8(cf.cp[ref.b].a);
        if (bsmOwner == "java/lang/invoke/LambdaMetafactory") {
            const CpEntry& impl = cf.cp[bsm.args.at(1)];  // the MethodHandle
            site->kind = IndySite::Kind::Lambda;
            site->handleKind = impl.a;
            site->handle = &methodRef(cls, impl.b);
            const std::size_t close = desc.find(')');
            site->iface = loadClass(desc.substr(close + 2, desc.size() - close - 3));
        } else if (bsmOwner == "java/lang/invoke/StringConcatFactory") {
            site->kind = IndySite::Kind::Concat;
            if (bsmName == "makeConcatWithConstants") {
                site->recipe = cf.utf8(cf.cp[bsm.args.at(0)].a);
                for (std::size_t i = 1; i < bsm.args.size(); ++i) {
                    const CpEntry& c = cf.cp[bsm.args[i]];
                    site->constants.push_back(intern(c.tag == kString ? cf.utf8(c.a) : std::string()));
                }
            } else {
                site->recipe.assign(site->params.size(), '\x01');
            }
        } else if (bsmOwner == "java/lang/runtime/ObjectMethods") {
            site->kind = name == "toString" ? IndySite::Kind::RecordToString : name == "equals" ? IndySite::Kind::RecordEquals : IndySite::Kind::RecordHash;
            site->record = loadClass(cf.className(bsm.args.at(0)));
            std::string names = cf.utf8(cf.cp[bsm.args.at(1)].a);
            for (std::size_t from = 0; from < names.size();) {
                const std::size_t semi = names.find(';', from);
                site->components.push_back(names.substr(from, semi == std::string::npos ? std::string::npos : semi - from));
                if (semi == std::string::npos) break;
                from = semi + 1;
            }
        } else {
            site->why = bsmOwner + "." + bsmName;
        }
    }
    slots = site->argSlots;
    ret = site->ret;
    Value* args = sp - slots;
    switch (site->kind) {
        case IndySite::Kind::Lambda: {
            Object* o = newObject(site->iface);
            o->kind = Kind::Lambda;
            o->handle = site->handle;
            o->handleKind = site->handleKind;
            int slot = 0;
            for (const char k : site->params) {
                o->captured.push_back(args[slot]);
                o->str.push_back(k);
                slot += k == 'J' || k == 'D' ? 2 : 1;
            }
            return valueOfRef(o);
        }
        case IndySite::Kind::Concat: {
            std::string out;
            std::size_t arg = 0, constant = 0;
            int slot = 0;
            std::vector<std::pair<Value, char>> values;
            for (std::size_t i = 0; i < site->params.size(); ++i) {
                values.emplace_back(args[slot], site->types[i]);
                slot += site->params[i] == 'J' || site->params[i] == 'D' ? 2 : 1;
            }
            for (const char c : site->recipe) {
                if (c == '\x01') {
                    if (arg >= values.size()) continue;
                    const auto& [v, type] = values[arg++];
                    switch (type) {
                        case 'Z': out += v.i != 0 ? "true" : "false"; break;
                        case 'C': appendUtf8(out, static_cast<unsigned>(v.i) & 0xFFFF); break;
                        case 'J': out += std::to_string(v.j); break;
                        case 'F': out += javaDouble(static_cast<double>(v.f)); break;
                        case 'D': out += javaDouble(v.d); break;
                        case 'L': out += toJavaString(v.a); break;
                        default: out += std::to_string(v.i); break;
                    }
                } else if (c == '\x02') {
                    if (constant < site->constants.size()) out += site->constants[constant++]->str;
                } else {
                    out.push_back(c);
                }
            }
            return valueOfRef(newString(std::move(out)));
        }
        case IndySite::Kind::RecordToString:
        case IndySite::Kind::RecordEquals:
        case IndySite::Kind::RecordHash: {
            Object* self = args[0].a;
            if (self == nullptr) throwNew("java/lang/NullPointerException");
            const auto fieldText = [&](Object* o, const std::string& component) {
                const auto it = o->cls->fields.find(component);
                if (it == o->cls->fields.end()) return std::string("?");
                const Value v = o->fields[static_cast<std::size_t>(it->second.index)];
                const char kind = kindOfType(it->second.desc[0]);
                if (kind == 'A') return toJavaString(v.a);
                if (it->second.desc[0] == 'Z') return std::string(v.i != 0 ? "true" : "false");
                return describe(v, kind);
            };
            if (site->kind == IndySite::Kind::RecordToString) {
                std::string out = self->cls->simpleName() + "[";
                for (std::size_t i = 0; i < site->components.size(); ++i) out += (i ? ", " : "") + site->components[i] + "=" + fieldText(self, site->components[i]);
                return valueOfRef(newString(out + "]"));
            }
            if (site->kind == IndySite::Kind::RecordHash) {
                std::uint32_t h = 0;
                for (const std::string& c : site->components) h = h * 31 + static_cast<std::uint32_t>(std::hash<std::string>{}(fieldText(self, c)));
                return valueOfInt(static_cast<std::int32_t>(h));
            }
            Object* other = args[1].a;
            if (other == nullptr || other->cls != self->cls) return valueOfInt(0);
            for (const std::string& c : site->components) {
                if (fieldText(self, c) != fieldText(other, c)) return valueOfInt(0);
            }
            return valueOfInt(1);
        }
        default: break;
    }
    unsupported("invokedynamic " + site->why);
    throwNew("java/lang/BootstrapMethodError", site->why);
}

}  // namespace falcon::jvm
