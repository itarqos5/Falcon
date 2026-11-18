// The parts of the Java class library a mod's start-up code leans on, written natively: Object, String, StringBuilder, the boxes, Math, System, Enum, the exceptions and the collections. Only
// what a mod would need to get through its initializer is here; anything else a mod calls on a java/ class is answered by a stand-in and listed as unsupported.
#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>

namespace falcon::jvm {
namespace {

Value I(std::int32_t v) { return valueOfInt(v); }
Value Z(bool v) { return valueOfInt(v ? 1 : 0); }
Value R(Object* o) { return valueOfRef(o); }
Value L(std::int64_t v) { return valueOfLong(v); }
Value F(float v) { return valueOfFloat(v); }
Value D(double v) { return valueOfDouble(v); }
Value V() { return Value(); }

#define NAT(cls, name, desc, ...) reg.defineNative(cls, name, desc, false, [](Vm & vm, Value * a) -> Value { (void)vm; (void)a; __VA_ARGS__ })
#define STAT(cls, name, desc, ...) reg.defineNative(cls, name, desc, true, [](Vm & vm, Value * a) -> Value { (void)vm; (void)a; __VA_ARGS__ })

std::string javaFloating(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", d);
    std::string s = buffer;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

std::string primText(char type, Value v) {
    switch (type) {
        case 'Z': return v.i != 0 ? "true" : "false";
        case 'J': return std::to_string(v.j);
        case 'F': return javaFloating(static_cast<double>(v.f));
        case 'D': return javaFloating(v.d);
        case 'C': return std::string(1, static_cast<char>(v.i));
        default: return std::to_string(v.i);
    }
}

// Lists, sets and the like keep their elements in `elems`.
bool containsElement(Vm& vm, Object* list, Object* e) {
    for (const Value& v : list->elems) {
        if (vm.equalsJava(v.a, e)) return true;
    }
    return false;
}
int indexOfElement(Vm& vm, Object* list, Object* e) {
    for (std::size_t i = 0; i < list->elems.size(); ++i) {
        if (vm.equalsJava(list->elems[i].a, e)) return static_cast<int>(i);
    }
    return -1;
}

Object* newList(Vm& vm, const char* cls = "java/util/ArrayList") { return vm.newObject(cls); }

Object* iteratorOf(Vm& vm, Object* collection) {
    Object* it = vm.newObject("java/util/Iterator");
    it->kind = Kind::Iterator;
    it->elems = collection->elems;
    it->elemType = 'L';
    return it;
}

// A collection's contents for the methods that take any: a list, a set, an array, or the keys of what is neither.
std::vector<Value> contentsOf(Vm& vm, Object* c) {
    if (c == nullptr) vm.throwNew("java/lang/NullPointerException");
    return c->elems;
}

std::string formatJava(Vm& vm, const std::string& format, Object* args) {
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') {
            out.push_back(format[i]);
            continue;
        }
        std::size_t j = i + 1;
        std::string flags;
        while (j < format.size() && std::strchr("-+ 0,#(", format[j]) != nullptr) flags.push_back(format[j++]);
        int width = 0, precision = -1;
        while (j < format.size() && std::isdigit(static_cast<unsigned char>(format[j]))) width = width * 10 + (format[j++] - '0');
        if (j < format.size() && format[j] == '.') {
            ++j;
            precision = 0;
            while (j < format.size() && std::isdigit(static_cast<unsigned char>(format[j]))) precision = precision * 10 + (format[j++] - '0');
        }
        if (j >= format.size()) break;
        const char conv = format[j];
        i = j;
        if (conv == 'n') { out.push_back('\n'); continue; }
        if (conv == '%') { out.push_back('%'); continue; }
        Object* arg = args != nullptr && next < args->elems.size() ? args->elems[next].a : nullptr;
        ++next;
        std::string text;
        if (conv == 'd' || conv == 'x' || conv == 'X') {
            const std::int64_t v = arg != nullptr && arg->kind == Kind::Box ? (arg->primType == 'J' ? arg->prim.j : arg->prim.i) : 0;
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), conv == 'd' ? "%lld" : conv == 'x' ? "%llx" : "%llX", static_cast<long long>(v));
            text = buffer;
        } else if (conv == 'f') {
            const double v = arg != nullptr && arg->kind == Kind::Box ? (arg->primType == 'F' ? static_cast<double>(arg->prim.f) : arg->prim.d) : 0.0;
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision < 0 ? 6 : precision, v);
            text = buffer;
        } else {
            text = vm.toJavaString(arg);
            if (conv == 'S') std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (precision >= 0 && static_cast<int>(text.size()) > precision) text.resize(static_cast<std::size_t>(precision));
        }
        if (static_cast<int>(text.size()) < width) {
            const bool zero = flags.find('0') != std::string::npos && conv != 's';
            const std::string pad(static_cast<std::size_t>(width) - text.size(), zero ? '0' : ' ');
            text = flags.find('-') != std::string::npos ? text + pad : pad + text;
        }
        out += text;
    }
    return out;
}

void installLang(Vm& reg) {
    // ---- the hierarchy ----
    for (const char* name : {"java/lang/CharSequence", "java/lang/Comparable", "java/lang/Runnable", "java/lang/AutoCloseable", "java/lang/Cloneable", "java/lang/Iterable", "java/io/Serializable"}) reg.defineClass(name, "java/lang/Object", {}, true);
    reg.defineClass("java/lang/Class", "java/lang/Object");
    reg.defineClass("java/lang/String", "java/lang/Object", {"java/lang/CharSequence", "java/lang/Comparable"});
    reg.defineClass("java/lang/Number", "java/lang/Object");
    for (const char* name : {"java/lang/Integer", "java/lang/Long", "java/lang/Double", "java/lang/Float", "java/lang/Short", "java/lang/Byte"}) reg.defineClass(name, "java/lang/Number", {"java/lang/Comparable"});
    reg.defineClass("java/lang/Boolean", "java/lang/Object", {"java/lang/Comparable"});
    reg.defineClass("java/lang/Character", "java/lang/Object", {"java/lang/Comparable"});
    reg.defineClass("java/lang/Math", "java/lang/Object");
    reg.defineClass("java/lang/System", "java/lang/Object");
    reg.defineClass("java/lang/StringBuilder", "java/lang/Object", {"java/lang/CharSequence"});
    reg.defineClass("java/lang/StringBuffer", "java/lang/StringBuilder");
    reg.defineClass("java/lang/Enum", "java/lang/Object", {"java/lang/Comparable"});
    reg.defineClass("java/lang/Record", "java/lang/Object");
    reg.defineClass("java/lang/Throwable", "java/lang/Object");
    const std::vector<std::pair<const char*, const char*>> exceptions = {
        {"java/lang/Exception", "java/lang/Throwable"}, {"java/lang/Error", "java/lang/Throwable"}, {"java/lang/RuntimeException", "java/lang/Exception"},
        {"java/lang/IllegalArgumentException", "java/lang/RuntimeException"}, {"java/lang/NumberFormatException", "java/lang/IllegalArgumentException"},
        {"java/lang/IllegalStateException", "java/lang/RuntimeException"}, {"java/lang/NullPointerException", "java/lang/RuntimeException"},
        {"java/lang/UnsupportedOperationException", "java/lang/RuntimeException"}, {"java/lang/ArithmeticException", "java/lang/RuntimeException"},
        {"java/lang/ClassCastException", "java/lang/RuntimeException"}, {"java/lang/IndexOutOfBoundsException", "java/lang/RuntimeException"},
        {"java/lang/ArrayIndexOutOfBoundsException", "java/lang/IndexOutOfBoundsException"}, {"java/lang/StringIndexOutOfBoundsException", "java/lang/IndexOutOfBoundsException"},
        {"java/lang/NegativeArraySizeException", "java/lang/RuntimeException"}, {"java/lang/ArrayStoreException", "java/lang/RuntimeException"}, {"java/util/NoSuchElementException", "java/lang/RuntimeException"},
        {"java/util/ConcurrentModificationException", "java/lang/RuntimeException"}, {"java/lang/InterruptedException", "java/lang/Exception"}, {"java/lang/CloneNotSupportedException", "java/lang/Exception"},
        {"java/lang/ReflectiveOperationException", "java/lang/Exception"}, {"java/lang/ClassNotFoundException", "java/lang/ReflectiveOperationException"}, {"java/io/IOException", "java/lang/Exception"},
        {"java/io/UncheckedIOException", "java/lang/RuntimeException"}, {"java/lang/AssertionError", "java/lang/Error"}, {"java/lang/VirtualMachineError", "java/lang/Error"},
        {"java/lang/StackOverflowError", "java/lang/VirtualMachineError"}, {"java/lang/OutOfMemoryError", "java/lang/VirtualMachineError"}, {"java/lang/LinkageError", "java/lang/Error"},
        {"java/lang/IncompatibleClassChangeError", "java/lang/LinkageError"}, {"java/lang/AbstractMethodError", "java/lang/IncompatibleClassChangeError"},
        {"java/lang/NoSuchMethodError", "java/lang/IncompatibleClassChangeError"}, {"java/lang/NoSuchFieldError", "java/lang/IncompatibleClassChangeError"}, {"java/lang/BootstrapMethodError", "java/lang/LinkageError"},
        {"java/lang/VerifyError", "java/lang/LinkageError"}, {"java/lang/ExceptionInInitializerError", "java/lang/LinkageError"}};
    for (const auto& [name, super] : exceptions) reg.defineClass(name, super);

    // ---- Object and Class ----
    NAT("java/lang/Object", "<init>", "()V", return V(););
    NAT("java/lang/Object", "toString", "()Ljava/lang/String;", return R(vm.newString(vm.describeObject(a[0].a)));
    );
    NAT("java/lang/Object", "hashCode", "()I", return I(vm.hashJava(a[0].a)););
    NAT("java/lang/Object", "equals", "(Ljava/lang/Object;)Z", return Z(a[0].a == a[1].a || vm.equalsJava(a[0].a, a[1].a)););
    NAT("java/lang/Object", "getClass", "()Ljava/lang/Class;", return R(vm.classObject(a[0].a->cls->name)););
    NAT("java/lang/Object", "clone", "()Ljava/lang/Object;", {
        Object* o = a[0].a;
        Object* copy = vm.newObject(o->cls);
        copy->kind = o->kind;
        copy->elems = o->elems;
        copy->elemType = o->elemType;
        copy->fields = o->fields;
        copy->str = o->str;
        return R(copy);
    });
    for (const char* name : {"notify", "notifyAll", "wait"}) reg.defineNative("java/lang/Object", name, "()V", false, [](Vm&, Value*) { return V(); });
    NAT("java/lang/Class", "getName", "()Ljava/lang/String;", {
        std::string n = vm.str(a[0].a);
        std::replace(n.begin(), n.end(), '/', '.');
        return R(vm.newString(n));
    });
    NAT("java/lang/Class", "getSimpleName", "()Ljava/lang/String;", {
        const std::string& n = vm.str(a[0].a);
        const std::size_t at = n.find_last_of("/$");
        return R(vm.newString(at == std::string::npos ? n : n.substr(at + 1)));
    });
    NAT("java/lang/Class", "toString", "()Ljava/lang/String;", return R(vm.newString("class " + vm.str(a[0].a))););
    NAT("java/lang/Class", "desiredAssertionStatus", "()Z", return Z(false););
    NAT("java/lang/Class", "isInstance", "(Ljava/lang/Object;)Z", return Z(vm.instanceOf(a[1].a, vm.loadClass(vm.str(a[0].a)))););

    // ---- String ----
    NAT("java/lang/String", "<init>", "()V", a[0].a->kind = Kind::String; return V(););
    NAT("java/lang/String", "<init>", "(Ljava/lang/String;)V", a[0].a->kind = Kind::String; a[0].a->str = vm.str(a[1].a); return V(););
    NAT("java/lang/String", "<init>", "([C)V", {
        a[0].a->kind = Kind::String;
        for (const Value& v : a[1].a->elems) a[0].a->str.push_back(static_cast<char>(v.i));
        return V();
    });
    // (a String holds its bytes: UTF-8 is what it already is)
    NAT("java/lang/String", "<init>", "([B)V", {
        a[0].a->kind = Kind::String;
        for (const Value& v : a[1].a->elems) a[0].a->str.push_back(static_cast<char>(v.i));
        return V();
    });
    NAT("java/lang/String", "<init>", "([BLjava/nio/charset/Charset;)V", {
        a[0].a->kind = Kind::String;
        for (const Value& v : a[1].a->elems) a[0].a->str.push_back(static_cast<char>(v.i));
        return V();
    });
    NAT("java/lang/String", "<init>", "([BII)V", {
        a[0].a->kind = Kind::String;
        for (int i = a[2].i; i < a[2].i + a[3].i && i < static_cast<int>(a[1].a->elems.size()); ++i) a[0].a->str.push_back(static_cast<char>(a[1].a->elems[static_cast<std::size_t>(i)].i));
        return V();
    });
    NAT("java/lang/String", "getBytes", "()[B", {
        const std::string& str = vm.str(a[0].a);
        Object* bytes = vm.newArray('B', static_cast<int>(str.size()));
        for (std::size_t i = 0; i < str.size(); ++i) bytes->elems[i] = I(static_cast<signed char>(str[i]));
        return R(bytes);
    });
    NAT("java/lang/String", "getBytes", "(Ljava/nio/charset/Charset;)[B", {
        const std::string& str = vm.str(a[0].a);
        Object* bytes = vm.newArray('B', static_cast<int>(str.size()));
        for (std::size_t i = 0; i < str.size(); ++i) bytes->elems[i] = I(static_cast<signed char>(str[i]));
        return R(bytes);
    });
    NAT("java/lang/String", "trim", "()Ljava/lang/String;", {
        const std::string& str = vm.str(a[0].a);
        std::size_t b = 0, e = str.size();
        while (b < e && static_cast<unsigned char>(str[b]) <= ' ') ++b;
        while (e > b && static_cast<unsigned char>(str[e - 1]) <= ' ') --e;
        return R(vm.newString(str.substr(b, e - b)));
    });
    NAT("java/lang/String", "strip", "()Ljava/lang/String;", {
        const std::string& str = vm.str(a[0].a);
        std::size_t b = 0, e = str.size();
        while (b < e && std::isspace(static_cast<unsigned char>(str[b])) != 0) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(str[e - 1])) != 0) --e;
        return R(vm.newString(str.substr(b, e - b)));
    });
    NAT("java/lang/String", "length", "()I", return I(static_cast<int>(vm.str(a[0].a).size())););
    NAT("java/lang/String", "isEmpty", "()Z", return Z(vm.str(a[0].a).empty()););
    NAT("java/lang/String", "isBlank", "()Z", {
        const std::string& s = vm.str(a[0].a);
        return Z(std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }));
    });
    NAT("java/lang/String", "charAt", "(I)C", {
        const std::string& s = vm.str(a[0].a);
        if (a[1].i < 0 || static_cast<std::size_t>(a[1].i) >= s.size()) vm.throwNew("java/lang/StringIndexOutOfBoundsException", "Index " + std::to_string(a[1].i) + " out of bounds for length " + std::to_string(s.size()));
        return I(static_cast<unsigned char>(s[static_cast<std::size_t>(a[1].i)]));
    });
    NAT("java/lang/String", "equals", "(Ljava/lang/Object;)Z", return Z(a[1].a != nullptr && a[1].a->kind == Kind::String && a[1].a->str == a[0].a->str););
    NAT("java/lang/String", "equalsIgnoreCase", "(Ljava/lang/String;)Z", {
        if (a[1].a == nullptr) return Z(false);
        std::string x = a[0].a->str, y = a[1].a->str;
        for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Z(x == y);
    });
    NAT("java/lang/String", "hashCode", "()I", return I(vm.hashJava(a[0].a)););
    NAT("java/lang/String", "toString", "()Ljava/lang/String;", return a[0];);
    NAT("java/lang/String", "intern", "()Ljava/lang/String;", return R(vm.intern(a[0].a->str)););
    NAT("java/lang/String", "concat", "(Ljava/lang/String;)Ljava/lang/String;", return R(vm.newString(a[0].a->str + vm.str(a[1].a))););
    NAT("java/lang/String", "compareTo", "(Ljava/lang/String;)I", return I(a[0].a->str.compare(vm.str(a[1].a))););
    NAT("java/lang/String", "substring", "(I)Ljava/lang/String;", {
        const std::string& s = a[0].a->str;
        if (a[1].i < 0 || static_cast<std::size_t>(a[1].i) > s.size()) vm.throwNew("java/lang/StringIndexOutOfBoundsException", "begin " + std::to_string(a[1].i));
        return R(vm.newString(s.substr(static_cast<std::size_t>(a[1].i))));
    });
    NAT("java/lang/String", "substring", "(II)Ljava/lang/String;", {
        const std::string& s = a[0].a->str;
        if (a[1].i < 0 || a[2].i < a[1].i || static_cast<std::size_t>(a[2].i) > s.size()) vm.throwNew("java/lang/StringIndexOutOfBoundsException", "begin " + std::to_string(a[1].i) + ", end " + std::to_string(a[2].i));
        return R(vm.newString(s.substr(static_cast<std::size_t>(a[1].i), static_cast<std::size_t>(a[2].i - a[1].i))));
    });
    NAT("java/lang/String", "indexOf", "(Ljava/lang/String;)I", { const auto at = a[0].a->str.find(vm.str(a[1].a)); return I(at == std::string::npos ? -1 : static_cast<int>(at)); });
    NAT("java/lang/String", "indexOf", "(I)I", { const auto at = a[0].a->str.find(static_cast<char>(a[1].i)); return I(at == std::string::npos ? -1 : static_cast<int>(at)); });
    NAT("java/lang/String", "lastIndexOf", "(I)I", { const auto at = a[0].a->str.rfind(static_cast<char>(a[1].i)); return I(at == std::string::npos ? -1 : static_cast<int>(at)); });
    NAT("java/lang/String", "contains", "(Ljava/lang/CharSequence;)Z", return Z(a[0].a->str.find(vm.toJavaString(a[1].a)) != std::string::npos););
    NAT("java/lang/String", "startsWith", "(Ljava/lang/String;)Z", return Z(a[0].a->str.rfind(vm.str(a[1].a), 0) == 0););
    NAT("java/lang/String", "endsWith", "(Ljava/lang/String;)Z", {
        const std::string& s = a[0].a->str;
        const std::string& t = vm.str(a[1].a);
        return Z(s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0);
    });
    NAT("java/lang/String", "toLowerCase", "()Ljava/lang/String;", { std::string s = a[0].a->str; for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return R(vm.newString(s)); });
    NAT("java/lang/String", "toUpperCase", "()Ljava/lang/String;", { std::string s = a[0].a->str; for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return R(vm.newString(s)); });
    NAT("java/lang/String", "toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;", { std::string s = a[0].a->str; for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return R(vm.newString(s)); });
    NAT("java/lang/String", "toUpperCase", "(Ljava/util/Locale;)Ljava/lang/String;", { std::string s = a[0].a->str; for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return R(vm.newString(s)); });
    for (const char* name : {"trim", "strip"}) {
        reg.defineNative("java/lang/String", name, "()Ljava/lang/String;", false, [](Vm& vm, Value* a) {
            const std::string& s = a[0].a->str;
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return R(vm.newString(s.substr(b, e - b)));
        });
    }
    NAT("java/lang/String", "replace", "(CC)Ljava/lang/String;", { std::string s = a[0].a->str; std::replace(s.begin(), s.end(), static_cast<char>(a[1].i), static_cast<char>(a[2].i)); return R(vm.newString(s)); });
    NAT("java/lang/String", "replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;", {
        std::string s = a[0].a->str;
        const std::string from = vm.toJavaString(a[1].a), to = vm.toJavaString(a[2].a);
        if (!from.empty()) {
            for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
        }
        return R(vm.newString(s));
    });
    NAT("java/lang/String", "repeat", "(I)Ljava/lang/String;", { std::string s; for (int i = 0; i < a[1].i && s.size() < (1u << 20); ++i) s += a[0].a->str; return R(vm.newString(s)); });
    NAT("java/lang/String", "toCharArray", "()[C", {
        Object* arr = vm.newArray('C', static_cast<int>(a[0].a->str.size()));
        for (std::size_t i = 0; i < a[0].a->str.size(); ++i) arr->elems[i] = I(static_cast<unsigned char>(a[0].a->str[i]));
        return R(arr);
    });
    NAT("java/lang/String", "split", "(Ljava/lang/String;)[Ljava/lang/String;", {
        const std::string& s = a[0].a->str;
        const std::string sep = vm.str(a[1].a);  // (a plain separator, not a pattern)
        std::vector<std::string> parts;
        std::size_t from = 0;
        while (!sep.empty()) {
            const std::size_t at = s.find(sep, from);
            if (at == std::string::npos) break;
            parts.push_back(s.substr(from, at - from));
            from = at + sep.size();
        }
        parts.push_back(s.substr(from));
        while (parts.size() > 1 && parts.back().empty()) parts.pop_back();
        Object* arr = vm.newArray('L', static_cast<int>(parts.size()));
        for (std::size_t i = 0; i < parts.size(); ++i) arr->elems[i] = R(vm.newString(parts[i]));
        return R(arr);
    });
    STAT("java/lang/String", "valueOf", "(Ljava/lang/Object;)Ljava/lang/String;", return R(vm.newString(vm.toJavaString(a[0].a))););
    STAT("java/lang/String", "valueOf", "(I)Ljava/lang/String;", return R(vm.newString(std::to_string(a[0].i))););
    STAT("java/lang/String", "valueOf", "(J)Ljava/lang/String;", return R(vm.newString(std::to_string(a[0].j))););
    STAT("java/lang/String", "valueOf", "(Z)Ljava/lang/String;", return R(vm.newString(a[0].i != 0 ? "true" : "false")););
    STAT("java/lang/String", "valueOf", "(C)Ljava/lang/String;", return R(vm.newString(std::string(1, static_cast<char>(a[0].i)))););
    STAT("java/lang/String", "valueOf", "(F)Ljava/lang/String;", return R(vm.newString(javaFloating(static_cast<double>(a[0].f)))););
    STAT("java/lang/String", "valueOf", "(D)Ljava/lang/String;", return R(vm.newString(javaFloating(a[0].d))););
    STAT("java/lang/String", "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;", return R(vm.newString(formatJava(vm, vm.str(a[0].a), a[1].a))););
    STAT("java/lang/String", "join", "(Ljava/lang/CharSequence;[Ljava/lang/CharSequence;)Ljava/lang/String;", {
        std::string out;
        const std::string sep = vm.toJavaString(a[0].a);
        for (std::size_t i = 0; i < a[1].a->elems.size(); ++i) out += (i ? sep : std::string()) + vm.toJavaString(a[1].a->elems[i].a);
        return R(vm.newString(out));
    });
    STAT("java/lang/String", "join", "(Ljava/lang/CharSequence;Ljava/lang/Iterable;)Ljava/lang/String;", {
        std::string out;
        const std::string sep = vm.toJavaString(a[0].a);
        const std::vector<Value> items = contentsOf(vm, a[1].a);
        for (std::size_t i = 0; i < items.size(); ++i) out += (i ? sep : std::string()) + vm.toJavaString(items[i].a);
        return R(vm.newString(out));
    });
    NAT("java/lang/CharSequence", "toString", "()Ljava/lang/String;", return R(vm.newString(vm.toJavaString(a[0].a))););

    // ---- StringBuilder ----
    reg.defineNative("java/lang/StringBuilder", "<init>", "()V", false, [](Vm&, Value*) { return V(); });
    NAT("java/lang/StringBuilder", "<init>", "(I)V", return V(););
    NAT("java/lang/StringBuilder", "<init>", "(Ljava/lang/String;)V", a[0].a->str = vm.str(a[1].a); return V(););
    const auto append = [&](const char* desc, std::function<std::string(Vm&, Value)> text) {
        reg.defineNative("java/lang/StringBuilder", "append", desc, false, [text](Vm& v, Value* a) {
            a[0].a->str += text(v, a[1]);
            return a[0];
        });
    };
    append("(Ljava/lang/String;)Ljava/lang/StringBuilder;", [](Vm& v, Value x) { return v.toJavaString(x.a); });
    append("(Ljava/lang/Object;)Ljava/lang/StringBuilder;", [](Vm& v, Value x) { return v.toJavaString(x.a); });
    append("(Ljava/lang/CharSequence;)Ljava/lang/StringBuilder;", [](Vm& v, Value x) { return v.toJavaString(x.a); });
    append("(I)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return std::to_string(x.i); });
    append("(J)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return std::to_string(x.j); });
    append("(C)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return std::string(1, static_cast<char>(x.i)); });
    append("(Z)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return std::string(x.i != 0 ? "true" : "false"); });
    append("(F)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return javaFloating(static_cast<double>(x.f)); });
    append("(D)Ljava/lang/StringBuilder;", [](Vm&, Value x) { return javaFloating(x.d); });
    NAT("java/lang/StringBuilder", "toString", "()Ljava/lang/String;", return R(vm.newString(a[0].a->str)););
    NAT("java/lang/StringBuilder", "length", "()I", return I(static_cast<int>(a[0].a->str.size())););
    NAT("java/lang/StringBuilder", "isEmpty", "()Z", return Z(a[0].a->str.empty()););
    NAT("java/lang/StringBuilder", "charAt", "(I)C", return I(static_cast<unsigned char>(a[0].a->str.at(static_cast<std::size_t>(a[1].i)))););
    NAT("java/lang/StringBuilder", "reverse", "()Ljava/lang/StringBuilder;", std::reverse(a[0].a->str.begin(), a[0].a->str.end()); return a[0];);
    NAT("java/lang/StringBuilder", "setLength", "(I)V", a[0].a->str.resize(static_cast<std::size_t>(std::max(0, a[1].i))); return V(););
    NAT("java/lang/StringBuilder", "insert", "(ILjava/lang/String;)Ljava/lang/StringBuilder;", a[0].a->str.insert(static_cast<std::size_t>(a[1].i), vm.toJavaString(a[2].a)); return a[0];);
    NAT("java/lang/StringBuilder", "deleteCharAt", "(I)Ljava/lang/StringBuilder;", a[0].a->str.erase(static_cast<std::size_t>(a[1].i), 1); return a[0];);

    // ---- the boxes ----
    struct BoxType {
        const char* cls;
        char type;
        const char* prim;  // the descriptor character
        const char* value; // the xxxValue method
    };
    const BoxType boxes[] = {{"java/lang/Integer", 'I', "I", "intValue"}, {"java/lang/Long", 'J', "J", "longValue"}, {"java/lang/Float", 'F', "F", "floatValue"}, {"java/lang/Double", 'D', "D", "doubleValue"},
                             {"java/lang/Short", 'S', "S", "shortValue"}, {"java/lang/Byte", 'B', "B", "byteValue"}, {"java/lang/Boolean", 'Z', "Z", "booleanValue"}, {"java/lang/Character", 'C', "C", "charValue"}};
    for (const BoxType& b : boxes) {
        const std::string cls = b.cls, prim = b.prim, self = "L" + cls + ";";
        const char type = b.type;
        reg.defineNative(cls, "valueOf", "(" + prim + ")" + self, true, [type](Vm& v, Value* a) { return R(v.box(type, a[0])); });
        reg.defineNative(cls, b.value, "()" + prim, false, [](Vm&, Value* a) { return a[0].a->prim; });
        reg.defineNative(cls, "toString", "()Ljava/lang/String;", false, [type](Vm& v, Value* a) { return R(v.newString(primText(type, a[0].a->prim))); });
        reg.defineNative(cls, "toString", "(" + prim + ")Ljava/lang/String;", true, [type](Vm& v, Value* a) { return R(v.newString(primText(type, a[0]))); });
        reg.defineNative(cls, "hashCode", "()I", false, [](Vm& v, Value* a) { return I(v.hashJava(a[0].a)); });
        reg.defineNative(cls, "equals", "(Ljava/lang/Object;)Z", false, [](Vm& v, Value* a) { return Z(v.equalsJava(a[0].a, a[1].a)); });
        reg.defineNative(cls, "compareTo", "(" + self + ")I", false, [type](Vm&, Value* a) {
            const Value x = a[0].a->prim, y = a[1].a->prim;
            if (type == 'J') return I(x.j < y.j ? -1 : x.j > y.j ? 1 : 0);
            if (type == 'D') return I(x.d < y.d ? -1 : x.d > y.d ? 1 : 0);
            if (type == 'F') return I(x.f < y.f ? -1 : x.f > y.f ? 1 : 0);
            return I(x.i < y.i ? -1 : x.i > y.i ? 1 : 0);
        });
    }
    STAT("java/lang/Integer", "parseInt", "(Ljava/lang/String;)I", {
        const std::string& s = vm.str(a[0].a);
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') vm.throwNew("java/lang/NumberFormatException", "For input string: \"" + s + "\"");
        return I(static_cast<int>(v));
    });
    STAT("java/lang/Integer", "compare", "(II)I", return I(a[0].i < a[1].i ? -1 : a[0].i > a[1].i ? 1 : 0););
    STAT("java/lang/Integer", "toHexString", "(I)Ljava/lang/String;", { char b[16]; std::snprintf(b, sizeof(b), "%x", static_cast<unsigned>(a[0].i)); return R(vm.newString(b)); });
    STAT("java/lang/Long", "parseLong", "(Ljava/lang/String;)J", {
        const std::string& s = vm.str(a[0].a);
        char* end = nullptr;
        const long long v = std::strtoll(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') vm.throwNew("java/lang/NumberFormatException", "For input string: \"" + s + "\"");
        return L(v);
    });
    STAT("java/lang/Double", "parseDouble", "(Ljava/lang/String;)D", return D(std::strtod(vm.str(a[0].a).c_str(), nullptr)););
    STAT("java/lang/Float", "parseFloat", "(Ljava/lang/String;)F", return F(std::strtof(vm.str(a[0].a).c_str(), nullptr)););
    STAT("java/lang/Boolean", "parseBoolean", "(Ljava/lang/String;)Z", return Z(a[0].a != nullptr && a[0].a->str == "true"););
    STAT("java/lang/Double", "compare", "(DD)I", return I(a[0].d < a[1].d ? -1 : a[0].d > a[1].d ? 1 : 0););
    STAT("java/lang/Character", "isDigit", "(C)Z", return Z(a[0].i >= '0' && a[0].i <= '9'););
    STAT("java/lang/Character", "isLetter", "(C)Z", return Z(std::isalpha(a[0].i & 0xFF) != 0 && a[0].i < 128););
    STAT("java/lang/Character", "toUpperCase", "(C)C", return I(a[0].i < 128 ? std::toupper(a[0].i) : a[0].i););
    STAT("java/lang/Character", "toLowerCase", "(C)C", return I(a[0].i < 128 ? std::tolower(a[0].i) : a[0].i););

    // ---- Math and System ----
    STAT("java/lang/Math", "abs", "(I)I", return I(a[0].i < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(a[0].i)) : a[0].i););
    STAT("java/lang/Math", "abs", "(J)J", return L(a[0].j < 0 ? static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(a[0].j)) : a[0].j););
    STAT("java/lang/Math", "abs", "(F)F", return F(std::fabs(a[0].f)););
    STAT("java/lang/Math", "abs", "(D)D", return D(std::fabs(a[0].d)););
    STAT("java/lang/Math", "max", "(II)I", return I(std::max(a[0].i, a[1].i)););
    STAT("java/lang/Math", "min", "(II)I", return I(std::min(a[0].i, a[1].i)););
    STAT("java/lang/Math", "max", "(JJ)J", return L(std::max(a[0].j, a[1].j)););
    STAT("java/lang/Math", "min", "(JJ)J", return L(std::min(a[0].j, a[1].j)););
    STAT("java/lang/Math", "max", "(FF)F", return F(std::fmax(a[0].f, a[1].f)););
    STAT("java/lang/Math", "min", "(FF)F", return F(std::fmin(a[0].f, a[1].f)););
    STAT("java/lang/Math", "max", "(DD)D", return D(std::fmax(a[0].d, a[1].d)););
    STAT("java/lang/Math", "min", "(DD)D", return D(std::fmin(a[0].d, a[1].d)););
    STAT("java/lang/Math", "clamp", "(JII)I", return I(static_cast<int>(std::min<std::int64_t>(std::max<std::int64_t>(a[0].j, a[1].i), a[2].i))););
    STAT("java/lang/Math", "pow", "(DD)D", return D(std::pow(a[0].d, a[1].d)););
    STAT("java/lang/Math", "sqrt", "(D)D", return D(std::sqrt(a[0].d)););
    STAT("java/lang/Math", "floor", "(D)D", return D(std::floor(a[0].d)););
    STAT("java/lang/Math", "ceil", "(D)D", return D(std::ceil(a[0].d)););
    STAT("java/lang/Math", "sin", "(D)D", return D(std::sin(a[0].d)););
    STAT("java/lang/Math", "cos", "(D)D", return D(std::cos(a[0].d)););
    STAT("java/lang/Math", "round", "(F)I", return I(static_cast<int>(std::floor(static_cast<double>(a[0].f) + 0.5))););
    STAT("java/lang/Math", "round", "(D)J", return L(static_cast<std::int64_t>(std::floor(a[0].d + 0.5))););
    STAT("java/lang/Math", "floorMod", "(II)I", { if (a[1].i == 0) vm.throwNew("java/lang/ArithmeticException", "/ by zero"); const int m = a[0].i % a[1].i; return I(m != 0 && ((m < 0) != (a[1].i < 0)) ? m + a[1].i : m); });
    STAT("java/lang/System", "currentTimeMillis", "()J", return L(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()););
    STAT("java/lang/System", "nanoTime", "()J", return L(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()););
    STAT("java/lang/System", "getProperty", "(Ljava/lang/String;)Ljava/lang/String;", return R(nullptr););
    STAT("java/lang/System", "getenv", "(Ljava/lang/String;)Ljava/lang/String;", return R(nullptr););
    STAT("java/lang/System", "lineSeparator", "()Ljava/lang/String;", return R(vm.intern("\n")););
    STAT("java/lang/System", "identityHashCode", "(Ljava/lang/Object;)I", return I(vm.hashJava(a[0].a)););
    STAT("java/lang/System", "exit", "(I)V", throw BudgetExceeded{"the mod called System.exit"};);
    STAT("java/lang/System", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", {
        Object* src = a[0].a;
        Object* dst = a[2].a;
        if (src == nullptr || dst == nullptr) vm.throwNew("java/lang/NullPointerException");
        const int sp = a[1].i, dp = a[3].i, n = a[4].i;
        if (sp < 0 || dp < 0 || n < 0 || static_cast<std::size_t>(sp) + n > src->elems.size() || static_cast<std::size_t>(dp) + n > dst->elems.size()) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException", "arraycopy: last index out of bounds");
        std::vector<Value> tmp(src->elems.begin() + sp, src->elems.begin() + sp + n);
        std::copy(tmp.begin(), tmp.end(), dst->elems.begin() + dp);
        return V();
    });

    // ---- Enum, Throwable ----
    NAT("java/lang/Enum", "<init>", "(Ljava/lang/String;I)V", a[0].a->str = vm.str(a[1].a); a[0].a->position = a[2].i; return V(););
    NAT("java/lang/Enum", "name", "()Ljava/lang/String;", return R(vm.newString(a[0].a->str)););
    NAT("java/lang/Enum", "toString", "()Ljava/lang/String;", return R(vm.newString(a[0].a->str)););
    NAT("java/lang/Enum", "ordinal", "()I", return I(a[0].a->position););
    NAT("java/lang/Enum", "compareTo", "(Ljava/lang/Enum;)I", return I(a[0].a->position - a[1].a->position););
    NAT("java/lang/Enum", "equals", "(Ljava/lang/Object;)Z", return Z(a[0].a == a[1].a););
    NAT("java/lang/Enum", "hashCode", "()I", return I(a[0].a->position * 31 + 7););
    STAT("java/lang/Enum", "valueOf", "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;", {
        Class* c = vm.loadClass(vm.str(a[0].a));
        vm.ensureInitialized(c);
        Method* values = vm.findMethod(c, "values", "()[L" + c->name + ";");
        if (values != nullptr) {
            const Value all = vm.invoke(values, nullptr, 0);
            for (const Value& e : all.a->elems) {
                if (e.a != nullptr && e.a->str == vm.str(a[1].a)) return e;
            }
        }
        vm.throwNew("java/lang/IllegalArgumentException", "No enum constant " + vm.str(a[0].a) + "." + vm.str(a[1].a));
    });
    NAT("java/lang/Enum", "getDeclaringClass", "()Ljava/lang/Class;", {
        Class* c = a[0].a->cls;
        if (c->super != nullptr && c->super->name != "java/lang/Enum") c = c->super;  // (a constant with a body is a subclass)
        return R(vm.classObject(c->name));
    });
    reg.defineClass("java/lang/reflect/Array", "java/lang/Object");
    STAT("java/lang/reflect/Array", "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;", return R(vm.newArray('L', a[1].i)););
    NAT("java/lang/Record", "<init>", "()V", return V(););
    NAT("java/lang/Throwable", "<init>", "()V", return V(););
    NAT("java/lang/Throwable", "<init>", "(Ljava/lang/String;)V", a[0].a->str = a[1].a != nullptr ? a[1].a->str : std::string(); a[0].a->primType = a[1].a != nullptr; return V(););
    NAT("java/lang/Throwable", "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V", a[0].a->str = a[1].a != nullptr ? a[1].a->str : std::string(); a[0].a->primType = a[1].a != nullptr; a[0].a->captured = {a[2]}; return V(););
    NAT("java/lang/Throwable", "<init>", "(Ljava/lang/Throwable;)V", {
        a[0].a->captured = {a[1]};
        if (a[1].a != nullptr) { a[0].a->str = a[1].a->cls->name + (a[1].a->primType != 0 ? ": " + a[1].a->str : std::string()); a[0].a->primType = 1; }
        return V();
    });
    NAT("java/lang/Throwable", "getMessage", "()Ljava/lang/String;", return a[0].a->primType != 0 ? R(vm.newString(a[0].a->str)) : R(nullptr););
    NAT("java/lang/Throwable", "getLocalizedMessage", "()Ljava/lang/String;", return a[0].a->primType != 0 ? R(vm.newString(a[0].a->str)) : R(nullptr););
    NAT("java/lang/Throwable", "getCause", "()Ljava/lang/Throwable;", return a[0].a->captured.empty() ? R(nullptr) : a[0].a->captured[0];);
    NAT("java/lang/Throwable", "initCause", "(Ljava/lang/Throwable;)Ljava/lang/Throwable;", a[0].a->captured = {a[1]}; return a[0];);
    NAT("java/lang/Throwable", "toString", "()Ljava/lang/String;", {
        std::string n = a[0].a->cls->name;
        std::replace(n.begin(), n.end(), '/', '.');
        return R(vm.newString(a[0].a->primType != 0 ? n + ": " + a[0].a->str : n));
    });
    NAT("java/lang/Throwable", "fillInStackTrace", "()Ljava/lang/Throwable;", return a[0];);
    NAT("java/lang/Throwable", "addSuppressed", "(Ljava/lang/Throwable;)V", return V(););
    NAT("java/lang/Throwable", "getStackTrace", "()[Ljava/lang/StackTraceElement;", return R(vm.newArray('L', 0)););
    NAT("java/lang/Throwable", "printStackTrace", "()V", vm.note(vm.toJavaString(a[0].a)); return V(););
}

void installUtil(Vm& reg) {
    reg.defineClass("java/util/Collection", "java/lang/Object", {"java/lang/Iterable"}, true);
    reg.defineClass("java/util/List", "java/lang/Object", {"java/util/Collection"}, true);
    reg.defineClass("java/util/Set", "java/lang/Object", {"java/util/Collection"}, true);
    reg.defineClass("java/util/Map", "java/lang/Object", {}, true);
    reg.defineClass("java/util/Map$Entry", "java/lang/Object", {}, true);
    reg.defineClass("java/util/Iterator", "java/lang/Object", {}, true);
    reg.defineClass("java/util/ArrayList", "java/lang/Object", {"java/util/List"});
    reg.defineClass("java/util/LinkedList", "java/util/ArrayList");
    reg.defineClass("java/util/Vector", "java/util/ArrayList");
    reg.defineClass("java/util/HashSet", "java/lang/Object", {"java/util/Set"});
    reg.defineClass("java/util/LinkedHashSet", "java/util/HashSet");
    reg.defineClass("java/util/TreeSet", "java/util/HashSet");
    reg.defineClass("java/util/HashMap", "java/lang/Object", {"java/util/Map"});
    reg.defineClass("java/util/LinkedHashMap", "java/util/HashMap");
    reg.defineClass("java/util/TreeMap", "java/util/HashMap");
    reg.defineClass("java/util/concurrent/ConcurrentHashMap", "java/util/HashMap");
    reg.defineClass("java/util/Optional", "java/lang/Object");
    reg.defineClass("java/util/Objects", "java/lang/Object");
    reg.defineClass("java/util/Arrays", "java/lang/Object");
    reg.defineClass("java/util/Collections", "java/lang/Object");

    // ---- lists (and the sets, which share most of it) ----
    for (const char* cls : {"java/util/ArrayList", "java/util/HashSet"}) {
        const std::string c = cls;
        const bool set = c == "java/util/HashSet";
        reg.defineNative(c, "<init>", "()V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(I)V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(Ljava/util/Collection;)V", false, [set](Vm& v, Value* a) {
            for (const Value& e : contentsOf(v, a[1].a)) {
                if (!set || !containsElement(v, a[0].a, e.a)) a[0].a->elems.push_back(e);
            }
            return V();
        });
        reg.defineNative(c, "add", "(Ljava/lang/Object;)Z", false, [set](Vm& v, Value* a) {
            if (set && containsElement(v, a[0].a, a[1].a)) return Z(false);
            a[0].a->elems.push_back(a[1]);
            return Z(true);
        });
        reg.defineNative(c, "addAll", "(Ljava/util/Collection;)Z", false, [set](Vm& v, Value* a) {
            bool changed = false;
            for (const Value& e : contentsOf(v, a[1].a)) {
                if (set && containsElement(v, a[0].a, e.a)) continue;
                a[0].a->elems.push_back(e);
                changed = true;
            }
            return Z(changed);
        });
        reg.defineNative(c, "size", "()I", false, [](Vm&, Value* a) { return I(static_cast<int>(a[0].a->elems.size())); });
        reg.defineNative(c, "isEmpty", "()Z", false, [](Vm&, Value* a) { return Z(a[0].a->elems.empty()); });
        reg.defineNative(c, "clear", "()V", false, [](Vm&, Value* a) { a[0].a->elems.clear(); return V(); });
        reg.defineNative(c, "contains", "(Ljava/lang/Object;)Z", false, [](Vm& v, Value* a) { return Z(containsElement(v, a[0].a, a[1].a)); });
        reg.defineNative(c, "iterator", "()Ljava/util/Iterator;", false, [](Vm& v, Value* a) { return R(iteratorOf(v, a[0].a)); });
        reg.defineNative(c, "forEach", "(Ljava/util/function/Consumer;)V", false, [](Vm& v, Value* a) {
            for (const Value& e : std::vector<Value>(a[0].a->elems)) v.callInterface(a[1].a, "accept", "(Ljava/lang/Object;)V", &e, 1);
            return V();
        });
        reg.defineNative(c, "toArray", "()[Ljava/lang/Object;", false, [](Vm& v, Value* a) {
            Object* arr = v.newArray('L', static_cast<int>(a[0].a->elems.size()));
            arr->elems = a[0].a->elems;
            return R(arr);
        });
        reg.defineNative(c, "toArray", "([Ljava/lang/Object;)[Ljava/lang/Object;", false, [](Vm& v, Value* a) {
            Object* arr = v.newArray('L', static_cast<int>(a[0].a->elems.size()));
            arr->elems = a[0].a->elems;
            return R(arr);
        });
        reg.defineNative(c, "toString", "()Ljava/lang/String;", false, [](Vm& v, Value* a) {
            std::string out = "[";
            for (std::size_t i = 0; i < a[0].a->elems.size(); ++i) out += (i ? ", " : "") + v.toJavaString(a[0].a->elems[i].a);
            return R(v.newString(out + "]"));
        });
        reg.defineNative(c, "remove", "(Ljava/lang/Object;)Z", false, [](Vm& v, Value* a) {
            const int at = indexOfElement(v, a[0].a, a[1].a);
            if (at >= 0) a[0].a->elems.erase(a[0].a->elems.begin() + at);
            return Z(at >= 0);
        });
        reg.defineNative(c, "hashCode", "()I", false, [](Vm& v, Value* a) {
            std::int32_t h = 1;
            for (const Value& e : a[0].a->elems) h = 31 * h + v.hashJava(e.a);
            return I(h);
        });
    }
    reg.defineNative("java/util/ArrayList", "get", "(I)Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[1].i < 0 || static_cast<std::size_t>(a[1].i) >= a[0].a->elems.size()) v.throwNew("java/lang/IndexOutOfBoundsException", "Index " + std::to_string(a[1].i) + " out of bounds for length " + std::to_string(a[0].a->elems.size()));
        return a[0].a->elems[static_cast<std::size_t>(a[1].i)];
    });
    reg.defineNative("java/util/ArrayList", "set", "(ILjava/lang/Object;)Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[1].i < 0 || static_cast<std::size_t>(a[1].i) >= a[0].a->elems.size()) v.throwNew("java/lang/IndexOutOfBoundsException", "Index " + std::to_string(a[1].i));
        const Value old = a[0].a->elems[static_cast<std::size_t>(a[1].i)];
        a[0].a->elems[static_cast<std::size_t>(a[1].i)] = a[2];
        return old;
    });
    reg.defineNative("java/util/ArrayList", "add", "(ILjava/lang/Object;)V", false, [](Vm&, Value* a) {
        a[0].a->elems.insert(a[0].a->elems.begin() + std::min<std::size_t>(static_cast<std::size_t>(std::max(0, a[1].i)), a[0].a->elems.size()), a[2]);
        return V();
    });
    reg.defineNative("java/util/ArrayList", "remove", "(I)Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[1].i < 0 || static_cast<std::size_t>(a[1].i) >= a[0].a->elems.size()) v.throwNew("java/lang/IndexOutOfBoundsException", "Index " + std::to_string(a[1].i));
        const Value old = a[0].a->elems[static_cast<std::size_t>(a[1].i)];
        a[0].a->elems.erase(a[0].a->elems.begin() + a[1].i);
        return old;
    });
    reg.defineNative("java/util/ArrayList", "indexOf", "(Ljava/lang/Object;)I", false, [](Vm& v, Value* a) { return I(indexOfElement(v, a[0].a, a[1].a)); });
    reg.defineNative("java/util/ArrayList", "getFirst", "()Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[0].a->elems.empty()) v.throwNew("java/util/NoSuchElementException");
        return a[0].a->elems.front();
    });
    reg.defineNative("java/util/ArrayList", "getLast", "()Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[0].a->elems.empty()) v.throwNew("java/util/NoSuchElementException");
        return a[0].a->elems.back();
    });
    for (int n = 0; n <= 10; ++n) {  // List.of(...) and Set.of(...)
        std::string desc = "(";
        for (int i = 0; i < n; ++i) desc += "Ljava/lang/Object;";
        desc += ")Ljava/util/";
        reg.defineNative("java/util/List", "of", desc + "List;", true, [n](Vm& v, Value* a) {
            Object* l = newList(v);
            for (int i = 0; i < n; ++i) l->elems.push_back(a[i]);
            return R(l);
        });
        reg.defineNative("java/util/Set", "of", desc + "Set;", true, [n](Vm& v, Value* a) {
            Object* l = newList(v, "java/util/LinkedHashSet");
            for (int i = 0; i < n; ++i) {
                if (!containsElement(v, l, a[i].a)) l->elems.push_back(a[i]);
            }
            return R(l);
        });
    }
    reg.defineNative("java/util/List", "of", "([Ljava/lang/Object;)Ljava/util/List;", true, [](Vm& v, Value* a) {
        Object* l = newList(v);
        l->elems = a[0].a->elems;
        return R(l);
    });
    reg.defineNative("java/util/List", "copyOf", "(Ljava/util/Collection;)Ljava/util/List;", true, [](Vm& v, Value* a) {
        Object* l = newList(v);
        l->elems = contentsOf(v, a[0].a);
        return R(l);
    });
    reg.defineNative("java/util/Arrays", "asList", "([Ljava/lang/Object;)Ljava/util/List;", true, [](Vm& v, Value* a) {
        Object* l = newList(v);
        l->elems = a[0].a->elems;
        return R(l);
    });
    for (const char* name : {"emptyList", "emptyMap", "emptySet"}) {
        const std::string n = name;
        reg.defineNative("java/util/Collections", name, n == "emptyList" ? "()Ljava/util/List;" : n == "emptyMap" ? "()Ljava/util/Map;" : "()Ljava/util/Set;", true, [n](Vm& v, Value*) {
            return R(v.newObject(n == "emptyList" ? "java/util/ArrayList" : n == "emptyMap" ? "java/util/HashMap" : "java/util/HashSet"));
        });
    }
    reg.defineNative("java/util/Collections", "singletonList", "(Ljava/lang/Object;)Ljava/util/List;", true, [](Vm& v, Value* a) {
        Object* l = newList(v);
        l->elems.push_back(a[0]);
        return R(l);
    });
    for (const char* name : {"unmodifiableList", "unmodifiableSet", "unmodifiableMap", "unmodifiableCollection"}) {
        reg.defineNative("java/util/Collections", name, std::string("(Ljava/util/") + (name[12] == 'L' ? "List" : name[12] == 'S' ? "Set" : name[12] == 'M' ? "Map" : "Collection") + ";)Ljava/util/" + (name[12] == 'L' ? "List" : name[12] == 'S' ? "Set" : name[12] == 'M' ? "Map" : "Collection") + ";", true,
                        [](Vm&, Value* a) { return a[0]; });
    }

    // ---- iterators ----
    reg.defineNative("java/util/Iterator", "hasNext", "()Z", false, [](Vm&, Value* a) { return Z(static_cast<std::size_t>(a[0].a->position) < a[0].a->elems.size()); });
    reg.defineNative("java/util/Iterator", "next", "()Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (static_cast<std::size_t>(a[0].a->position) >= a[0].a->elems.size()) v.throwNew("java/util/NoSuchElementException");
        return a[0].a->elems[static_cast<std::size_t>(a[0].a->position++)];
    });

    // ---- maps ----
    for (const char* cls : {"java/util/HashMap"}) {
        const std::string c = cls;
        const auto find = [](Vm& v, Object* m, Object* key) -> int {
            if (m->map.size() <= 12) {
                for (std::size_t i = 0; i < m->map.size(); ++i) {
                    if (v.equalsJava(m->map[i].first.a, key)) return static_cast<int>(i);
                }
                return -1;
            }
            // a bigger map: by the hash of the key (the entries after the ones the index covers were only appended; removals make it start again)
            if (m->mapIndexStale || m->mapIndexed > m->map.size()) {
                m->mapIndex.clear();
                m->mapIndexed = 0;
                m->mapIndexStale = false;
            }
            for (; m->mapIndexed < m->map.size(); ++m->mapIndexed) m->mapIndex.emplace(v.hashJava(m->map[m->mapIndexed].first.a), static_cast<std::uint32_t>(m->mapIndexed));
            const auto range = m->mapIndex.equal_range(v.hashJava(key));
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second < m->map.size() && v.equalsJava(m->map[it->second].first.a, key)) return static_cast<int>(it->second);
            }
            return -1;
        };
        reg.defineNative(c, "<init>", "()V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(I)V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(IF)V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(IFZ)V", false, [](Vm&, Value*) { return V(); });
        reg.defineNative(c, "<init>", "(Ljava/util/Map;)V", false, [](Vm&, Value* a) {
            if (a[1].a != nullptr) a[0].a->map = a[1].a->map;  // (a copy of the entries)
            return V();
        });
        reg.defineNative(c, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            if (at >= 0) {
                const Value old = a[0].a->map[static_cast<std::size_t>(at)].second;
                a[0].a->map[static_cast<std::size_t>(at)].second = a[2];
                return old;
            }
            a[0].a->map.emplace_back(a[1], a[2]);
            return R(nullptr);
        });
        reg.defineNative(c, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            return at >= 0 ? a[0].a->map[static_cast<std::size_t>(at)].second : R(nullptr);
        });
        reg.defineNative(c, "getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            return at >= 0 ? a[0].a->map[static_cast<std::size_t>(at)].second : a[2];
        });
        reg.defineNative(c, "containsKey", "(Ljava/lang/Object;)Z", false, [find](Vm& v, Value* a) { return Z(find(v, a[0].a, a[1].a) >= 0); });
        reg.defineNative(c, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            if (at < 0) return R(nullptr);
            const Value old = a[0].a->map[static_cast<std::size_t>(at)].second;
            a[0].a->map.erase(a[0].a->map.begin() + at);
            a[0].a->mapIndexStale = true;
            return old;
        });
        reg.defineNative(c, "putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            if (at >= 0) return a[0].a->map[static_cast<std::size_t>(at)].second;
            a[0].a->map.emplace_back(a[1], a[2]);
            return R(nullptr);
        });
        reg.defineNative(c, "computeIfAbsent", "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;", false, [find](Vm& v, Value* a) {
            const int at = find(v, a[0].a, a[1].a);
            if (at >= 0) return a[0].a->map[static_cast<std::size_t>(at)].second;
            const Value made = v.callInterface(a[2].a, "apply", "(Ljava/lang/Object;)Ljava/lang/Object;", &a[1], 1);
            a[0].a->map.emplace_back(a[1], made);
            return made;
        });
        reg.defineNative(c, "size", "()I", false, [](Vm&, Value* a) { return I(static_cast<int>(a[0].a->map.size())); });
        reg.defineNative(c, "isEmpty", "()Z", false, [](Vm&, Value* a) { return Z(a[0].a->map.empty()); });
        reg.defineNative(c, "clear", "()V", false, [](Vm&, Value* a) { a[0].a->map.clear(); a[0].a->mapIndexStale = true; return V(); });
        reg.defineNative(c, "putAll", "(Ljava/util/Map;)V", false, [find](Vm& v, Value* a) {
            for (const auto& [k, val] : std::vector<std::pair<Value, Value>>(a[1].a->map)) {
                const int at = find(v, a[0].a, k.a);
                if (at >= 0) a[0].a->map[static_cast<std::size_t>(at)].second = val;
                else a[0].a->map.emplace_back(k, val);
            }
            return V();
        });
        reg.defineNative(c, "keySet", "()Ljava/util/Set;", false, [](Vm& v, Value* a) {
            Object* s = newList(v, "java/util/LinkedHashSet");
            for (const auto& [k, val] : a[0].a->map) s->elems.push_back(k);
            return R(s);
        });
        reg.defineNative(c, "values", "()Ljava/util/Collection;", false, [](Vm& v, Value* a) {
            Object* l = newList(v);
            for (const auto& [k, val] : a[0].a->map) l->elems.push_back(val);
            return R(l);
        });
        reg.defineNative(c, "entrySet", "()Ljava/util/Set;", false, [](Vm& v, Value* a) {
            Object* s = newList(v, "java/util/LinkedHashSet");
            for (const auto& [k, val] : a[0].a->map) {
                Object* e = v.newObject("java/util/Map$Entry");
                e->kind = Kind::Entry;
                e->elems = {k, val};
                s->elems.push_back(R(e));
            }
            return R(s);
        });
        reg.defineNative(c, "forEach", "(Ljava/util/function/BiConsumer;)V", false, [](Vm& v, Value* a) {
            for (const auto& [k, val] : std::vector<std::pair<Value, Value>>(a[0].a->map)) {
                const Value args[2] = {k, val};
                v.callInterface(a[1].a, "accept", "(Ljava/lang/Object;Ljava/lang/Object;)V", args, 2);
            }
            return V();
        });
    }
    reg.defineNative("java/util/Map$Entry", "getKey", "()Ljava/lang/Object;", false, [](Vm&, Value* a) { return a[0].a->elems[0]; });
    reg.defineNative("java/util/Map$Entry", "getValue", "()Ljava/lang/Object;", false, [](Vm&, Value* a) { return a[0].a->elems[1]; });
    for (int n = 0; n <= 5; ++n) {  // Map.of(k, v, ...)
        std::string desc = "(";
        for (int i = 0; i < n * 2; ++i) desc += "Ljava/lang/Object;";
        reg.defineNative("java/util/Map", "of", desc + ")Ljava/util/Map;", true, [n](Vm& v, Value* a) {
            Object* m = v.newObject("java/util/LinkedHashMap");
            for (int i = 0; i < n; ++i) m->map.emplace_back(a[2 * i], a[2 * i + 1]);
            return R(m);
        });
    }
    reg.defineNative("java/util/Map", "entry", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/util/Map$Entry;", true, [](Vm& v, Value* a) {
        Object* e = v.newObject("java/util/Map$Entry");
        e->kind = Kind::Entry;
        e->elems = {a[0], a[1]};
        return R(e);
    });

    // ---- Optional and Objects ----
    reg.defineNative("java/util/Optional", "of", "(Ljava/lang/Object;)Ljava/util/Optional;", true, [](Vm& v, Value* a) {
        if (a[0].a == nullptr) v.throwNew("java/lang/NullPointerException");
        Object* o = v.newObject("java/util/Optional");
        o->elems.push_back(a[0]);
        return R(o);
    });
    reg.defineNative("java/util/Optional", "ofNullable", "(Ljava/lang/Object;)Ljava/util/Optional;", true, [](Vm& v, Value* a) {
        Object* o = v.newObject("java/util/Optional");
        if (a[0].a != nullptr) o->elems.push_back(a[0]);
        return R(o);
    });
    reg.defineNative("java/util/Optional", "empty", "()Ljava/util/Optional;", true, [](Vm& v, Value*) { return R(v.newObject("java/util/Optional")); });
    reg.defineNative("java/util/Optional", "isPresent", "()Z", false, [](Vm&, Value* a) { return Z(!a[0].a->elems.empty()); });
    reg.defineNative("java/util/Optional", "isEmpty", "()Z", false, [](Vm&, Value* a) { return Z(a[0].a->elems.empty()); });
    reg.defineNative("java/util/Optional", "get", "()Ljava/lang/Object;", false, [](Vm& v, Value* a) {
        if (a[0].a->elems.empty()) v.throwNew("java/util/NoSuchElementException", "No value present");
        return a[0].a->elems[0];
    });
    reg.defineNative("java/util/Optional", "orElse", "(Ljava/lang/Object;)Ljava/lang/Object;", false, [](Vm&, Value* a) { return a[0].a->elems.empty() ? a[1] : a[0].a->elems[0]; });
    reg.defineNative("java/util/Optional", "ifPresent", "(Ljava/util/function/Consumer;)V", false, [](Vm& v, Value* a) {
        if (!a[0].a->elems.empty()) v.callInterface(a[1].a, "accept", "(Ljava/lang/Object;)V", &a[0].a->elems[0], 1);
        return V();
    });
    reg.defineNative("java/util/Objects", "requireNonNull", "(Ljava/lang/Object;)Ljava/lang/Object;", true, [](Vm& v, Value* a) {
        if (a[0].a == nullptr) v.throwNew("java/lang/NullPointerException");
        return a[0];
    });
    reg.defineNative("java/util/Objects", "requireNonNull", "(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/Object;", true, [](Vm& v, Value* a) {
        if (a[0].a == nullptr) v.throwNew("java/lang/NullPointerException", a[1].a != nullptr ? a[1].a->str : std::string());
        return a[0];
    });
    reg.defineNative("java/util/Objects", "equals", "(Ljava/lang/Object;Ljava/lang/Object;)Z", true, [](Vm& v, Value* a) { return Z(v.equalsJava(a[0].a, a[1].a)); });
    reg.defineNative("java/util/Objects", "hashCode", "(Ljava/lang/Object;)I", true, [](Vm& v, Value* a) { return I(v.hashJava(a[0].a)); });
    reg.defineNative("java/util/Objects", "hash", "([Ljava/lang/Object;)I", true, [](Vm& v, Value* a) {
        std::int32_t h = 1;
        for (const Value& e : a[0].a->elems) h = 31 * h + v.hashJava(e.a);
        return I(h);
    });
    reg.defineNative("java/util/Objects", "toString", "(Ljava/lang/Object;)Ljava/lang/String;", true, [](Vm& v, Value* a) { return R(v.newString(v.toJavaString(a[0].a))); });
    reg.defineNative("java/util/Objects", "isNull", "(Ljava/lang/Object;)Z", true, [](Vm&, Value* a) { return Z(a[0].a == nullptr); });
    reg.defineNative("java/util/Objects", "nonNull", "(Ljava/lang/Object;)Z", true, [](Vm&, Value* a) { return Z(a[0].a != nullptr); });
    reg.defineNative("java/lang/Iterable", "forEach", "(Ljava/util/function/Consumer;)V", false, [](Vm& v, Value* a) {
        for (const Value& e : contentsOf(v, a[0].a)) v.callInterface(a[1].a, "accept", "(Ljava/lang/Object;)V", &e, 1);
        return V();
    });
}

}  // namespace

void installNatives(Vm& vm) {
    installLang(vm);
    installUtil(vm);
}

}  // namespace falcon::jvm
