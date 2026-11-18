#include "falcon/jvm/class_file.hpp"

#include <algorithm>
#include <cstring>

namespace falcon::jvm {
namespace {

struct Reader {
    const std::uint8_t* p;
    std::size_t size, at = 0;
    bool bad = false;

    [[nodiscard]] bool has(std::size_t n) const { return at + n <= size; }
    std::uint8_t u1() { return has(1) ? p[at++] : (bad = true, std::uint8_t{0}); }
    std::uint16_t u2() {
        if (!has(2)) return bad = true, std::uint16_t{0};
        const auto v = static_cast<std::uint16_t>((p[at] << 8) | p[at + 1]);
        at += 2;
        return v;
    }
    std::uint32_t u4() {
        if (!has(4)) return bad = true, std::uint32_t{0};
        const std::uint32_t v = (static_cast<std::uint32_t>(p[at]) << 24) | (static_cast<std::uint32_t>(p[at + 1]) << 16) | (static_cast<std::uint32_t>(p[at + 2]) << 8) | p[at + 3];
        at += 4;
        return v;
    }
    void skip(std::size_t n) {
        if (!has(n)) bad = true, at = size;
        else at += n;
    }
};

// What an annotation says, as text: for each element, the strings and class names in it (the numbers and enums are left out: nothing here needs them).
struct Annotation {
    std::string type;
    std::vector<std::pair<std::string, std::vector<std::string>>> elements;
};

std::string asClassName(const std::string& descriptor) {  // "Lnet/minecraft/Foo;" -> "net/minecraft/Foo"
    if (descriptor.size() > 2 && descriptor.front() == 'L' && descriptor.back() == ';') return descriptor.substr(1, descriptor.size() - 2);
    return descriptor;
}

void readElement(Reader& r, const ClassFile& cf, std::vector<std::string>& out, int depth) {
    const std::uint8_t tag = r.u1();
    switch (tag) {
        case 's': {
            const std::uint16_t index = r.u2();
            if (index < cf.cp.size() && cf.cp[index].tag == kUtf8) out.push_back(cf.cp[index].s);
            break;
        }
        case 'c': {
            const std::uint16_t index = r.u2();
            if (index < cf.cp.size() && cf.cp[index].tag == kUtf8) out.push_back(asClassName(cf.cp[index].s));
            break;
        }
        case 'e': r.u2(), r.u2(); break;
        case '@': {
            r.u2();
            const std::uint16_t pairs = r.u2();
            for (std::uint16_t i = 0; i < pairs && !r.bad; ++i) {
                r.u2();
                readElement(r, cf, out, depth + 1);
            }
            break;
        }
        case '[': {
            const std::uint16_t count = r.u2();
            for (std::uint16_t i = 0; i < count && !r.bad && depth < 8; ++i) readElement(r, cf, out, depth + 1);
            break;
        }
        default: r.u2(); break;  // B C D F I J S Z: a constant
    }
}

std::vector<Annotation> readAnnotations(Reader& r, const ClassFile& cf) {
    std::vector<Annotation> out;
    const std::uint16_t count = r.u2();
    for (std::uint16_t i = 0; i < count && !r.bad; ++i) {
        Annotation a;
        const std::uint16_t type = r.u2();
        if (type < cf.cp.size()) a.type = cf.cp[type].s;
        const std::uint16_t pairs = r.u2();
        for (std::uint16_t k = 0; k < pairs && !r.bad; ++k) {
            const std::uint16_t name = r.u2();
            std::vector<std::string> values;
            readElement(r, cf, values, 0);
            a.elements.emplace_back(name < cf.cp.size() ? cf.cp[name].s : std::string(), std::move(values));
        }
        out.push_back(std::move(a));
    }
    return out;
}

const std::vector<std::string>* element(const Annotation& a, const char* name) {
    for (const auto& [key, values] : a.elements) {
        if (key == name) return &values;
    }
    return nullptr;
}

std::string replaceDots(std::string s) {
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
}

}  // namespace

const std::string& ClassFile::utf8(std::uint16_t index) const {
    static const std::string kEmpty;
    return index < cp.size() && cp[index].tag == kUtf8 ? cp[index].s : kEmpty;
}

const std::string& ClassFile::className(std::uint16_t classIndex) const {
    static const std::string kEmpty;
    return classIndex < cp.size() && cp[classIndex].tag == kClass ? utf8(cp[classIndex].a) : kEmpty;
}

std::optional<ClassFile> ClassFile::parse(const std::uint8_t* data, std::size_t size) {
    Reader r{data, size};
    if (r.u4() != 0xCAFEBABEu) return std::nullopt;
    r.u2(), r.u2();  // minor, major
    ClassFile cf;
    const std::uint16_t cpCount = r.u2();
    cf.cp.resize(cpCount);
    for (std::uint16_t i = 1; i < cpCount && !r.bad; ++i) {
        CpEntry& e = cf.cp[i];
        e.tag = r.u1();
        switch (e.tag) {
            case kUtf8: {
                const std::uint16_t length = r.u2();
                if (!r.has(length)) return std::nullopt;
                e.s.assign(reinterpret_cast<const char*>(data + r.at), length);
                r.skip(length);
                break;
            }
            case kInteger: e.i = static_cast<std::int32_t>(r.u4()); break;
            case kFloat: {
                const std::uint32_t bits = r.u4();
                static_assert(sizeof(float) == 4);
                std::memcpy(&e.f, &bits, 4);
                break;
            }
            case kLong: {
                const std::uint64_t hi = r.u4(), lo = r.u4();
                e.j = static_cast<std::int64_t>((hi << 32) | lo);
                ++i;  // (takes two slots)
                break;
            }
            case kDouble: {
                const std::uint64_t hi = r.u4(), lo = r.u4();
                const std::uint64_t bits = (hi << 32) | lo;
                std::memcpy(&e.d, &bits, 8);
                ++i;
                break;
            }
            case kClass: case kString: case kMethodType: case kModule: case kPackage: e.a = r.u2(); break;
            case kFieldref: case kMethodref: case kInterfaceMethodref: case kNameAndType: case kDynamic: case kInvokeDynamic: e.a = r.u2(), e.b = r.u2(); break;
            case kMethodHandle: e.a = r.u1(), e.b = r.u2(); break;
            default: return std::nullopt;
        }
    }
    cf.access = r.u2();
    cf.name = cf.className(r.u2());
    cf.superName = cf.className(r.u2());
    const std::uint16_t interfaceCount = r.u2();
    for (std::uint16_t i = 0; i < interfaceCount && !r.bad; ++i) cf.interfaces.push_back(cf.className(r.u2()));

    for (int pass = 0; pass < 2; ++pass) {  // fields, then methods
        const std::uint16_t count = r.u2();
        for (std::uint16_t i = 0; i < count && !r.bad; ++i) {
            Member m;
            m.access = r.u2();
            m.name = cf.utf8(r.u2());
            m.desc = cf.utf8(r.u2());
            const std::uint16_t attributes = r.u2();
            for (std::uint16_t k = 0; k < attributes && !r.bad; ++k) {
                const std::string attribute = cf.utf8(r.u2());
                const std::uint32_t length = r.u4();
                const std::size_t end = r.at + length;
                if (end > size) return std::nullopt;
                if (attribute == "ConstantValue" && pass == 0) {
                    m.constantIndex = r.u2();
                } else if (attribute == "Signature" && pass == 0) {
                    m.signature = cf.utf8(r.u2());
                } else if (attribute == "Code" && pass == 1) {
                    m.hasCode = true;
                    m.code.maxStack = r.u2();
                    m.code.maxLocals = r.u2();
                    const std::uint32_t codeLength = r.u4();
                    if (!r.has(codeLength)) return std::nullopt;
                    m.code.bytes.assign(data + r.at, data + r.at + codeLength);
                    r.skip(codeLength);
                    const std::uint16_t handlers = r.u2();
                    for (std::uint16_t h = 0; h < handlers && !r.bad; ++h) {
                        Handler handler;
                        handler.start = r.u2(), handler.end = r.u2(), handler.handler = r.u2(), handler.catchType = r.u2();
                        m.code.handlers.push_back(handler);
                    }
                } else if ((attribute == "RuntimeInvisibleAnnotations" || attribute == "RuntimeVisibleAnnotations") && pass == 1) {
                    // An injector annotation on a method of a Mixin class: what it does and which game methods it names.
                    for (const Annotation& a : readAnnotations(r, cf)) {
                        const bool injection = a.type.find("/mixin/injection/") != std::string::npos || a.type.find("/mixin/gen/") != std::string::npos || a.type.find("mixinextras") != std::string::npos ||
                                               a.type.find("/mixin/Overwrite") != std::string::npos;
                        if (!injection) continue;
                        MixinHook hook;
                        const std::size_t slash = a.type.find_last_of('/');
                        hook.kind = a.type.substr(slash == std::string::npos ? 0 : slash + 1);
                        if (!hook.kind.empty() && hook.kind.back() == ';') hook.kind.pop_back();
                        hook.handler = m.name;
                        for (const char* key : {"method", "value", "target"}) {
                            if (const auto* values = element(a, key)) {
                                for (const std::string& v : *values) hook.targets.push_back(v);
                            }
                        }
                        cf.hooks.push_back(std::move(hook));
                    }
                }
                r.at = end;
            }
            (pass == 0 ? cf.fields : cf.methods).push_back(std::move(m));
        }
    }

    const std::uint16_t attributes = r.u2();
    for (std::uint16_t k = 0; k < attributes && !r.bad; ++k) {
        const std::string attribute = cf.utf8(r.u2());
        const std::uint32_t length = r.u4();
        const std::size_t end = r.at + length;
        if (end > size) return std::nullopt;
        if (attribute == "Signature") {
            cf.signature = cf.utf8(r.u2());
        } else if (attribute == "BootstrapMethods") {
            const std::uint16_t count = r.u2();
            for (std::uint16_t i = 0; i < count && !r.bad; ++i) {
                Bootstrap b;
                b.method = r.u2();
                const std::uint16_t args = r.u2();
                for (std::uint16_t a = 0; a < args && !r.bad; ++a) b.args.push_back(r.u2());
                cf.bootstraps.push_back(std::move(b));
            }
        } else if (attribute == "Record") {
            const std::uint16_t count = r.u2();
            for (std::uint16_t i = 0; i < count && !r.bad; ++i) {
                cf.record.push_back(cf.utf8(r.u2()));
                r.u2();  // descriptor
                const std::uint16_t componentAttributes = r.u2();
                for (std::uint16_t a = 0; a < componentAttributes && !r.bad; ++a) {
                    r.u2();
                    r.skip(r.u4());
                }
            }
        } else if (attribute == "RuntimeInvisibleAnnotations" || attribute == "RuntimeVisibleAnnotations") {
            for (const Annotation& a : readAnnotations(r, cf)) {
                if (a.type != "Lorg/spongepowered/asm/mixin/Mixin;") continue;
                for (const char* key : {"value", "targets"}) {
                    if (const auto* values = element(a, key)) {
                        for (const std::string& v : *values) cf.mixinTargets.push_back(replaceDots(v));
                    }
                }
            }
        }
        r.at = end;
    }
    if (r.bad) return std::nullopt;
    return cf;
}

}  // namespace falcon::jvm
