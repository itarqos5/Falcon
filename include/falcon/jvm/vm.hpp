#pragma once

// A small Java virtual machine for what a mod does when it starts: not the game's classes and not a real JVM, but an interpreter of JVM bytecode (the mod's own classes, from its jar) with every
// class of the game, of the mod loader and of the libraries it does not carry itself answered by a recording stand-in (a "shim"). A shim takes the constructor arguments and every call made on
// it and keeps them, so what the mod would have done (registered an item, a block, a key, an event handler ...) can be read afterwards and carried out by the client itself. Nothing of the mod runs
// outside this interpreter, no file or network is reachable from it (the only native methods are the ones registered in natives.cpp and shims.cpp), and a budget of instructions ends a mod that
// does not stop. Mixins are not applied: their annotations are read (class_file.hpp) and said.

#include "falcon/assets/zip_archive.hpp"
#include "falcon/jvm/class_file.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
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
};
struct Shim {
    std::string className;       // "net/minecraft/world/item/Item$Properties"
    std::string origin;          // where it came from when it is a static field of a stand-in class: "BuiltInRegistries.ITEM"
    std::vector<ShimCall> calls; // what was done to it, in order
    std::map<std::string, Object*> fields;  // its fields, made when first read (client.player is one stand-in, the same every time)
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
    Value prim;                                 // a box's value
    char primType = 0;                          // a box's type (I J F D Z C B S)
    std::unique_ptr<Shim> shim;                 // set on the stand-ins (and on a mod's object whose base class is one)
    MethodRef* handle = nullptr;                // a lambda: the method it calls (as its class names it)
    std::vector<Value> captured;                // a lambda: the arguments it carries
    int handleKind = 0;                         // a lambda: how its target is called (REF_invokeStatic ...)
    int position = 0;                           // an Iterator's place
    int hash = 0;
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
};

struct Class {
    std::string name;
    Class* super = nullptr;
    std::vector<Class*> interfaces;
    std::shared_ptr<const ClassFile> file;  // null for the built-in classes and the stand-ins
    bool shim = false, isInterface = false;
    enum class State : std::uint8_t { Uninitialized, Initializing, Ready } state = State::Uninitialized;
    std::unordered_map<std::string, std::unique_ptr<Method>> methods;  // "name:desc"
    std::unordered_map<std::string, FieldInfo> fields;
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
// A handler a mod registered for an event: the object (a lambda, or a mod's own object) and the event's name.
struct EventHandler {
    std::string event;
    Object* handler = nullptr;
};
struct Capture {
    std::vector<EventHandler> handlers;
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
    [[nodiscard]] Shim& shimOf(Object* o);
    [[nodiscard]] std::string describe(Value v, char kind, int depth = 0);
    [[nodiscard]] std::string describeObject(Object* o, int depth = 0);
    [[nodiscard]] Object* makeShim(const std::string& className, const std::string& origin = {});
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

    Capture capture;
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
    Class* defineFromFile(std::shared_ptr<const ClassFile> file);
    Value fieldDefault(char kind);
    Value shimStatic(Class* cls, const FieldRef& f);
    static void parseDescriptor(const std::string& desc, std::vector<char>& params, char& ret, int& slots);
    Value defaultReturn(char ret, const std::string& desc, Object* receiver);

    std::vector<std::shared_ptr<const ZipArchive>> jars_;
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
};

void installNatives(Vm& vm);
void installShims(Vm& vm);

}  // namespace falcon::jvm
