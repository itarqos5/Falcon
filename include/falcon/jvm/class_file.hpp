#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace falcon::jvm {

// One entry of a class file's constant pool. The meaning of a and b depends on the tag: Class and String: a is the Utf8 index; Fieldref, Methodref and InterfaceMethodref: a is the class, b the
// NameAndType; NameAndType: a the name, b the descriptor; MethodHandle: a the kind, b the reference; MethodType: a the descriptor; InvokeDynamic and Dynamic: a the bootstrap method, b the NameAndType.
enum Tag : std::uint8_t {
    kUtf8 = 1, kInteger = 3, kFloat = 4, kLong = 5, kDouble = 6, kClass = 7, kString = 8, kFieldref = 9, kMethodref = 10, kInterfaceMethodref = 11, kNameAndType = 12, kMethodHandle = 15, kMethodType = 16,
    kDynamic = 17, kInvokeDynamic = 18, kModule = 19, kPackage = 20,
};

struct CpEntry {
    std::uint8_t tag = 0;
    std::uint16_t a = 0, b = 0;
    std::int32_t i = 0;
    std::int64_t j = 0;
    float f = 0.0f;
    double d = 0.0;
    std::string s;  // the text of a Utf8 entry
};

struct Handler {
    std::uint16_t start = 0, end = 0, handler = 0, catchType = 0;  // catchType 0: any
};

struct Code {
    std::uint16_t maxStack = 0, maxLocals = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<Handler> handlers;
};

// What a Mixin class does to the game, read from its annotations (nothing is applied): an injector annotation (@Inject, @Redirect, @ModifyVariable ...) on a method, and the method strings it names.
struct MixinHook {
    std::string kind;                  // "Inject", "Redirect", "ModifyArg", "Accessor" ...
    std::string handler;               // the method of the mixin class that carries it
    std::vector<std::string> targets;  // the game methods it names ("tick", "init(Lnet/minecraft/...;)V")
};

struct Member {
    std::uint16_t access = 0;
    std::string name, desc;
    bool hasCode = false;
    Code code;
    std::uint16_t constantIndex = 0;  // a static field's ConstantValue
    std::string signature;            // a field's generic signature ("Ljava/util/List<Ljava/lang/String;>;"), when it has one
};

struct Bootstrap {
    std::uint16_t method = 0;
    std::vector<std::uint16_t> args;
};

struct ClassFile {
    std::vector<CpEntry> cp;
    std::uint16_t access = 0;
    std::string name, superName;  // "com/example/Thing", "java/lang/Object" (empty for Object itself)
    std::vector<std::string> interfaces;
    std::vector<Member> fields, methods;
    std::vector<Bootstrap> bootstraps;
    std::vector<std::string> mixinTargets;  // class-level @Mixin: the game classes it patches ("net/minecraft/client/gui/screens/TitleScreen")
    std::vector<MixinHook> hooks;           // the injector annotations on its methods
    std::vector<std::string> record;        // the component names of a record ("x", "y"), from the Record attribute
    std::string signature;                  // the class's generic signature ("Lcom/google/gson/reflect/TypeToken<Ljava/util/List<Lcom/x/Foo;>;>;"), when it has one

    [[nodiscard]] const std::string& utf8(std::uint16_t index) const;
    [[nodiscard]] const std::string& className(std::uint16_t classIndex) const;  // the name a Class entry gives
    [[nodiscard]] bool isInterface() const { return (access & 0x0200) != 0; }

    // Nothing if the bytes are not a class file this reader understands (a version it does not know is still read: the format has only grown).
    [[nodiscard]] static std::optional<ClassFile> parse(const std::uint8_t* data, std::size_t size);
};

}  // namespace falcon::jvm
