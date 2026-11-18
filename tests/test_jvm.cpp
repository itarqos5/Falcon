// The VM (jvm/vm.hpp) on a mod compiled by the real JDK (tests/jvm_mod, built into tests/data/jvm/vmtest.jar): that it computes what Java computes, that what the mod registers and hooks is
// captured, and that a mod that never stops is stopped.
#include "falcon/jvm/mod_runtime.hpp"
#include "falcon/jvm/vm.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

using namespace falcon::jvm;

namespace {

std::map<std::string, const Registration*> byId(const ModReport& r) {
    std::map<std::string, const Registration*> out;
    for (const Registration& reg : r.registrations) out[reg.id] = &reg;
    return out;
}

void test_mod_start_up() {
    const ModReport r = runMod(FALCON_JVM_TEST_JAR);
    CHECK(r.supported && r.modId == "vmtest" && r.loader == "Fabric" && r.error.empty());
    CHECK(r.entrypoints.size() == 2);  // (main and client)
    const auto ids = byId(r);
    // what the mod computed, read off the ids it registered (the values are what Java gives)
    for (const char* id : {"test:fib_55", "test:sum_5050", "test:fact_3628800", "test:mix_97", "test:sqrt_12.0", "test:big_1099511627777", "test:conv_-56_b_3_-2", "test:str_HELLO_3_cba", "test:list_bac_3_true_2",
                           "test:map_2_11_-1", "test:enum_2_1_fast", "test:rec_Point[x=3, y=4]_true_false", "test:switch_2", "test:array_Index 5 out of bounds for length 2!", "test:exc_NumberFormatException",
                           "test:cast_ok", "test:throw_boom", "test:shape_Square:16.0_Rect:10.0", "test:greet_hi bob", "test:lambda_15_sup10_42", "test:grid_3x4_7", "test:counter_2", "test:fmt_a-42-3.14"}) {
        if (ids.count(id) == 0) std::fprintf(stderr, "missing registration: %s\n", id);
        CHECK(ids.count(id) == 1);
    }
    CHECK(r.registrations.size() == 24);
    // an item, with what its constructor was given (a mod's own subclass of a game class: the base's constructor is recorded on it)
    const auto ruby = ids.find("test:ruby");
    CHECK(ruby != ids.end());
    if (ruby != ids.end()) {
        CHECK(ruby->second->registry == "item" && ruby->second->kind == "RubyItem");
        CHECK(ruby->second->text == "RubyItem[<init>(Properties[stacksTo(16), durability(250), fireResistant()])]");
    }
    CHECK(r.events.size() == 1 && r.events[0].rfind("ClientTickEvents.END_CLIENT_TICK: lambda(TestMod::lambda$onInitializeClient$0", 0) == 0);
    CHECK(r.log.size() == 1 && r.log[0] == "[out] \"booted\"");
    // the mixin: read, never applied
    CHECK(r.mixins.size() == 1 && r.mixins[0].mixinClass == "com/example/test/mixin/TitleMixin");
    if (!r.mixins.empty()) {
        CHECK(r.mixins[0].targets == std::vector<std::string>{"net/minecraft/client/gui/screens/TitleScreen"});
        CHECK(r.mixins[0].hooks.size() == 1 && r.mixins[0].hooks[0].kind == "Inject" && r.mixins[0].hooks[0].targets == std::vector<std::string>{"init"});
    }
    CHECK(r.unsupported.empty());
    CHECK(r.instructions > 500 && r.instructions < 100000);
    const std::vector<std::string> lines = summarize(r);
    CHECK(!lines.empty() && lines[0].rfind("Adds 23 to block:", 0) == 0);

    // a jar that is not a mod, and one for a loader whose start-up is not run
    CHECK(!runMod("no-such-file.jar").supported);
}

// The handlers a mod registered run again every tick, and what they do to the player is handed over: here, one line of chat on the third tick.
void test_tick_handlers() {
    ModInstance mod(FALCON_JVM_TEST_JAR);
    CHECK(mod.report().supported);
    std::vector<std::vector<std::string>> said;
    for (int i = 0; i < 5; ++i) said.push_back(mod.tick());
    CHECK(said[0].empty() && said[1].empty() && said[3].empty() && said[4].empty());
    CHECK(said[2] == std::vector<std::string>{"hello from the mod 3"});
    CHECK(mod.report().error.empty());
}

// What a mod adds, read as facts: items with their properties, a block, the tabs the items are listed in, a key; and the HUD, the key and the button at work.
void test_content() {
    ModInstance mod(FALCON_JVM_CONTENT_JAR);
    CHECK(mod.report().supported && mod.report().error.empty());
    const ModContent& c = mod.content();
    CHECK(c.modId == "vmcontent");
    CHECK(c.blocks.size() == 1);
    if (c.blocks.size() == 1) {
        const ModBlockDef& b = c.blocks[0];
        CHECK(b.id == "vmcontent:ruby_block" && b.hardness == 3.0f && b.resistance == 6.0f && b.light == 7 && b.sound == "amethyst_block");
    }
    CHECK(c.items.size() == 3);
    const auto item = [&](const char* id) -> const ModItemDef* {
        for (const ModItemDef& i : c.items) {
            if (i.id == id) return &i;
        }
        return nullptr;
    };
    const ModItemDef* block = item("vmcontent:ruby_block");
    const ModItemDef* ruby = item("vmcontent:ruby");
    const ModItemDef* sword = item("vmcontent:ruby_sword");
    CHECK(block != nullptr && ruby != nullptr && sword != nullptr);
    if (block != nullptr && ruby != nullptr && sword != nullptr) {
        CHECK(block->block == "vmcontent:ruby_block" && block->tab == "minecraft:building_blocks");
        CHECK(ruby->stacksTo == 16 && ruby->food && ruby->nutrition == 4 && ruby->saturation > 0.29f && ruby->saturation < 0.31f && ruby->tab == "minecraft:ingredients");
        CHECK(sword->durability == 250 && sword->stacksTo == 1 && sword->rarity == "epic" && sword->tab == "minecraft:tools_and_utilities");
    }
    CHECK(c.keys.size() == 1);
    if (c.keys.size() == 1) CHECK(c.keys[0].name == "key.vmcontent.hello" && c.keys[0].key == 72 && c.keys[0].category == "key.categories.vmcontent" && !c.keys[0].mouse);

    // the HUD: a rectangle and a text, in the coordinates the mod gave them
    std::vector<DrawCmd> hud = mod.renderHud();
    CHECK(hud.size() == 2);
    if (hud.size() == 2) {
        CHECK(hud[0].kind == DrawCmd::Kind::Fill && hud[0].x0 == 4 && hud[0].y0 == 4 && hud[0].x1 == 96 && hud[0].y1 == 16 && hud[0].color == 0x90000000u);
        CHECK(hud[1].kind == DrawCmd::Kind::Text && hud[1].text == "Ruby HUD 0" && hud[1].x0 == 8 && hud[1].y0 == 6 && hud[1].color == 0xFFFF5555u);
    }
    // the key: pressed once, it makes one line of chat on the next tick
    int clicks = 1;
    mod.host().consumeClick = [&](const std::string& name) {
        if (name != "key.vmcontent.hello" || clicks == 0) return false;
        --clicks;
        return true;
    };
    CHECK(mod.tick() == std::vector<std::string>{"the key was pressed 1"});
    CHECK(mod.tick().empty());
    hud = mod.renderHud();
    CHECK(hud.size() == 2 && hud[1].text == "Ruby HUD 2");

    // the button: only on the title screen, where the mod put it, and its handler runs when it is pressed
    CHECK(mod.screenOpened("net/minecraft/client/gui/screens/PauseScreen", 427, 240).empty());
    const auto buttons = mod.screenOpened("net/minecraft/client/gui/screens/TitleScreen", 427, 240);
    CHECK(buttons.size() == 1);
    if (buttons.size() == 1) {
        CHECK(buttons[0].label == "Ruby Button" && buttons[0].x == 427 / 2 + 104 && buttons[0].y == 240 / 4 + 48 && buttons[0].width == 98 && buttons[0].height == 20);
        mod.pressWidget(buttons[0].id);
        CHECK(mod.tick() == std::vector<std::string>{"the ruby button was pressed"});
    }
    // a handler that runs every frame does not make the heap grow for ever
    for (int i = 0; i < 20000; ++i) (void)mod.renderHud();
    CHECK(mod.report().error.empty());
}

void test_speed_and_limits() {
    auto jar = falcon::ZipArchive::open(FALCON_JVM_TEST_JAR);
    CHECK(jar.has_value());
    if (!jar) return;
    const auto shared = std::make_shared<const falcon::ZipArchive>(std::move(*jar));
    {  // arithmetic: the VM's answer is the answer of the same loop in C++
        Vm vm;
        vm.addJar(shared);
        vm.budget = 2'000'000'000ull;
        Method* spin = vm.findMethod(vm.loadClass("com/example/test/Bench"), "spin", "(I)J");
        CHECK(spin != nullptr);
        if (spin != nullptr) {
            const int n = 3'000'000;
            Value args[1];
            args[0] = valueOfInt(n);
            const auto started = std::chrono::steady_clock::now();
            const Value got = vm.invoke(spin, args, 1);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            std::int64_t expected = 0;
            for (int i = 0; i < n; ++i) expected += static_cast<std::int64_t>((i ^ (i >> 3))) * 31LL + (expected & 7);
            CHECK(got.j == expected);
            std::printf("  spin(%d): %.0f ms, %llu instructions, %.0f million a second\n", n, ms, static_cast<unsigned long long>(vm.instructionsRun()), static_cast<double>(vm.instructionsRun()) / ms / 1000.0);
        }
    }
    {  // a mod that never stops, and one that never returns, are stopped
        Vm vm;
        vm.addJar(shared);
        vm.budget = 1'000'000;
        Method* forever = vm.findMethod(vm.loadClass("com/example/test/Bench"), "forever", "()I");
        bool stopped = false;
        try {
            vm.invoke(forever, nullptr, 0);
        } catch (const BudgetExceeded&) {
            stopped = true;
        }
        CHECK(stopped);
        Vm deep;
        deep.addJar(shared);
        Method* recurse = deep.findMethod(deep.loadClass("com/example/test/Bench"), "recurse", "(I)I");
        Value args[1];
        args[0] = valueOfInt(0);
        stopped = false;
        try {
            deep.invoke(recurse, args, 1);
        } catch (const BudgetExceeded&) {
            stopped = true;
        }
        CHECK(stopped);
    }
}

}  // namespace

int main() {
    test_mod_start_up();
    test_tick_handlers();
    test_content();
    test_speed_and_limits();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("All VM tests passed");
    return 0;
}
