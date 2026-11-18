// The natives of net.falcon.Host (modapi/src/net/falcon/Host.java): what the client lends the mods, and what the mods hand back. Every one reads the HostState the client filled in before it called
// into the mod, or writes into the capture the client reads after.
#include "falcon/jvm/vm.hpp"

#include "falcon/assets/image.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>

namespace falcon::jvm {
namespace {

Value ref(Object* o) { return valueOfRef(o); }

Object* stringArray(Vm& v, const std::vector<std::string>& items) {
    Object* a = v.newArray('L', static_cast<int>(items.size()));
    for (std::size_t i = 0; i < items.size(); ++i) a->elems[i] = ref(v.newString(items[i]));
    return a;
}

Object* doubleArray(Vm& v, std::initializer_list<double> values) {
    Object* a = v.newArray('D', static_cast<int>(values.size()));
    std::size_t i = 0;
    for (const double d : values) a->elems[i++] = valueOfDouble(d);
    return a;
}

std::string textOf(Vm& v, Object* o) { return o == nullptr ? std::string() : v.str(o); }

namespace fs = std::filesystem;

std::string lowered(std::string s) {
#ifdef _WIN32
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return s;
}

bool inside(const fs::path& child, const fs::path& root) {
    if (root.empty()) return false;
    std::string c = lowered(child.lexically_normal().generic_string()), r = lowered(root.lexically_normal().generic_string());
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    return c == r || c.rfind(r + "/", 0) == 0;
}

// A path a mod names, made absolute (relative ones are in the game folder) and checked: the game folder may be read and written, the mods folder only read. Anything else is not there.
bool allowedPath(Vm& v, const std::string& text, bool write, fs::path& out) {
    fs::path p(text);
    if (p.is_relative()) p = fs::path(v.host.gameDir) / p;
    out = p.lexically_normal();
    return inside(out, v.host.gameDir) || (!write && inside(out, v.host.modsDir));
}

Object* byteArray(Vm& v, const std::vector<char>& bytes) {
    Object* a = v.newArray('B', static_cast<int>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) a->elems[i] = valueOfInt(static_cast<signed char>(bytes[i]));
    return a;
}

}  // namespace

void installHostNatives(Vm& vm) {
    const std::string H = "net/falcon/Host";
    const auto nat = [&](const char* name, const char* desc, NativeFn fn) { vm.defineHostNative(H, name, desc, std::move(fn)); };

    nat("gameDir", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.gameDir)); });
    nat("modsDir", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.modsDir)); });
    nat("loadedMods", "()[Ljava/lang/String;", [](Vm& v, Value*) { return ref(stringArray(v, v.host.loadedMods)); });
    nat("log", "(ILjava/lang/String;)V", [](Vm& v, Value* a) {
        static const char* const kLevels[] = {"debug", "info", "warn", "error"};
        v.note(std::string("[") + kLevels[std::clamp(a[0].i, 0, 3)] + "] " + textOf(v, a[1].a));
        return Value();
    });
    nat("translate", "(Ljava/lang/String;)Ljava/lang/String;", [](Vm& v, Value* a) {
        const std::string key = textOf(v, a[0].a);
        return ref(v.newString(v.host.translate ? v.host.translate(key) : key));
    });
    nat("textWidth", "(Ljava/lang/String;)I", [](Vm& v, Value* a) {
        const std::string text = textOf(v, a[0].a);
        return valueOfInt(v.host.textWidth ? v.host.textWidth(text) : static_cast<int>(text.size()) * 6);
    });
    nat("player", "()[D", [](Vm& v, Value*) {
        const HostState& h = v.host;
        return ref(doubleArray(v, {h.x, h.y, h.z, h.yaw, h.pitch, h.eyeHeight, static_cast<double>(h.health), static_cast<double>(h.food), static_cast<double>(h.playerFlags)}));
    });
    nat("window", "()[D", [](Vm& v, Value*) {
        const HostState& h = v.host;
        return ref(doubleArray(v, {static_cast<double>(h.guiWidth), static_cast<double>(h.guiHeight), static_cast<double>(h.framebufferWidth), static_cast<double>(h.framebufferHeight), static_cast<double>(h.guiScale),
                                   h.mouseX, h.mouseY, h.gameTime}));
    });
    nat("camera", "()[D", [](Vm& v, Value*) {
        const HostState& h = v.host;
        return ref(doubleArray(v, {h.camX, h.camY, h.camZ, h.camYaw, h.camPitch, h.camFov}));
    });
    nat("dimension", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.dimension)); });
    nat("worldName", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.worldName)); });
    nat("worldKind", "()I", [](Vm& v, Value*) { return valueOfInt(v.host.worldKind); });
    nat("playerName", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.playerName)); });
    nat("playerUuid", "()Ljava/lang/String;", [](Vm& v, Value*) { return ref(v.newString(v.host.playerUuid)); });
    nat("blockAt", "(III)Ljava/lang/String;", [](Vm& v, Value* a) { return ref(v.newString(v.host.blockAt ? v.host.blockAt(a[0].i, a[1].i, a[2].i) : std::string("minecraft:air"))); });
    nat("blockFlags", "(III)I", [](Vm& v, Value* a) { return valueOfInt(v.host.blockFlags ? v.host.blockFlags(a[0].i, a[1].i, a[2].i) : 0); });
    nat("keyDown", "(I)Z", [](Vm& v, Value* a) { return valueOfInt(v.host.keyDown && v.host.keyDown(a[0].i) ? 1 : 0); });
    nat("chat", "(Ljava/lang/String;)V", [](Vm& v, Value* a) {
        if (v.capture.chat.size() < 200) v.capture.chat.push_back(textOf(v, a[0].a));
        return Value();
    });
    nat("overlay", "(Ljava/lang/String;)V", [](Vm& v, Value* a) {
        if (v.capture.overlay.size() < 20) v.capture.overlay.push_back(textOf(v, a[0].a));
        return Value();
    });
    nat("sendChat", "(Ljava/lang/String;)V", [](Vm& v, Value* a) {
        if (v.host.sendChat) v.host.sendChat(textOf(v, a[0].a));
        return Value();
    });
    nat("sendCommand", "(Ljava/lang/String;)V", [](Vm& v, Value* a) {
        if (v.host.sendCommand) v.host.sendCommand(textOf(v, a[0].a));
        return Value();
    });
    nat("openScreen", "(ZLjava/lang/String;)V", [](Vm& v, Value* a) {
        v.capture.screenOpen = a[0].i != 0;
        v.capture.screenTitle = textOf(v, a[1].a);
        return Value();
    });
    nat("consumeClick", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { return valueOfInt(v.host.consumeClick && v.host.consumeClick(textOf(v, a[0].a)) ? 1 : 0); });
    nat("isDown", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { return valueOfInt(v.host.isDown && v.host.isDown(textOf(v, a[0].a)) ? 1 : 0); });

    // ---- what the mod adds ----
    nat("registerKey", "(Ljava/lang/String;Ljava/lang/String;II)V", [](Vm& v, Value* a) {
        ModKeyDef k;
        k.name = textOf(v, a[0].a);
        k.category = textOf(v, a[1].a);
        k.mouse = a[2].i == 1;
        k.key = a[3].i;
        v.capture.content.keys.push_back(std::move(k));
        return Value();
    });
    nat("registerItem", "(Ljava/lang/String;IIZLjava/lang/String;IFLjava/lang/String;)V", [](Vm& v, Value* a) {
        ModItemDef d;
        d.id = textOf(v, a[0].a);
        d.kind = "Item";
        d.stacksTo = a[1].i;
        d.durability = a[2].i;
        d.fireResistant = a[3].i != 0;
        d.rarity = textOf(v, a[4].a);
        if (d.rarity == "common") d.rarity.clear();
        d.nutrition = a[5].i;
        d.food = d.nutrition > 0;
        d.saturation = a[6].f;
        d.block = textOf(v, a[7].a);
        d.tab = d.block.empty() ? "minecraft:ingredients" : "minecraft:building_blocks";
        v.capture.content.items.push_back(std::move(d));
        return Value();
    });
    nat("registerBlock", "(Ljava/lang/String;FFIZZLjava/lang/String;)V", [](Vm& v, Value* a) {
        ModBlockDef b;
        b.id = textOf(v, a[0].a);
        b.kind = "Block";
        b.hardness = a[1].f;
        b.resistance = a[2].f;
        b.light = a[3].i;
        b.requiresTool = a[4].i != 0;
        b.noOcclusion = a[5].i != 0;
        b.sound = textOf(v, a[6].a);
        v.capture.content.blocks.push_back(std::move(b));
        return Value();
    });
    nat("registered", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", [](Vm& v, Value* a) {
        Registration r;
        r.registry = textOf(v, a[0].a);
        r.id = textOf(v, a[1].a);
        r.kind = textOf(v, a[2].a);
        r.text = r.kind;
        v.capture.registrations.push_back(std::move(r));
        return Value();
    });
    nat("creativeTab", "(Ljava/lang/String;Ljava/lang/String;)V", [](Vm& v, Value* a) {
        const std::string tab = textOf(v, a[0].a), item = textOf(v, a[1].a);
        for (ModItemDef& d : v.capture.content.items) {
            if (d.id == item) d.tab = tab;
        }
        return Value();
    });

    // ---- what the mod draws on the screen ----
    const auto draw = [](Vm& v, DrawCmd c) {
        if (v.capture.draw.size() < 20000) v.capture.draw.push_back(std::move(c));
    };
    nat("drawFill", "(FFFFI)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Fill;
        c.x0 = a[0].f; c.y0 = a[1].f; c.x1 = a[2].f; c.y1 = a[3].f;
        c.color = static_cast<std::uint32_t>(a[4].i);
        draw(v, std::move(c));
        return Value();
    });
    nat("drawGradient", "(FFFFII)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Gradient;
        c.x0 = a[0].f; c.y0 = a[1].f; c.x1 = a[2].f; c.y1 = a[3].f;
        c.color = static_cast<std::uint32_t>(a[4].i);
        c.color2 = static_cast<std::uint32_t>(a[5].i);
        draw(v, std::move(c));
        return Value();
    });
    nat("drawText", "(Ljava/lang/String;FFIZZF)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Text;
        c.text = textOf(v, a[0].a);
        c.x0 = a[1].f; c.y0 = a[2].f;
        c.color = static_cast<std::uint32_t>(a[3].i);
        if ((c.color >> 24) == 0) c.color |= 0xFF000000u;
        c.shadow = a[4].i != 0;
        c.centered = a[5].i != 0;
        c.scale = a[6].f;
        draw(v, std::move(c));
        return Value();
    });
    nat("drawTexture", "(Ljava/lang/String;FFFFFFFFIII)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Blit;
        c.text = textOf(v, a[0].a);
        c.x0 = a[1].f; c.y0 = a[2].f; c.x1 = a[3].f; c.y1 = a[4].f;
        c.u = a[5].f; c.v = a[6].f; c.regionW = a[7].f; c.regionH = a[8].f;
        c.texW = a[9].i; c.texH = a[10].i;
        c.color = static_cast<std::uint32_t>(a[11].i);
        draw(v, std::move(c));
        return Value();
    });
    nat("drawSprite", "(Ljava/lang/String;FFFFI)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Blit;
        c.text = "sprite:" + textOf(v, a[0].a);
        c.x0 = a[1].f; c.y0 = a[2].f; c.x1 = a[3].f; c.y1 = a[4].f;
        c.color = static_cast<std::uint32_t>(a[5].i);
        draw(v, std::move(c));
        return Value();
    });
    nat("drawItem", "(Ljava/lang/String;FF)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = DrawCmd::Kind::Item;
        c.text = textOf(v, a[0].a);
        c.x0 = a[1].f; c.y0 = a[2].f;
        draw(v, std::move(c));
        return Value();
    });
    nat("scissor", "(ZFFFF)V", [draw](Vm& v, Value* a) {
        DrawCmd c;
        c.kind = a[0].i != 0 ? DrawCmd::Kind::ScissorOn : DrawCmd::Kind::ScissorOff;
        c.x0 = a[1].f; c.y0 = a[2].f; c.x1 = a[3].f; c.y1 = a[4].f;
        draw(v, std::move(c));
        return Value();
    });

    // ---- what the mod draws in the world ----
    nat("submit", "([FIIIFLjava/lang/String;)V", [](Vm& v, Value* a) {
        Object* data = a[0].a;
        if (data == nullptr || v.capture.submits.size() > 4000) return Value();
        WorldSubmit s;
        const int count = std::min<int>(a[1].i, static_cast<int>(data->elems.size() / 9));
        s.vertices.resize(static_cast<std::size_t>(count) * 9);
        for (std::size_t i = 0; i < s.vertices.size(); ++i) s.vertices[i] = data->elems[i].f;
        s.topology = a[2].i;
        s.state = a[3].i;
        s.lineWidth = a[4].f;
        s.texture = textOf(v, a[5].a);
        v.capture.submits.push_back(std::move(s));
        return Value();
    });

    // ---- files ----
    const std::string F = "net/falcon/Fs";
    const auto fsnat = [&](const char* name, const char* desc, NativeFn fn) { vm.defineHostNative(F, name, desc, std::move(fn)); };
    fsnat("exists", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { fs::path p; std::error_code ec; return valueOfInt(allowedPath(v, textOf(v, a[0].a), false, p) && fs::exists(p, ec) ? 1 : 0); });
    fsnat("isDirectory", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { fs::path p; std::error_code ec; return valueOfInt(allowedPath(v, textOf(v, a[0].a), false, p) && fs::is_directory(p, ec) ? 1 : 0); });
    fsnat("isRegularFile", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { fs::path p; std::error_code ec; return valueOfInt(allowedPath(v, textOf(v, a[0].a), false, p) && fs::is_regular_file(p, ec) ? 1 : 0); });
    fsnat("read", "(Ljava/lang/String;)[B", [](Vm& v, Value* a) {
        fs::path p;
        if (!allowedPath(v, textOf(v, a[0].a), false, p)) return ref(nullptr);
        std::ifstream in(p, std::ios::binary);
        if (!in) return ref(nullptr);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return ref(byteArray(v, bytes));
    });
    fsnat("write", "(Ljava/lang/String;[BZ)Z", [](Vm& v, Value* a) {
        fs::path p;
        if (!allowedPath(v, textOf(v, a[0].a), true, p) || a[1].a == nullptr) return valueOfInt(0);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | (a[2].i != 0 ? std::ios::app : std::ios::trunc));
        if (!out) return valueOfInt(0);
        std::vector<char> bytes(a[1].a->elems.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(a[1].a->elems[i].i);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return valueOfInt(out.good() ? 1 : 0);
    });
    fsnat("list", "(Ljava/lang/String;)[Ljava/lang/String;", [](Vm& v, Value* a) {
        fs::path p;
        std::error_code ec;
        if (!allowedPath(v, textOf(v, a[0].a), false, p) || !fs::is_directory(p, ec)) return ref(nullptr);
        std::vector<std::string> names;
        for (const auto& e : fs::directory_iterator(p, ec)) names.push_back(e.path().filename().generic_string());
        std::sort(names.begin(), names.end());
        return ref(stringArray(v, names));
    });
    fsnat("mkdirs", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { fs::path p; std::error_code ec; return valueOfInt(allowedPath(v, textOf(v, a[0].a), true, p) && (fs::create_directories(p, ec) || fs::is_directory(p, ec)) ? 1 : 0); });
    fsnat("delete", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) { fs::path p; std::error_code ec; return valueOfInt(allowedPath(v, textOf(v, a[0].a), true, p) && fs::remove(p, ec) ? 1 : 0); });
    fsnat("move", "(Ljava/lang/String;Ljava/lang/String;Z)Z", [](Vm& v, Value* a) {
        fs::path from, to;
        std::error_code ec;
        if (!allowedPath(v, textOf(v, a[0].a), true, from) || !allowedPath(v, textOf(v, a[1].a), true, to)) return valueOfInt(0);
        fs::create_directories(to.parent_path(), ec);
        if (a[2].i != 0) fs::remove(to, ec);
        fs::rename(from, to, ec);
        return valueOfInt(ec ? 0 : 1);
    });
    fsnat("copy", "(Ljava/lang/String;Ljava/lang/String;Z)Z", [](Vm& v, Value* a) {
        fs::path from, to;
        std::error_code ec;
        if (!allowedPath(v, textOf(v, a[0].a), false, from) || !allowedPath(v, textOf(v, a[1].a), true, to)) return valueOfInt(0);
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(from, to, a[2].i != 0 ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
        return valueOfInt(ec ? 0 : 1);
    });
    fsnat("size", "(Ljava/lang/String;)J", [](Vm& v, Value* a) { fs::path p; std::error_code ec; const auto n = allowedPath(v, textOf(v, a[0].a), false, p) ? fs::file_size(p, ec) : 0; return valueOfLong(ec ? 0 : static_cast<std::int64_t>(n)); });
    fsnat("lastModified", "(Ljava/lang/String;)J", [](Vm& v, Value* a) {
        fs::path p;
        std::error_code ec;
        if (!allowedPath(v, textOf(v, a[0].a), false, p)) return valueOfLong(0);
        const auto t = fs::last_write_time(p, ec);
        if (ec) return valueOfLong(0);
        return valueOfLong(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch()).count());
    });
    fsnat("absolute", "(Ljava/lang/String;)Ljava/lang/String;", [](Vm& v, Value* a) {
        fs::path p(textOf(v, a[0].a));
        if (p.is_relative()) p = fs::path(v.host.gameDir) / p;
        return ref(v.newString(p.lexically_normal().generic_string()));
    });

    // ---- reflection (for the reflective Gson of modapi: fields by name, objects made without their constructor being asked, enum constants) ----
    const std::string R = "net/falcon/Reflect";
    const auto rnat = [&](const char* name, const char* desc, NativeFn fn) { vm.defineHostNative(R, name, desc, std::move(fn)); };
    rnat("fields", "(Ljava/lang/String;)[Ljava/lang/String;", [](Vm& v, Value* a) {
        // the instance fields that are not transient, the class's own first and then each superclass's, in the order they were declared: "name\0descriptor\0signature"
        std::vector<std::string> out;
        for (Class* c = v.loadClass(textOf(v, a[0].a)); c != nullptr && !c->shim; c = c->super) {
            for (const std::string& name : c->fieldOrder) {
                const FieldInfo& f = c->fields.at(name);
                if (f.isStatic || (f.access & 0x0080) != 0 || (f.access & 0x1000) != 0) continue;  // static, transient, synthetic
                out.push_back(name + '\0' + f.desc + '\0' + f.signature);
            }
        }
        return ref(stringArray(v, out));
    });
    const auto fieldOf = [](Vm& v, Object* o, const std::string& name) -> const FieldInfo* {
        for (Class* c = o->cls; c != nullptr; c = c->super) {
            if (const auto it = c->fields.find(name); it != c->fields.end() && !it->second.isStatic) return &it->second;
        }
        (void)v;
        return nullptr;
    };
    rnat("get", "(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/Object;", [fieldOf](Vm& v, Value* a) {
        const FieldInfo* f = a[0].a == nullptr ? nullptr : fieldOf(v, a[0].a, textOf(v, a[1].a));
        if (f == nullptr || static_cast<std::size_t>(f->index) >= a[0].a->fields.size()) return ref(nullptr);
        const Value raw = a[0].a->fields[static_cast<std::size_t>(f->index)];
        switch (f->desc[0]) {
            case 'I': case 'S': case 'B': return ref(v.box(f->desc[0], raw));
            case 'C': case 'Z': return ref(v.box(f->desc[0], raw));
            case 'J': case 'F': case 'D': return ref(v.box(f->desc[0], raw));
            default: return ref(raw.a);
        }
    });
    rnat("set", "(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/Object;)V", [fieldOf](Vm& v, Value* a) {
        const FieldInfo* f = a[0].a == nullptr ? nullptr : fieldOf(v, a[0].a, textOf(v, a[1].a));
        if (f == nullptr || static_cast<std::size_t>(f->index) >= a[0].a->fields.size()) return Value();
        const char k = f->desc[0];
        const bool primitive = k != 'L' && k != '[';
        a[0].a->fields[static_cast<std::size_t>(f->index)] = primitive && a[2].a != nullptr ? v.unbox(a[2].a) : (primitive ? Value() : a[2]);
        return Value();
    });
    rnat("newInstance", "(Ljava/lang/String;)Ljava/lang/Object;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        if (c->shim) return ref(nullptr);
        v.ensureInitialized(c);
        Object* o = v.newObject(c);
        if (Method* init = v.findMethod(c, "<init>", "()V")) {
            Value self = valueOfRef(o);
            v.invoke(init, &self, 1);
        }
        return ref(o);
    });
    rnat("enumValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        if (c->shim) return ref(nullptr);
        v.ensureInitialized(c);
        const auto it = c->fields.find(textOf(v, a[1].a));
        if (it == c->fields.end() || !it->second.isStatic || static_cast<std::size_t>(it->second.index) >= c->statics.size()) return ref(nullptr);
        return ref(c->statics[static_cast<std::size_t>(it->second.index)].a);
    });
    rnat("isEnum", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        return valueOfInt(c->super != nullptr && c->super->name == "java/lang/Enum" ? 1 : 0);
    });
    rnat("isRecord", "(Ljava/lang/String;)Z", [](Vm& v, Value* a) {
        Class* c = v.loadClass(textOf(v, a[0].a));
        return valueOfInt(c->super != nullptr && c->super->name == "java/lang/Record" ? 1 : 0);
    });
    rnat("classSignature", "(Ljava/lang/Object;)Ljava/lang/String;", [](Vm& v, Value* a) { return ref(v.newString(a[0].a == nullptr || a[0].a->cls == nullptr || !a[0].a->cls->file ? std::string() : a[0].a->cls->file->signature)); });
    rnat("className", "(Ljava/lang/Object;)Ljava/lang/String;", [](Vm& v, Value* a) { return ref(v.newString(a[0].a == nullptr || a[0].a->cls == nullptr ? std::string() : a[0].a->cls->name)); });

    // ---- regular expressions ----
    // (the patterns are kept by handle for the life of the VM: a mod compiles a handful)
    auto patterns = std::make_shared<std::vector<std::regex>>();
    vm.defineHostNative("net/falcon/Regex", "compile", "(Ljava/lang/String;I)I", [patterns](Vm& v, Value* a) {
        auto flags = std::regex::ECMAScript;
        if ((a[1].i & 1) != 0) flags |= std::regex::icase;
        if ((a[1].i & 2) != 0) flags |= std::regex::multiline;
        try {
            patterns->emplace_back(textOf(v, a[0].a), flags);
        } catch (const std::regex_error&) {
            return valueOfInt(-1);
        }
        return valueOfInt(static_cast<int>(patterns->size()) - 1);
    });
    vm.defineHostNative("net/falcon/Regex", "find", "(ILjava/lang/String;IZ)[I", [patterns](Vm& v, Value* a) {
        if (a[0].i < 0 || static_cast<std::size_t>(a[0].i) >= patterns->size()) return ref(nullptr);
        const std::string text = textOf(v, a[1].a);
        const std::size_t from = static_cast<std::size_t>(std::clamp(a[2].i, 0, static_cast<int>(text.size())));
        std::smatch m;
        const auto begin = text.begin() + static_cast<std::ptrdiff_t>(from);
        const bool ok = a[3].i != 0 ? std::regex_match(text.begin(), text.end(), m, (*patterns)[static_cast<std::size_t>(a[0].i)])
                                    : std::regex_search(begin, text.end(), m, (*patterns)[static_cast<std::size_t>(a[0].i)], from > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default);
        if (!ok) return ref(nullptr);
        Object* out = v.newArray('I', static_cast<int>(m.size()) * 2);
        for (std::size_t i = 0; i < m.size(); ++i) {
            out->elems[i * 2] = valueOfInt(m[i].matched ? static_cast<int>(m[i].first - text.begin()) : -1);
            out->elems[i * 2 + 1] = valueOfInt(m[i].matched ? static_cast<int>(m[i].second - text.begin()) : -1);
        }
        return ref(out);
    });

    installReflectNatives(vm);

    // ---- pictures ----
    nat("decodeImageSize", "([B)[I", [](Vm& v, Value* a) {
        Object* bytes = a[0].a;
        if (bytes == nullptr) return ref(nullptr);
        std::vector<unsigned char> raw(bytes->elems.size());
        for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<unsigned char>(bytes->elems[i].i);
        const auto image = decodePng(raw);
        if (!image) return ref(nullptr);
        Object* size = v.newArray('I', 2);
        size->elems[0] = valueOfInt(image->width);
        size->elems[1] = valueOfInt(image->height);
        return ref(size);
    });
    nat("decodeImage", "([B[I)V", [](Vm&, Value* a) {
        Object* bytes = a[0].a;
        Object* pixels = a[1].a;
        if (bytes == nullptr || pixels == nullptr) return Value();
        std::vector<unsigned char> raw(bytes->elems.size());
        for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<unsigned char>(bytes->elems[i].i);
        const auto image = decodePng(raw);
        if (!image) return Value();
        const int count = std::min<int>(static_cast<int>(pixels->elems.size()), image->width * image->height);
        for (int i = 0; i < count; ++i) {  // (the game keeps ABGR: the red in the low byte)
            const unsigned char* p = image->rgba.data() + static_cast<std::size_t>(i) * 4;
            pixels->elems[static_cast<std::size_t>(i)] = valueOfInt(static_cast<std::int32_t>((static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0]));
        }
        return Value();
    });
}

}  // namespace falcon::jvm
