// Runs the start-up code of Fabric and Quilt mods through the VM (jvm/vm.hpp) and says what they did: falcon_jvm_run <mod.jar> [more.jar ...] [--verbose]
// With --refs it lists instead every method and field of a class outside the mod and outside java/ that the mod's classes name (what its API surface is), most used class first; --jdk-gaps lists the
// JDK calls the VM cannot answer; --sim <mod.jar> <game folder> starts the mod on the platform, opens a world around a player and runs ticks and frames, printing what the mod did.
#include "falcon/assets/zip_archive.hpp"
#include "falcon/jvm/class_file.hpp"
#include "falcon/jvm/mod_runtime.hpp"
#include "falcon/jvm/modapi.hpp"
#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace {

// gaps: only java/ and javax/ references, and only those this VM cannot answer (a class it does not have, or a method of one it has); api: the references to the game, the loader and libraries that the platform jar
// (modapi) does not have yet
int listReferences(const char* path, bool gaps, bool api = false) {
    auto zip = falcon::ZipArchive::open(path);
    if (!zip) {
        std::fprintf(stderr, "not a jar: %s\n", path);
        return 1;
    }
    std::set<std::string> own;  // the classes the jar has
    for (const std::string& name : zip->entryNames()) {
        if (name.size() > 6 && name.compare(name.size() - 6, 6, ".class") == 0) own.insert(name.substr(0, name.size() - 6));
    }
    falcon::jvm::Vm vm;
    if (api) {
        if (auto platform = falcon::jvm::modApiJar()) vm.addPlatformJar(platform);
        falcon::jvm::installHostNatives(vm);
    }
    std::map<std::string, std::map<std::string, int>> refs;  // class -> "name desc" -> uses
    for (const std::string& name : own) {
        const auto bytes = zip->read(name + ".class");
        if (!bytes) continue;
        const auto cf = falcon::jvm::ClassFile::parse(bytes->data(), bytes->size());
        if (!cf) continue;
        for (const falcon::jvm::CpEntry& e : cf->cp) {
            if (e.tag != falcon::jvm::kMethodref && e.tag != falcon::jvm::kInterfaceMethodref && e.tag != falcon::jvm::kFieldref) continue;
            const std::string& owner = cf->className(e.a);
            const bool jdk = owner.rfind("java/", 0) == 0 || owner.rfind("javax/", 0) == 0;
            if (owner.empty() || owner[0] == '[' || own.count(owner) != 0 || (!api && jdk != gaps) || (api && jdk)) continue;
            const falcon::jvm::CpEntry& nt = cf->cp[e.b];
            const std::string key = std::string(e.tag == falcon::jvm::kFieldref ? "field " : "") + cf->utf8(nt.a) + " " + cf->utf8(nt.b);
            if (api) {
                falcon::jvm::Class* c = vm.loadClass(owner);
                if (!c->shim) {  // the class is there: the member too?
                    bool have = false;
                    if (e.tag == falcon::jvm::kFieldref) {
                        for (falcon::jvm::Class* k = c; k != nullptr && !have; k = k->super) have = k->fields.count(cf->utf8(nt.a)) != 0;
                    } else if (cf->utf8(nt.a) == "<init>") {
                        have = c->methods.count(cf->utf8(nt.a) + cf->utf8(nt.b)) != 0;
                    } else {
                        const std::string key = cf->utf8(nt.a) + cf->utf8(nt.b);
                        const std::function<bool(falcon::jvm::Class*)> declared = [&](falcon::jvm::Class* k) -> bool {  // (declared anywhere above it, abstract or not)
                            if (k == nullptr) return false;
                            if (k->methods.count(key) != 0) return true;
                            for (falcon::jvm::Class* i : k->interfaces) {
                                if (declared(i)) return true;
                            }
                            return declared(k->super);
                        };
                        have = declared(c);
                    }
                    if (have) continue;
                }
            }
            if (gaps) {
                falcon::jvm::Class* c = vm.loadClass(owner);
                if (!c->shim && e.tag != falcon::jvm::kFieldref && (cf->utf8(nt.a) == "<init>" || vm.findMethod(c, cf->utf8(nt.a), cf->utf8(nt.b)) != nullptr)) continue;
                if (!c->shim && e.tag == falcon::jvm::kFieldref) continue;
            }
            ++refs[owner][key];
        }
    }
    std::vector<std::pair<int, std::string>> order;
    for (const auto& [owner, members] : refs) {
        int total = 0;
        for (const auto& [m, n] : members) total += n;
        order.emplace_back(total, owner);
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    for (const auto& [total, owner] : order) {
        std::printf("%s  (%d uses, %zu members)\n", owner.c_str(), total, refs[owner].size());
        for (const auto& [member, n] : refs[owner]) std::printf("    %s  x%d\n", member.c_str(), n);
    }
    return 0;
}

int simulate(const char* jar, const char* gameDir, int ticks, const std::vector<std::string>& commands) {
    falcon::jvm::RunOptions options;
    options.gameDir = gameDir;
    options.modsDir = std::string(gameDir) + "/mods";
    const char nul = 0;
    options.loadedMods.push_back(std::string("wwaypoints") + nul + "0.9.0" + nul + "wWaypoints");
    falcon::jvm::ModInstance mod(jar, options);
    std::printf("start-up: %.1f ms, %llu instructions, platform %s\n", mod.report().milliseconds, static_cast<unsigned long long>(mod.report().instructions), mod.onPlatform() ? "yes" : "no");
    falcon::jvm::HostState& h = mod.host();
    h.dimension = "minecraft:overworld";
    h.worldName = "New World";
    h.playerName = "Tester";
    h.playerUuid = "11111111-2222-3333-4444-555555555555";
    h.x = 10.5;
    h.y = 70;
    h.z = 10.5;
    h.camX = h.x;
    h.camY = h.y + 1.62;
    h.camZ = h.z;
    h.camYaw = 0;
    h.camPitch = 10;
    h.blockAt = [](int, int y, int) { return std::string(y < 64 ? "minecraft:stone" : "minecraft:air"); };
    h.blockFlags = [](int, int y, int) { return y < 64 ? 7 : 1; };
    h.textWidth = [](const std::string& t) { return static_cast<int>(t.size()) * 6; };
    h.translate = [](const std::string& k) { return k; };
    mod.clientStarted();
    {
        mod.joined();
        for (const std::string& said : mod.tick()) std::printf("chat: %s\n", said.c_str());
    }
    std::string pendingClick;
    h.consumeClick = [&pendingClick](const std::string& name) {
        if (name != pendingClick) return false;
        pendingClick.clear();
        return true;
    };
    h.isDown = [](const std::string&) { return false; };
    for (const std::string& line : commands) {
        if (line.rfind("click:", 0) == 0) {  // a key mapping pressed once
            pendingClick = line.substr(6);
            for (const std::string& said : mod.tick()) std::printf("chat: %s\n", said.c_str());
            std::printf("clicked \"%s\"\n", line.substr(6).c_str());
            continue;
        }
        const int result = mod.runCommand(line);
        std::printf("command \"%s\": %d\n", line.c_str(), result);
        for (const std::string& said : mod.tick()) std::printf("chat: %s\n", said.c_str());
    }
    for (int i = 0; i < ticks; ++i) {
        h.gameTime = i;
        for (const std::string& line : mod.tick()) std::printf("chat: %s\n", line.c_str());
    }
    const auto level = mod.renderLevel(0.5f);
    std::printf("level: %zu submit(s)", level.size());
    for (const auto& sub : level) std::printf(" [%zu vertices, topology %d, state %d]", sub.vertices.size() / 9, sub.topology, sub.state);
    std::printf("\n");
    for (const auto& sub : level) {
        for (std::size_t i = 0; i < 4 && i * 9 + 8 < sub.vertices.size(); ++i) {
            const float* v = sub.vertices.data() + i * 9;
            std::printf("  vertex %zu: (%.2f, %.2f, %.2f) rgba(%.2f, %.2f, %.2f, %.2f)\n", i, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        }
    }
    const auto hud = mod.renderHud(0.5f);
    std::printf("hud: %zu command(s)\n", hud.size());
    for (std::size_t i = 0; i < hud.size() && i < 12; ++i) std::printf("  %d (%.0f,%.0f)-(%.0f,%.0f) %s\n", static_cast<int>(hud[i].kind), hud[i].x0, hud[i].y0, hud[i].x1, hud[i].y1, hud[i].text.c_str());
    for (const std::string& u : mod.report().unsupported) std::printf("unsupported %s\n", u.c_str());
    for (const std::string& l : mod.report().log) std::printf("log %s\n", l.c_str());
    if (!mod.report().error.empty()) std::printf("error: %s\n", mod.report().error.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (std::strcmp(argv[i], "--sim") == 0 && i + 2 < argc) {
            std::vector<std::string> commands;
            for (int k = i + 3; k < argc; ++k) commands.emplace_back(argv[k]);
            return simulate(argv[i + 1], argv[i + 2], 3, commands);
        }
        if (std::strcmp(argv[i], "--refs") == 0 && i + 1 < argc) return listReferences(argv[++i], false);
        if (std::strcmp(argv[i], "--jdk-gaps") == 0 && i + 1 < argc) return listReferences(argv[++i], true);
        if (std::strcmp(argv[i], "--api-gaps") == 0 && i + 1 < argc) return listReferences(argv[++i], false, true);
        const falcon::jvm::ModReport r = falcon::jvm::runMod(argv[i]);
        std::printf("== %s (%s %s): %.1f ms, %llu instructions\n", r.name.empty() ? argv[i] : r.name.c_str(), r.loader.c_str(), r.modId.c_str(), r.milliseconds, static_cast<unsigned long long>(r.instructions));
        for (const std::string& line : falcon::jvm::summarize(r)) std::printf("   %s\n", line.c_str());
        if (verbose) {
            for (const auto& reg : r.registrations) std::printf("   register %s %s: %s\n", reg.registry.c_str(), reg.id.c_str(), reg.text.c_str());
            for (const std::string& e : r.events) std::printf("   event %s\n", e.c_str());
            for (const std::string& k : r.keys) std::printf("   key %s\n", k.c_str());
            for (const std::string& c : r.calls) std::printf("   call %s\n", c.c_str());
            for (const std::string& u : r.unsupported) std::printf("   unsupported %s\n", u.c_str());
            for (const auto& m : r.mixins) {
                std::printf("   mixin %s ->", m.mixinClass.c_str());
                for (const std::string& t : m.targets) std::printf(" %s", t.c_str());
                std::printf("\n");
                for (const auto& h : m.hooks) {
                    std::printf("      %s %s:", h.kind.c_str(), h.handler.c_str());
                    for (const std::string& t : h.targets) std::printf(" %s", t.c_str());
                    std::printf("\n");
                }
            }
        }
        if (!r.error.empty()) status = 1;
    }
    return status;
}
