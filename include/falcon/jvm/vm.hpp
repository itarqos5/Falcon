#pragma once

// A small Java virtual machine for what a mod does when it starts: not the game's classes and not a real JVM, but an interpreter of JVM bytecode (the mod's own classes, from its jar) with every
// class of the game, of the mod loader and of the libraries it does not carry itself answered by a recording stand-in (a "shim"). A shim takes the constructor arguments and every call made on
// it and keeps them, so what the mod would have done (registered an item, a block, a key, an event handler ...) can be read afterwards and carried out by the client itself. Nothing of the mod runs
// outside this interpreter, no file or network is reachable from it (the only native methods are the ones registered in natives.cpp and shims.cpp), and a budget of instructions ends a mod that
// does not stop. Mixins are not applied: their annotations are read (class_file.hpp) and said.

#include "falcon/assets/zip_archive.hpp"
#include "falcon/jvm/class_file.hpp"
#include "falcon/jvm/mod_content.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <vector>

namespace falcon::jvm {

struct Object;
struct Class;
struct Method;
class Vm;
struct IndySite;

// One slot of a frame or of the heap. A long or a double fills a whole Value (and takes two slots in a frame, as the format says: the second one is never read).
union Value {
    std::int32_t i;
    std::int64_t j;
    float f;
    double d;
    Object* a;
    std::uint64_t raw;
    Value() : raw(0) {}
};
[[nodiscard]] inline Value valueOfInt(std::int32_t v) { Value r; r.i = v; return r; }
[[nodiscard]] inline Value valueOfLong(std::int64_t v) { Value r; r.j = v; return r; }
[[nodiscard]] inline Value valueOfFloat(float v) { Value r; r.f = v; return r; }
[[nodiscard]] inline Value valueOfDouble(double v) { Value r; r.d = v; return r; }
[[nodiscard]] inline Value valueOfRef(Object* v) { Value r; r.a = v; return r; }

// A Java exception on its way up the stack (a C++ exception, so the interpreter's own loop stays free of checks).
struct JavaThrow {
    Object* exception;
};
// The mod used more instructions than it was given, or went too deep: the run is over (not a Java exception: the mod cannot catch it).
struct BudgetExceeded {
    std::string why;
};

// Everything a call on a stand-in left: the arguments, as text.
struct ShimCall {
    std::string method;  // "<init>" for the constructor
    std::vector<std::string> args;
    std::vector<Value> raw;  // the same arguments as values, one each (a number as it is, an object as its pointer)
    std::string kinds;       // and what each is: I J F D or A
};
struct Shim {
    std::string className;       // "net/minecraft/world/item/Item$Properties"
    std::string origin;          // where it came from when it is a static field of a stand-in class: "BuiltInRegistries.ITEM"
    std::vector<ShimCall> calls; // what was done to it, in order
    std::map<std::string, Object*> fields;  // its fields, made when first read (client.player is one stand-in, the same every time)
    std::map<std::string, Value> values;    // its number fields, when the client set them (a screen's width and height)
};

enum class Kind : std::uint8_t { Plain, String, Array, Box, Lambda, Class, Iterator, Entry };

struct MethodRef;

struct Object {
    Class* cls = nullptr;
    Kind kind = Kind::Plain;
    std::vector<Value> fields;                  // instance fields (by index)
    std::string str;                            // a String, a StringBuilder, a Class's name, an Identifier's text, a Component's text
    std::vector<Value> elems;                   // an array's elements, a list's or set's contents, an Iterator's source
    char elemType = 0;                          // an array's component: B C D F I J S Z or L
    std::vector<std::pair<Value, Value>> map;   // a map's entries, in the order they were put
    std::unordered_multimap<int, std::uint32_t> mapIndex;  // a big map's entries by the hash of their key (built when asked for, rebuilt when entries were removed)
    std::size_t mapIndexed = 0;                 // how many entries the index covers
    bool mapIndexStale = false;
    Value prim;                                 // a box's value
    char primType = 0;                          // a box's type (I J F D Z C B S)
    std::unique_ptr<Shim> shim;                 // set on the stand-ins (and on a mod's object whose base class is one)
    MethodRef* handle = nullptr;                // a lambda: the method it calls (as its class names it)
    std::vector<Value> captured;                // a lambda: the arguments it carries
    int handleKind = 0;                         // a lambda: how its target is called (REF_invokeStatic ...)
    int position = 0;                           // an Iterator's place
    int hash = 0;
    bool marked = false;                        // the collector's mark
};

using NativeFn = std::function<Value(Vm&, Value*)>;  // the arguments one Value each (this first), whatever their type

struct Method {
    Class* owner = nullptr;
    std::string name, desc;
    std::uint16_t access = 0;
    const Code* code = nullptr;
    NativeFn native;
    std::vector<char> params;  // I (all that fits an int), J, F, D or A
    char ret = 'V';            // V, I, J, F, D or A
    int argSlots = 0;          // the slots the arguments take in a frame, this included
    [[nodiscard]] bool isStatic() const { return (access & 0x0008) != 0; }
    [[nodiscard]] bool callable() const { return code != nullptr || static_cast<bool>(native); }
};

struct FieldInfo {
    std::string name, desc;
    bool isStatic = false;
    int index = 0;
    std::uint16_t constantIndex = 0;
    std::uint16_t access = 0;
    std::string signature;
    int order = 0;  // where the class declares it
};

struct Class {
    std::string name;
    Class* super = nullptr;
    std::vector<Class*> interfaces;
    std::shared_ptr<const ClassFile> file;  // null for the built-in classes and the stand-ins
    bool shim = false, isInterface = false;
    bool platformChecked = false;  // a stand-in that the platform jar was asked for once
    enum class State : std::uint8_t { Uninitialized, Initializing, Ready } state = State::Uninitialized;
    std::unordered_map<std::string, std::unique_ptr<Method>> methods;  // "name:desc"
    std::unordered_map<std::string, FieldInfo> fields;
    std::vector<std::string> fieldOrder;  // the names, as the class declares them
    std::vector<Value> statics;
    int instanceSize = 0;
    std::vector<void*> cache;                       // the resolved entries of the constant pool, by index
    std::map<std::string, Object*> shimStatics;     // a stand-in's static fields, made when first read
    [[nodiscard]] std::string simpleName() const;
};

// A method named in a constant pool, resolved once.
struct MethodRef {
    Class* cls = nullptr;
    std::string name, desc, key;
    std::vector<char> params;
    char ret = 'V';
    int argSlots = 0;
    Method* direct = nullptr;
    Class* lastClass = nullptr;  // an inline cache for the virtual calls
    Method* lastMethod = nullptr;
};
struct FieldRef {
    Class* cls = nullptr;
    std::string name, desc;
    char kind = 'A';  // the field's descriptor kind: I J F D A
    Class* declaring = nullptr;
    FieldInfo* info = nullptr;
};

// What the mods did, as the stand-ins saw it.
struct Registration {
    std::string registry;  // "item", "block", "block_entity_type", "creative_mode_tab" ...
    std::string id;        // "examplemod:ruby"
    std::string kind;      // the class of the object registered ("RubyItem", "Item")
    std::string text;      // the object as the stand-ins recorded it: Item[<init>(Item$Properties[stacksTo(16)])]
    Object* object = nullptr;
};
// One thing a mod drew on the screen (through the GuiGraphics stand-in), in the GUI's own coordinates with the mod's translate and scale already applied.
struct DrawCmd {
    enum class Kind : std::uint8_t { Fill, Gradient, Text, Blit, Item, ScissorOn, ScissorOff } kind = Kind::Fill;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // a rectangle (Text: x0, y0 is where it starts)
    std::uint32_t color = 0xFFFFFFFF, color2 = 0xFFFFFFFF;  // ARGB
    bool shadow = true;
    bool centered = false;  // Text: x0 is the middle
    float scale = 1.0f;     // Text: the mod's scale
    std::string text;       // Text: what it says; Blit: the texture ("ns:textures/gui/x.png"); Item: the item's id
    float u = 0, v = 0, regionW = 0, regionH = 0;  // Blit: the part of the picture, in its pixels
    int texW = 256, texH = 256;
};

// What the client lends the VM: where the player is, how big the screen is, which keys are down. Every field has a harmless default.
struct HostState {
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    int guiWidth = 427, guiHeight = 240;
    int health = 20, food = 20;
    double eyeHeight = 1.62;
    int playerFlags = 32;                      // 1 sneaking, 2 on ground, 4 in water, 8 creative, 16 spectator, 32 alive
    double camX = 0, camY = 0, camZ = 0, camYaw = 0, camPitch = 0, camFov = 70;
    int framebufferWidth = 854, framebufferHeight = 480, guiScale = 2;
    double mouseX = 0, mouseY = 0, gameTime = 0;
    std::string gameDir, modsDir;              // where the mods may keep their files, and the folder they came from
    std::vector<std::string> loadedMods;       // "id\0version\0name", one per mod
    std::string dimension;                     // "minecraft:overworld"; empty with no world open
    std::string worldName, playerName, playerUuid;
    int worldKind = 0;                         // 0 single player, 1 multiplayer, 2 LAN
    std::function<std::string(int, int, int)> blockAt;  // "minecraft:stone"
    std::function<int(int, int, int)> blockFlags;       // 1 loaded, 2 opaque, 4 full collision
    std::function<bool(int)> keyDown;                   // a GLFW key code
    std::function<void(const std::string&)> sendChat, sendCommand;
    std::function<int(const std::string& text)> textWidth;                // the font's width of a text, 0 for the VM's guess
    std::function<bool(const std::string& keyName)> consumeClick;         // KeyMapping.consumeClick: a press not yet taken
    std::function<bool(const std::string& keyName)> isDown;               // KeyMapping.isDown
    std::function<std::string(const std::string& key)> translate;         // a translation key in the player's language
    std::vector<std::array<float, 4>> pose{{0.0f, 0.0f, 1.0f, 1.0f}};     // the mod's translate and scale while it draws: x offset, y offset, x scale, y scale
};

// A handler a mod registered for an event: the object (a lambda, or a mod's own object) and the event's name.
struct EventHandler {
    std::string event;
    Object* handler = nullptr;
};
// World geometry a mod submitted (SubmitNodeCollector.submitCustomGeometry): vertices of nine floats (x y z r g b a u v), camera-relative.
struct WorldSubmit {
    std::vector<float> vertices;
    int topology = 1;  // 0 lines, 1 triangles, 2 quads
    int state = 0;     // bit 0 depth test, 1 depth write, 2 blend, 3 cull, 4 line strip
    float lineWidth = 1.0f;
    std::string texture;
};

// An item a mod put into one of the game's creative tabs (ItemGroupEvents.modifyEntriesEvent(tab).register(entries -> entries.accept(item))).
struct GroupItem {
    std::string group;  // "INGREDIENTS"
    Object* item = nullptr;
};
struct Capture {
    std::vector<GroupItem> groupItems;
    std::vector<Object*> keyObjects;     // the key mappings a mod registered
    std::vector<EventHandler> handlers;
    std::vector<DrawCmd> draw;           // what the mod drew since the client last took it
    std::vector<WorldSubmit> submits;    // what it drew in the world
    std::vector<std::string> overlay;    // what it put on the action bar
    ModContent content;                  // what the platform's registries told of what the mod added
    std::vector<std::string> chat;       // what the mod said to the player (displayClientMessage and the like), until the client takes it
    std::vector<Registration> registrations;
    std::vector<std::string> events;     // "ClientTickEvents.END_CLIENT_TICK: handler lambda$onInitializeClient$0"
    std::vector<std::string> keys;       // "KeyMapping[<init>(key.examplemod.cast, ...)]"
    std::vector<std::string> calls;      // every other call a stand-in took, "Class.method(args)" (the first few hundred)
    std::vector<std::string> log;        // what the mod wrote (its logger, System.out)
    std::set<std::string> unsupported;   // library classes and methods the mod used that this machine does not have
};

class Vm {
public:
    Vm();
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    // The jars whose classes the mod can load (its own, and the ones inside it). Everything else is a stand-in.
    void addJar(std::shared_ptr<const ZipArchive> jar);
    // The platform: Falcon's own Java classes for the game, the mod loader's API and the parts of the JDK this machine has no native class for. They are loaded as real classes (interpreted) under
    // the names the game uses, before a mod's jar is asked; a class neither has is a stand-in. Methods declared native in them are answered by defineHostNative.
    void addPlatformJar(std::shared_ptr<const ZipArchive> jar);
    void defineHostNative(const std::string& cls, const std::string& name, const std::string& desc, NativeFn fn);
    // Calls a static method of a loaded class by name; the arguments one Value each. Null when there is no such method.
    Value callStatic(const std::string& cls, const std::string& name, const std::string& desc, const Value* args, int count, bool* found = nullptr);

    [[nodiscard]] Class* loadClass(const std::string& name);  // never null: a class nobody has becomes a stand-in
    // Runs a method: `args` one Value each (this first); throws JavaThrow.
    Value invoke(Method* method, const Value* args, int count);
    [[nodiscard]] Method* findMethod(Class* cls, const std::string& name, const std::string& desc);
    void ensureInitialized(Class* cls);
    // Makes an instance of a mod's class and runs its no-argument constructor.
    [[nodiscard]] Object* instantiate(Class* cls);

    // ---- for the natives ----
    [[nodiscard]] Object* newObject(Class* cls);
    [[nodiscard]] Object* newObject(const std::string& className) { return newObject(loadClass(className)); }
    [[nodiscard]] Object* newString(std::string text);
    [[nodiscard]] Object* intern(const std::string& text);
    [[nodiscard]] Object* newArray(char elemType, int length);
    [[nodiscard]] Object* box(char type, Value v);
    [[nodiscard]] Value unbox(Object* o) const { return o->prim; }
    [[nodiscard]] Object* classObject(const std::string& name);
    Value callInterface(Object* fn, const std::string& name, const std::string& desc, const Value* args, int count);
    [[noreturn]] void throwNew(const std::string& className, const std::string& message = {});
    [[nodiscard]] std::string& str(Object* o);  // throws NullPointerException on null
    [[nodiscard]] std::string toJavaString(Object* o);
    [[nodiscard]] bool equalsJava(Object* a, Object* b);
    [[nodiscard]] int hashJava(Object* o);
    [[nodiscard]] bool instanceOf(Object* o, Class* target);
    // The instanceof operator: like instanceOf, but a stand-in is an instance only of the game classes it can be (its own, and a few well-known bases), so a screen is not every kind of screen.
    [[nodiscard]] bool isInstance(Object* o, Class* target);
    [[nodiscard]] Shim& shimOf(Object* o);
    [[nodiscard]] std::string describe(Value v, char kind, int depth = 0);
    [[nodiscard]] std::string describeObject(Object* o, int depth = 0);
    [[nodiscard]] Object* makeShim(const std::string& className, const std::string& origin = {});
    // The one Minecraft (the client) every mod sees: Minecraft.getInstance(), the same each time.
    [[nodiscard]] Object* client();
    // A field of a stand-in: made the first time it is read (an object for a reference, zero for the rest), and the same after.
    [[nodiscard]] Value shimField(Object* o, const std::string& name, const std::string& desc);
    // What stands for a value of a class nobody gave the mod: an empty collection or string for the library types, a stand-in for the rest.
    [[nodiscard]] Object* makeDefault(const std::string& className, const std::string& origin = {});
    void note(const std::string& text);  // a line of the mod's own output
    void unsupported(const std::string& what) { capture.unsupported.insert(what); }

    // Natives and stand-in handlers register here (natives.cpp, shims.cpp).
    void defineClass(const std::string& name, const std::string& super, const std::vector<std::string>& interfaces = {}, bool isInterface = false);
    void defineNative(const std::string& cls, const std::string& name, const std::string& desc, bool isStatic, NativeFn fn);
    // A handler for a call on a stand-in (keyed "owner.name", or "*.name" for any owner): fills `result` and returns true when it answers the call, false to let the call be recorded as usual.
    using ShimHandler = std::function<bool(Vm&, Value*, const MethodRef&, Value& result)>;
    void defineShimHandler(const std::string& ownerAndName, ShimHandler handler) { shimHandlers_[ownerAndName] = std::move(handler); }
    [[nodiscard]] static std::string stripKey(const std::string& desc);

    // Calls what a mod registered as an event handler or a HUD element: a lambda, or an object of the mod that has a method of one of these names (and as many parameters as there are arguments). Kinds says what each argument is: I J F D or A.
    Value callHandler(Object* fn, const std::vector<std::string>& names, const std::string& kinds, const Value* args);

    // Frees what nothing reaches any more (a handler that runs every frame makes objects every frame). Only between calls: the roots are the classes' statics, the stand-ins' fields, what the
    // capture holds, and what was pinned. Values are not typed, so every one is looked at as if it could be a pointer (an int that equals an address only keeps one object alive a little longer).
    void collectGarbage();
    [[nodiscard]] std::size_t heapSize() const { return heap_.size(); }
    void pin(Object* o) { pins_.push_back(o); }
    // Where the VM was: the methods that were running, innermost first (at the last throw, or now): "Class.method <- Class.method ...".
    [[nodiscard]] std::string stackTrace(int depth, int max = 10) const;
    [[nodiscard]] int throwDepth() const { return throwDepth_; }
    [[nodiscard]] int depth() const { return depth_; }

    Capture capture;
    HostState host;
    std::string modId;                        // for the log lines
    std::uint64_t budget = 200'000'000;       // instructions
    [[nodiscard]] std::uint64_t instructionsRun() const { return instructions_; }
    [[nodiscard]] Class* objectClass() const { return objectClass_; }

private:
    friend void installNatives(Vm&);
    friend void installShims(Vm&);

    struct Frame;
    Value run(Method* m, Value* locals);
    Value call(Method* m, Value* slotArgs);
    Value dispatch(MethodRef& ref, int op, Value* args);
    Value shimCall(MethodRef& ref, int op, Value* args);
    Value lambdaInvoke(Object* lambda, const MethodRef& ref, Value* args);
    Value invokeDynamic(Class* cls, int index, Value* sp, int& consumedSlots, char& ret);
    MethodRef& methodRef(Class* cls, int index);
    FieldRef& fieldRef(Class* cls, int index);
    Method* makeMethod(Class* cls, const std::string& name, const std::string& desc, std::uint16_t access);
    Class* makeClass(const std::string& name);
    Class* defineFromFile(std::shared_ptr<const ClassFile> file, Class* existing = nullptr);
    void applyMixinAccessors(Class* mixin);
    Method* extensionMethod(const std::string& ownerName, Class* receiver, const MethodRef& r, bool isStatic);
    Class* findPlatformClass(const std::string& name);
    Value fieldDefault(char kind);
    Value shimStatic(Class* cls, const FieldRef& f);
    static void parseDescriptor(const std::string& desc, std::vector<char>& params, char& ret, int& slots);
    Value defaultReturn(char ret, const std::string& desc, Object* receiver);

    std::vector<std::shared_ptr<const ZipArchive>> jars_;
    std::vector<std::shared_ptr<const ZipArchive>> platformJars_;
    std::unordered_map<std::string, NativeFn> hostNatives_;  // "owner.name(desc)": the natives of platform classes
    std::unordered_set<std::string> noExtension_;           // extension classes looked for and not there
    std::vector<std::unique_ptr<IndySite>> sites_;
    std::map<std::string, std::unique_ptr<Class>> classes_;
    std::vector<std::unique_ptr<Object>> heap_;
    std::unordered_map<std::string, Object*> interned_;
    std::unordered_map<std::string, Object*> classObjects_;
    std::deque<MethodRef> methodRefs_;
    std::deque<FieldRef> fieldRefs_;
    std::unordered_map<std::string, ShimHandler> shimHandlers_;
    std::vector<Value> slab_;
    Value* top_ = nullptr;
    int depth_ = 0;
    std::uint64_t instructions_ = 0;
    int nextHash_ = 1;
    Class* objectClass_ = nullptr;
    Class* stringClass_ = nullptr;
    Class* classClass_ = nullptr;
    Object* client_ = nullptr;
    std::vector<Object*> pins_;
    std::array<Method*, 400> callStack_{};
    std::string tracePrefix_;  // FALCON_JVM_TRACE=com/wwaypoints/: every call of a class under that name is printed (what runs, in what order)
    bool tracing_ = false;
    int throwDepth_ = 0;
};

void installNatives(Vm& vm);
void installShims(Vm& vm);
void installHostNatives(Vm& vm);

}  // namespace falcon::jvm
