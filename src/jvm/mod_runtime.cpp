#include "falcon/jvm/mod_runtime.hpp"

#include "falcon/mods/mod_info.hpp"
#include "falcon/mods/optimization_mods.hpp"

#include "shim_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace falcon::jvm {
namespace {

using json = nlohmann::json;

std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
}

// The names in a list of entry points: a string, or an object with a "value".
std::vector<std::string> entryNames(const json& list) {
    std::vector<std::string> out;
    if (!list.is_array()) return out;
    for (const json& e : list) {
        if (e.is_string()) out.push_back(e.get<std::string>());
        else if (e.is_object() && e.contains("value") && e["value"].is_string()) out.push_back(e["value"].get<std::string>());
    }
    return out;
}

}  // namespace

ModInstance::~ModInstance() = default;

ModInstance::ModInstance(const std::filesystem::path& jarPath, const RunOptions& options) {
    ModReport& report = report_;
    const auto started = std::chrono::steady_clock::now();
    const auto finish = [&] { report.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
    auto jar = ZipArchive::open(jarPath);
    if (!jar) {
        report.notRun = "the file is not a jar";
        { finish(); return; }
    }
    auto shared = std::make_shared<const ZipArchive>(std::move(*jar));
    json meta;
    std::vector<std::pair<std::string, std::string>> entries;  // (kind, class) in the order they run
    std::vector<std::string> mixinConfigs;
    if (const auto text = shared->readText("fabric.mod.json")) {
        meta = json::parse(*text, nullptr, false, true);
        report.loader = "Fabric";
        if (meta.is_object()) {
            report.modId = meta.value("id", "");
            report.name = meta.value("name", report.modId);
            if (meta.contains("entrypoints") && meta["entrypoints"].is_object()) {
                for (const std::string& n : entryNames(meta["entrypoints"].value("main", json::array()))) entries.emplace_back("main", n);
                if (options.runClient) {
                    for (const std::string& n : entryNames(meta["entrypoints"].value("client", json::array()))) entries.emplace_back("client", n);
                }
            }
            if (meta.contains("mixins") && meta["mixins"].is_array()) {
                for (const json& m : meta["mixins"]) {
                    if (m.is_string()) mixinConfigs.push_back(m.get<std::string>());
                    else if (m.is_object() && m.contains("config") && m["config"].is_string()) mixinConfigs.push_back(m["config"].get<std::string>());
                }
            }
        }
    } else if (const auto quiltText = shared->readText("quilt.mod.json")) {
        meta = json::parse(*quiltText, nullptr, false, true);
        report.loader = "Quilt";
        if (meta.is_object() && meta.contains("quilt_loader") && meta["quilt_loader"].is_object()) {
            const json& loader = meta["quilt_loader"];
            report.modId = loader.value("id", "");
            report.name = loader.contains("metadata") ? loader["metadata"].value("name", report.modId) : report.modId;
            if (loader.contains("entrypoints") && loader["entrypoints"].is_object()) {
                for (const std::string& n : entryNames(loader["entrypoints"].value("init", json::array()))) entries.emplace_back("main", n);
                if (options.runClient) {
                    for (const std::string& n : entryNames(loader["entrypoints"].value("client_init", json::array()))) entries.emplace_back("client", n);
                }
            }
        }
        if (meta.is_object() && meta.contains("mixin") && meta["mixin"].is_array()) {
            for (const json& m : meta["mixin"]) {
                if (m.is_string()) mixinConfigs.push_back(m.get<std::string>());
            }
        }
    } else {
        report.notRun = shared->contains("META-INF/mods.toml") || shared->contains("META-INF/neoforge.mods.toml") ? "a Forge or NeoForge mod: its start-up is not run" : "no fabric.mod.json or quilt.mod.json";
        { finish(); return; }
    }
    report.supported = true;

    vm_ = std::make_unique<Vm>();
    Vm& vm = *vm_;
    vm.modId = report.modId;
    vm.budget = options.budget;
    vm.addJar(shared);
    for (const std::string& name : shared->entryNames()) {  // the jars it carries (jar-in-jar): its own libraries
        if (name.rfind("META-INF/jars/", 0) != 0 || name.size() < 5 || name.compare(name.size() - 4, 4, ".jar") != 0) continue;
        if (auto bytes = shared->read(name)) {
            if (auto inner = ZipArchive::fromBytes(std::move(*bytes))) vm.addJar(std::make_shared<const ZipArchive>(std::move(*inner)));
        }
    }

    // The mixins first: read, never applied.
    for (const std::string& config : mixinConfigs) {
        const auto text = shared->readText(config);
        if (!text) continue;
        const json mixins = json::parse(*text, nullptr, false, true);
        if (!mixins.is_object()) continue;
        const std::string package = mixins.value("package", "");
        for (const char* section : {"mixins", "client", "server"}) {
            if (!mixins.contains(section) || !mixins[section].is_array()) continue;
            for (const json& name : mixins[section]) {
                if (!name.is_string()) continue;
                const std::string path = slashes(package + "." + name.get<std::string>()) + ".class";
                const auto bytes = shared->read(path);
                if (!bytes) continue;
                const auto cf = ClassFile::parse(bytes->data(), bytes->size());
                if (!cf) continue;
                report.mixins.push_back({cf->name, cf->mixinTargets, cf->hooks});
            }
        }
    }

    for (const auto& [kind, name] : entries) {
        if (name.find("::") != std::string::npos) {
            report.error += (report.error.empty() ? "" : "; ") + std::string("the entry point ") + name + " is a method reference, which is not run";
            continue;
        }
        try {
            Class* cls = vm.loadClass(slashes(name));
            if (cls->shim) {
                report.error += (report.error.empty() ? "" : "; ") + std::string("the entry point class ") + name + " is not in the jar";
                continue;
            }
            Object* entry = vm.instantiate(cls);
            const char* method = kind == "client" ? "onInitializeClient" : "onInitialize";
            if (Method* m = vm.findMethod(cls, method, "()V")) {
                Value args[1];
                args[0] = valueOfRef(entry);
                vm.invoke(m, args, 1);
                report.entrypoints.push_back(name + " (" + kind + ")");
            } else if (Method* init = vm.findMethod(cls, kind == "client" ? "init" : "init", "()V"); init != nullptr && init->isStatic()) {
                vm.invoke(init, nullptr, 0);
                report.entrypoints.push_back(name + " (" + kind + ")");
            } else {
                report.error += (report.error.empty() ? "" : "; ") + name + " has no " + method + "()";
            }
        } catch (const JavaThrow& thrown) {
            report.error += (report.error.empty() ? "" : "; ") + std::string("uncaught: ") + vm.toJavaString(thrown.exception);
            report.entrypoints.push_back(name + " (" + kind + ", stopped)");
        } catch (const BudgetExceeded& over) {
            report.error += (report.error.empty() ? "" : "; ") + over.why;
            report.entrypoints.push_back(name + " (" + kind + ", stopped)");
        }
    }

    // What the mod put into the game's creative tabs: each tab's handlers are given the tab's entries and add to them.
    for (const EventHandler& h : std::vector<EventHandler>(vm.capture.handlers)) {
        if (h.event.rfind("ItemGroupEvents.modifyEntries.", 0) != 0) continue;
        try {
            vm.budget = vm.instructionsRun() + 200'000;
            Value tabEntries = valueOfRef(vm.makeShim("net/fabricmc/fabric/api/itemgroup/v1/FabricItemGroupEntries", h.event));
            vm.callHandler(h.handler, {"modifyEntries"}, "A", &tabEntries);
        } catch (const JavaThrow&) {
        } catch (const BudgetExceeded&) {
        }
    }
    content_ = extractContent(vm, report.modId);

    report.registrations = vm.capture.registrations;
    for (Registration& r : report.registrations) r.object = nullptr;  // (the objects go with the VM)
    report.events = vm.capture.events;
    report.keys = vm.capture.keys;
    report.calls = vm.capture.calls;
    report.log = vm.capture.log;
    report.unsupported.assign(vm.capture.unsupported.begin(), vm.capture.unsupported.end());
    report.instructions = vm.instructionsRun();
    off_.assign(vm.capture.handlers.size(), false);
    finish();
}

std::vector<std::string> ModInstance::tick() {
    std::vector<std::string> said;
    if (!vm_ || vm_->capture.handlers.empty()) return said;
    Vm& vm = *vm_;
    off_.resize(vm.capture.handlers.size(), false);
    for (std::size_t i = 0; i < vm.capture.handlers.size(); ++i) {
        const EventHandler& h = vm.capture.handlers[i];
        if (off_[i] || h.event.find("CLIENT_TICK") == std::string::npos) continue;
        vm.budget = vm.instructionsRun() + 100'000;  // what one tick of one handler may spend
        try {
            Value arg = valueOfRef(vm.client());
            vm.callHandler(h.handler, {"onEndTick", "onStartTick", "onTick"}, "A", &arg);
        } catch (const JavaThrow& thrown) {
            switchOff(i, vm.toJavaString(thrown.exception));
        } catch (const BudgetExceeded& over) {
            switchOff(i, over.why);
        }
    }
    said.swap(vm.capture.chat);
    collect();
    return said;
}

void ModInstance::switchOff(std::size_t handler, const std::string& why) {
    off_[handler] = true;
    report_.error += (report_.error.empty() ? "" : "; ") + std::string("a handler was switched off: ") + why;
}

void ModInstance::collect() {
    if (vm_->heapSize() < gcAt_) return;
    vm_->collectGarbage();
    gcAt_ = std::max<std::size_t>(50'000, vm_->heapSize() * 2);
}

std::vector<DrawCmd> ModInstance::renderHud() {
    std::vector<DrawCmd> out;
    if (!vm_) return out;
    Vm& vm = *vm_;
    if (graphics_ == nullptr) {
        graphics_ = vm.makeShim("net/minecraft/client/gui/GuiGraphics");
        delta_ = vm.makeShim("net/minecraft/client/DeltaTracker");
        vm.pin(graphics_);
        vm.pin(delta_);
    }
    off_.resize(vm.capture.handlers.size(), false);
    vm.capture.draw.clear();
    vm.host.pose.assign(1, {0.0f, 0.0f, 1.0f, 1.0f});
    for (std::size_t i = 0; i < vm.capture.handlers.size(); ++i) {
        const EventHandler& h = vm.capture.handlers[i];
        if (off_[i] || (h.event != "HudElement" && h.event.rfind("HudRenderCallback", 0) != 0 && h.event.rfind("HudLayerRegistrationCallback", 0) != 0)) continue;
        vm.budget = vm.instructionsRun() + 200'000;  // what one frame of one HUD may spend
        try {
            Value args[2] = {valueOfRef(graphics_), valueOfRef(delta_)};
            vm.callHandler(h.handler, {"render", "onHudRender"}, "AA", args);
        } catch (const JavaThrow& thrown) {
            switchOff(i, vm.toJavaString(thrown.exception));
        } catch (const BudgetExceeded& over) {
            switchOff(i, over.why);
        }
        vm.host.pose.assign(1, {0.0f, 0.0f, 1.0f, 1.0f});
    }
    out.swap(vm.capture.draw);
    collect();
    return out;
}

namespace {

// A button the mod made: from Button.builder(message, onPress).bounds(...).build(), or from new Button(x, y, w, h, message, onPress, ...).
bool readButton(Vm& vm, Object* button, ModInstance::ScreenWidget& out, Object*& onPress) {
    if (button == nullptr || !button->shim) return false;
    out.width = 150;
    out.height = 20;
    onPress = nullptr;
    Object* message = nullptr;
    if (const auto it = button->shim->fields.find("builder"); it != button->shim->fields.end() && it->second != nullptr && it->second->shim) {
        const Object* builder = it->second;
        if (const auto m = builder->shim->fields.find("message"); m != builder->shim->fields.end()) message = m->second;
        if (const auto p = builder->shim->fields.find("onPress"); p != builder->shim->fields.end()) onPress = p->second;
        for (const ShimCall& c : builder->shim->calls) {
            const auto int_ = [&](std::size_t i) { return c.kinds[i] == 'F' ? static_cast<int>(c.raw[i].f) : c.raw[i].i; };
            if (c.method == "bounds" && c.raw.size() >= 4) out = {out.label, int_(0), int_(1), int_(2), int_(3), 0};
            else if (c.method == "pos" && c.raw.size() >= 2) out.x = int_(0), out.y = int_(1);
            else if (c.method == "size" && c.raw.size() >= 2) out.width = int_(0), out.height = int_(1);
            else if (c.method == "width" && !c.raw.empty()) out.width = int_(0);
        }
    } else if (const ShimCall* init = firstCall(button, "<init>")) {
        int ints = 0;
        for (std::size_t i = 0; i < init->raw.size(); ++i) {
            if (init->kinds[i] == 'I' && ints < 4) {
                (ints == 0 ? out.x : ints == 1 ? out.y : ints == 2 ? out.width : out.height) = init->raw[i].i;
                ++ints;
            } else if (init->kinds[i] == 'A' && init->raw[i].a != nullptr) {
                if (message == nullptr && (init->raw[i].a->kind == Kind::String || shimClassHas(init->raw[i].a, "Component"))) message = init->raw[i].a;
                else if (onPress == nullptr) onPress = init->raw[i].a;
            }
        }
    } else {
        return false;
    }
    out.label = textOf(vm, message);
    return onPress != nullptr;
}

}  // namespace

std::vector<ModInstance::ScreenWidget> ModInstance::screenOpened(const std::string& screenClass, int width, int height) {
    std::vector<ScreenWidget> out;
    if (!vm_) return out;
    Vm& vm = *vm_;
    off_.resize(vm.capture.handlers.size(), false);
    Object* screen = nullptr;
    for (std::size_t i = 0; i < vm.capture.handlers.size(); ++i) {
        const EventHandler& h = vm.capture.handlers[i];
        if (off_[i] || (h.event != "ScreenEvents.AFTER_INIT" && h.event != "ScreenEvents.BEFORE_INIT")) continue;
        if (screen == nullptr) {
            screen = vm.makeShim(screenClass);
            Shim& s = vm.shimOf(screen);
            s.values["width"] = valueOfInt(width);
            s.values["height"] = valueOfInt(height);
            vm.pin(screen);
        }
        vm.budget = vm.instructionsRun() + 200'000;
        try {
            Value args[4] = {valueOfRef(vm.client()), valueOfRef(screen), valueOfInt(width), valueOfInt(height)};
            vm.callHandler(h.handler, {"afterInit", "beforeInit"}, "AAII", args);
        } catch (const JavaThrow& thrown) {
            switchOff(i, vm.toJavaString(thrown.exception));
        } catch (const BudgetExceeded& over) {
            switchOff(i, over.why);
        }
    }
    if (screen != nullptr) {
        if (const auto it = vm.shimOf(screen).fields.find("#buttons"); it != vm.shimOf(screen).fields.end() && it->second != nullptr) {
            for (const Value& e : it->second->elems) {
                ScreenWidget w;
                Object* onPress = nullptr;
                if (!readButton(vm, e.a, w, onPress)) continue;
                w.id = nextWidget_++;
                widgets_[w.id] = {e.a, onPress};
                vm.pin(e.a);
                vm.pin(onPress);
                out.push_back(std::move(w));
            }
        }
        while (widgets_.size() > 256) widgets_.erase(widgets_.begin());  // (an old screen's buttons)
    }
    collect();
    return out;
}

void ModInstance::pressWidget(int id) {
    const auto it = widgets_.find(id);
    if (it == widgets_.end() || !vm_) return;
    Vm& vm = *vm_;
    vm.budget = vm.instructionsRun() + 200'000;
    try {
        Value arg = valueOfRef(it->second.button);
        vm.callHandler(it->second.onPress, {"onPress", "run", "accept"}, "A", &arg);
    } catch (const JavaThrow& thrown) {
        report_.error += (report_.error.empty() ? "" : "; ") + std::string("a button's handler failed: ") + vm.toJavaString(thrown.exception);
    } catch (const BudgetExceeded& over) {
        report_.error += (report_.error.empty() ? "" : "; ") + std::string("a button's handler failed: ") + over.why;
    }
}

ModReport runMod(const std::filesystem::path& jar, const RunOptions& options) { return ModInstance(jar, options).report(); }

ModHost::~ModHost() = default;

ModHost::ModHost(const std::filesystem::path& modsDir) {
    for (const ModInfo& info : readModsFolder(modsDir)) {
        if (info.loader != "Fabric" && info.loader != "Quilt") continue;
        std::vector<std::string> needs;
        for (const std::string& dependency : info.depends) needs.push_back(dependency.substr(0, dependency.find(' ')));
        if (judgeOptimization(info.id, info.name, {}, needs).blocked) continue;  // (not compatible with Falcon: not started)
        try {
            auto instance = std::make_unique<ModInstance>(info.file);
            if (instance->report().supported) mods_.push_back(std::move(instance));
        } catch (...) {  // (a jar the VM cannot make sense of is left out)
        }
    }
}

std::vector<ModHost::Said> ModHost::tick() {
    std::vector<Said> out;
    for (const auto& mod : mods_) {
        for (std::string& text : mod->tick()) out.push_back({mod->report().name.empty() ? mod->report().modId : mod->report().name, std::move(text)});
    }
    return out;
}

std::vector<DrawCmd> ModHost::renderHud() {
    std::vector<DrawCmd> out;
    for (const auto& mod : mods_) {
        std::vector<DrawCmd> drawn = mod->renderHud();
        out.insert(out.end(), std::make_move_iterator(drawn.begin()), std::make_move_iterator(drawn.end()));
    }
    return out;
}

std::vector<ModHost::HostWidget> ModHost::screenOpened(const std::string& screenClass, int width, int height) {
    std::vector<HostWidget> out;
    for (const auto& mod : mods_) {
        for (ModInstance::ScreenWidget& w : mod->screenOpened(screenClass, width, height)) out.push_back({mod.get(), std::move(w)});
    }
    return out;
}

void ModHost::setHost(const HostState& state) {
    for (const auto& mod : mods_) {
        HostState& h = mod->host();
        h.x = state.x;
        h.y = state.y;
        h.z = state.z;
        h.yaw = state.yaw;
        h.pitch = state.pitch;
        h.guiWidth = state.guiWidth;
        h.guiHeight = state.guiHeight;
        h.health = state.health;
        h.food = state.food;
        h.textWidth = state.textWidth;
        h.consumeClick = state.consumeClick;
        h.isDown = state.isDown;
        h.translate = state.translate;
    }
}

std::vector<std::string> summarize(const ModReport& r) {
    std::vector<std::string> out;
    if (!r.supported) {
        out.push_back("Not run: " + r.notRun + ".");
        return out;
    }
    std::map<std::string, std::vector<const Registration*>> byRegistry;
    for (const Registration& reg : r.registrations) byRegistry[reg.registry].push_back(&reg);
    for (const auto& [registry, list] : byRegistry) {
        std::string line = "Adds " + std::to_string(list.size()) + " to " + registry + ":";
        for (std::size_t i = 0; i < list.size() && i < 6; ++i) line += (i ? ", " : " ") + list[i]->id;
        if (list.size() > 6) line += ", ...";
        out.push_back(line);
    }
    if (!r.keys.empty()) out.push_back(std::to_string(r.keys.size()) + " key binding" + (r.keys.size() == 1 ? "" : "s") + ": " + r.keys.front());
    if (!r.events.empty()) {
        std::string line = std::to_string(r.events.size()) + " event handler" + (r.events.size() == 1 ? "" : "s") + ":";
        for (std::size_t i = 0; i < r.events.size() && i < 4; ++i) line += (i ? "; " : " ") + r.events[i].substr(0, r.events[i].find(':'));
        out.push_back(line);
    }
    if (!r.mixins.empty()) {
        std::size_t hooks = 0;
        std::vector<std::string> targets;
        for (const MixinInfo& m : r.mixins) {
            hooks += m.hooks.size();
            for (const std::string& t : m.targets) {
                const std::size_t at = t.find_last_of("/$");
                const std::string simple = at == std::string::npos ? t : t.substr(at + 1);
                if (std::find(targets.begin(), targets.end(), simple) == targets.end()) targets.push_back(simple);
            }
        }
        std::string line = std::to_string(r.mixins.size()) + " mixin" + (r.mixins.size() == 1 ? "" : "s") + " (" + std::to_string(hooks) + " injections, not applied) patching:";
        for (std::size_t i = 0; i < targets.size() && i < 6; ++i) line += (i ? ", " : " ") + targets[i];
        if (targets.size() > 6) line += ", ...";
        out.push_back(line);
    }
    for (std::size_t i = 0; i < r.log.size() && i < 3; ++i) out.push_back("Wrote: " + r.log[i]);
    if (!r.unsupported.empty()) out.push_back(std::to_string(r.unsupported.size()) + " library calls not available to the VM, e.g. " + r.unsupported.front());
    if (!r.error.empty()) out.push_back("Stopped: " + r.error);
    if (out.empty()) out.push_back("Its start-up code ran and registered nothing.");
    return out;
}

}  // namespace falcon::jvm
