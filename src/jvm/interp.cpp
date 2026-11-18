#include "falcon/jvm/vm.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace falcon::jvm {
namespace {

inline std::int32_t d2i(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 2147483647.0) return INT_MAX;
    if (d <= -2147483648.0) return INT_MIN;
    return static_cast<std::int32_t>(d);
}
inline std::int64_t d2l(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9223372036854775807.0) return LLONG_MAX;
    if (d <= -9223372036854775808.0) return LLONG_MIN;
    return static_cast<std::int64_t>(d);
}
inline std::int16_t rd16(const std::uint8_t* p) { return static_cast<std::int16_t>((p[0] << 8) | p[1]); }
inline std::uint16_t rdu16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
inline std::int32_t rd32(const std::uint8_t* p) { return static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
inline bool wide(char kind) { return kind == 'J' || kind == 'D'; }

}  // namespace

// The interpreter: one method's bytecode, with its locals and stack on the VM's own slab. A Java exception is a C++ exception that the loop catches to look for a handler of this method; what it
// does not handle goes up to the caller.
Value Vm::run(Method* m, Value* locals) {
    const Code& code = *m->code;
    Class* cls = m->owner;
    const ClassFile& cf = *cls->file;
    const std::uint8_t* const bc = code.bytes.data();
    Value* const stack = locals + code.maxLocals;
    Value* sp = stack;
    int pc = 0;

    const auto at = [&](Object* a, int index) -> Value& {
        if (a == nullptr) throwNew("java/lang/NullPointerException", "Cannot load from or store to a null array");
        if (index < 0 || static_cast<std::size_t>(index) >= a->elems.size()) throwNew("java/lang/ArrayIndexOutOfBoundsException", "Index " + std::to_string(index) + " out of bounds for length " + std::to_string(a->elems.size()));
        return a->elems[static_cast<std::size_t>(index)];
    };
    const auto classAt = [&](int index) -> Class* {
        void*& slot = cls->cache[static_cast<std::size_t>(index)];
        if (slot == nullptr) slot = loadClass(cf.className(static_cast<std::uint16_t>(index)));
        return static_cast<Class*>(slot);
    };
    const auto pushConstant = [&](int index) {
        const CpEntry& e = cf.cp[static_cast<std::size_t>(index)];
        switch (e.tag) {
            case kInteger: *sp++ = valueOfInt(e.i); break;
            case kFloat: *sp++ = valueOfFloat(e.f); break;
            case kLong: *sp = valueOfLong(e.j); sp += 2; break;
            case kDouble: *sp = valueOfDouble(e.d); sp += 2; break;
            case kString: {
                void*& slot = cls->cache[static_cast<std::size_t>(index)];
                if (slot == nullptr) slot = intern(cf.utf8(e.a));
                *sp++ = valueOfRef(static_cast<Object*>(slot));
                break;
            }
            case kClass: *sp++ = valueOfRef(classObject(cf.utf8(e.a))); break;  // (not cached by the pool's index: that slot holds the Class a new or a cast uses)
            default: throwNew("java/lang/UnsupportedOperationException", "ldc of a constant of kind " + std::to_string(e.tag));
        }
    };
    const std::function<Object*(const std::string&, const int*, int, int)> multi = [&](const std::string& desc, const int* counts, int dims, int level) -> Object* {
        // desc: "[[I": the array type at this level
        const std::string inner = desc.substr(1);
        const char elem = inner[0] == '[' ? 'L' : (inner[0] == 'L' ? 'L' : inner[0]);
        Object* arr = newArray(elem, counts[level]);
        if (level + 1 < dims && inner[0] == '[') {
            for (Value& v : arr->elems) v = valueOfRef(multi(inner, counts, dims, level + 1));
        }
        return arr;
    };

    for (;;) {
        try {
            for (;;) {
                if (++instructions_ > budget) throw BudgetExceeded{"the mod ran out of instructions in " + stackTrace(depth_)};
                const std::uint8_t op = bc[pc];
                switch (op) {
                    case 0x00: pc += 1; break;
                    case 0x01: *sp++ = valueOfRef(nullptr); pc += 1; break;
                    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: *sp++ = valueOfInt(op - 3); pc += 1; break;
                    case 0x09: case 0x0A: *sp = valueOfLong(op - 9); sp += 2; pc += 1; break;
                    case 0x0B: case 0x0C: case 0x0D: *sp++ = valueOfFloat(static_cast<float>(op - 0x0B)); pc += 1; break;
                    case 0x0E: case 0x0F: *sp = valueOfDouble(op - 0x0E); sp += 2; pc += 1; break;
                    case 0x10: *sp++ = valueOfInt(static_cast<std::int8_t>(bc[pc + 1])); pc += 2; break;
                    case 0x11: *sp++ = valueOfInt(rd16(bc + pc + 1)); pc += 3; break;
                    case 0x12: pushConstant(bc[pc + 1]); pc += 2; break;
                    case 0x13: case 0x14: pushConstant(rdu16(bc + pc + 1)); pc += 3; break;
                    // loads
                    case 0x15: case 0x17: case 0x19: *sp++ = locals[bc[pc + 1]]; pc += 2; break;
                    case 0x16: case 0x18: *sp = locals[bc[pc + 1]]; sp += 2; pc += 2; break;
                    case 0x1A: case 0x1B: case 0x1C: case 0x1D: *sp++ = locals[op - 0x1A]; pc += 1; break;
                    case 0x1E: case 0x1F: case 0x20: case 0x21: *sp = locals[op - 0x1E]; sp += 2; pc += 1; break;
                    case 0x22: case 0x23: case 0x24: case 0x25: *sp++ = locals[op - 0x22]; pc += 1; break;
                    case 0x26: case 0x27: case 0x28: case 0x29: *sp = locals[op - 0x26]; sp += 2; pc += 1; break;
                    case 0x2A: case 0x2B: case 0x2C: case 0x2D: *sp++ = locals[op - 0x2A]; pc += 1; break;
                    // array loads
                    case 0x2E: case 0x30: case 0x32: case 0x33: case 0x34: case 0x35: { const Value v = at(sp[-2].a, sp[-1].i); sp[-2] = v; --sp; pc += 1; break; }
                    case 0x2F: case 0x31: { const Value v = at(sp[-2].a, sp[-1].i); sp[-2] = v; pc += 1; break; }
                    // stores
                    case 0x36: case 0x38: case 0x3A: locals[bc[pc + 1]] = *--sp; pc += 2; break;
                    case 0x37: case 0x39: sp -= 2; locals[bc[pc + 1]] = sp[0]; pc += 2; break;
                    case 0x3B: case 0x3C: case 0x3D: case 0x3E: locals[op - 0x3B] = *--sp; pc += 1; break;
                    case 0x3F: case 0x40: case 0x41: case 0x42: sp -= 2; locals[op - 0x3F] = sp[0]; pc += 1; break;
                    case 0x43: case 0x44: case 0x45: case 0x46: locals[op - 0x43] = *--sp; pc += 1; break;
                    case 0x47: case 0x48: case 0x49: case 0x4A: sp -= 2; locals[op - 0x47] = sp[0]; pc += 1; break;
                    case 0x4B: case 0x4C: case 0x4D: case 0x4E: locals[op - 0x4B] = *--sp; pc += 1; break;
                    // array stores
                    case 0x4F: case 0x51: case 0x53: { at(sp[-3].a, sp[-2].i) = sp[-1]; sp -= 3; pc += 1; break; }
                    case 0x50: case 0x52: { at(sp[-4].a, sp[-3].i) = sp[-2]; sp -= 4; pc += 1; break; }
                    case 0x54: {
                        Object* a = sp[-3].a;
                        at(a, sp[-2].i) = valueOfInt(a->elemType == 'Z' ? (sp[-1].i & 1) : static_cast<std::int8_t>(sp[-1].i));
                        sp -= 3;
                        pc += 1;
                        break;
                    }
                    case 0x55: at(sp[-3].a, sp[-2].i) = valueOfInt(static_cast<std::uint16_t>(sp[-1].i)); sp -= 3; pc += 1; break;
                    case 0x56: at(sp[-3].a, sp[-2].i) = valueOfInt(static_cast<std::int16_t>(sp[-1].i)); sp -= 3; pc += 1; break;
                    // the stack
                    case 0x57: --sp; pc += 1; break;
                    case 0x58: sp -= 2; pc += 1; break;
                    case 0x59: sp[0] = sp[-1]; ++sp; pc += 1; break;
                    case 0x5A: sp[0] = sp[-1]; sp[-1] = sp[-2]; sp[-2] = sp[0]; ++sp; pc += 1; break;
                    case 0x5B: sp[0] = sp[-1]; sp[-1] = sp[-2]; sp[-2] = sp[-3]; sp[-3] = sp[0]; ++sp; pc += 1; break;
                    case 0x5C: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; pc += 1; break;
                    case 0x5D: sp[1] = sp[-1]; sp[0] = sp[-2]; sp[-1] = sp[-3]; sp[-2] = sp[1]; sp[-3] = sp[0]; sp += 2; pc += 1; break;
                    case 0x5E: sp[1] = sp[-1]; sp[0] = sp[-2]; sp[-1] = sp[-3]; sp[-2] = sp[-4]; sp[-3] = sp[1]; sp[-4] = sp[0]; sp += 2; pc += 1; break;
                    case 0x5F: std::swap(sp[-1], sp[-2]); pc += 1; break;
                    // arithmetic
                    case 0x60: sp[-2].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(sp[-2].i) + static_cast<std::uint32_t>(sp[-1].i)); --sp; pc += 1; break;
                    case 0x61: sp[-4].j = static_cast<std::int64_t>(static_cast<std::uint64_t>(sp[-4].j) + static_cast<std::uint64_t>(sp[-2].j)); sp -= 2; pc += 1; break;
                    case 0x62: sp[-2].f += sp[-1].f; --sp; pc += 1; break;
                    case 0x63: sp[-4].d += sp[-2].d; sp -= 2; pc += 1; break;
                    case 0x64: sp[-2].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(sp[-2].i) - static_cast<std::uint32_t>(sp[-1].i)); --sp; pc += 1; break;
                    case 0x65: sp[-4].j = static_cast<std::int64_t>(static_cast<std::uint64_t>(sp[-4].j) - static_cast<std::uint64_t>(sp[-2].j)); sp -= 2; pc += 1; break;
                    case 0x66: sp[-2].f -= sp[-1].f; --sp; pc += 1; break;
                    case 0x67: sp[-4].d -= sp[-2].d; sp -= 2; pc += 1; break;
                    case 0x68: sp[-2].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(sp[-2].i) * static_cast<std::uint32_t>(sp[-1].i)); --sp; pc += 1; break;
                    case 0x69: sp[-4].j = static_cast<std::int64_t>(static_cast<std::uint64_t>(sp[-4].j) * static_cast<std::uint64_t>(sp[-2].j)); sp -= 2; pc += 1; break;
                    case 0x6A: sp[-2].f *= sp[-1].f; --sp; pc += 1; break;
                    case 0x6B: sp[-4].d *= sp[-2].d; sp -= 2; pc += 1; break;
                    case 0x6C: {
                        const std::int32_t b = sp[-1].i;
                        if (b == 0) throwNew("java/lang/ArithmeticException", "/ by zero");
                        sp[-2].i = b == -1 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(sp[-2].i)) : sp[-2].i / b;
                        --sp;
                        pc += 1;
                        break;
                    }
                    case 0x6D: {
                        const std::int64_t b = sp[-2].j;
                        if (b == 0) throwNew("java/lang/ArithmeticException", "/ by zero");
                        sp[-4].j = b == -1 ? static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(sp[-4].j)) : sp[-4].j / b;
                        sp -= 2;
                        pc += 1;
                        break;
                    }
                    case 0x6E: sp[-2].f /= sp[-1].f; --sp; pc += 1; break;
                    case 0x6F: sp[-4].d /= sp[-2].d; sp -= 2; pc += 1; break;
                    case 0x70: {
                        const std::int32_t b = sp[-1].i;
                        if (b == 0) throwNew("java/lang/ArithmeticException", "/ by zero");
                        sp[-2].i = b == -1 ? 0 : sp[-2].i % b;
                        --sp;
                        pc += 1;
                        break;
                    }
                    case 0x71: {
                        const std::int64_t b = sp[-2].j;
                        if (b == 0) throwNew("java/lang/ArithmeticException", "/ by zero");
                        sp[-4].j = b == -1 ? 0 : sp[-4].j % b;
                        sp -= 2;
                        pc += 1;
                        break;
                    }
                    case 0x72: sp[-2].f = std::fmod(sp[-2].f, sp[-1].f); --sp; pc += 1; break;
                    case 0x73: sp[-4].d = std::fmod(sp[-4].d, sp[-2].d); sp -= 2; pc += 1; break;
                    case 0x74: sp[-1].i = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(sp[-1].i)); pc += 1; break;
                    case 0x75: sp[-2].j = static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(sp[-2].j)); pc += 1; break;
                    case 0x76: sp[-1].f = -sp[-1].f; pc += 1; break;
                    case 0x77: sp[-2].d = -sp[-2].d; pc += 1; break;
                    case 0x78: sp[-2].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(sp[-2].i) << (sp[-1].i & 31)); --sp; pc += 1; break;
                    case 0x79: sp[-3].j = static_cast<std::int64_t>(static_cast<std::uint64_t>(sp[-3].j) << (sp[-1].i & 63)); --sp; pc += 1; break;
                    case 0x7A: sp[-2].i >>= (sp[-1].i & 31); --sp; pc += 1; break;
                    case 0x7B: sp[-3].j >>= (sp[-1].i & 63); --sp; pc += 1; break;
                    case 0x7C: sp[-2].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(sp[-2].i) >> (sp[-1].i & 31)); --sp; pc += 1; break;
                    case 0x7D: sp[-3].j = static_cast<std::int64_t>(static_cast<std::uint64_t>(sp[-3].j) >> (sp[-1].i & 63)); --sp; pc += 1; break;
                    case 0x7E: sp[-2].i &= sp[-1].i; --sp; pc += 1; break;
                    case 0x7F: sp[-4].j &= sp[-2].j; sp -= 2; pc += 1; break;
                    case 0x80: sp[-2].i |= sp[-1].i; --sp; pc += 1; break;
                    case 0x81: sp[-4].j |= sp[-2].j; sp -= 2; pc += 1; break;
                    case 0x82: sp[-2].i ^= sp[-1].i; --sp; pc += 1; break;
                    case 0x83: sp[-4].j ^= sp[-2].j; sp -= 2; pc += 1; break;
                    case 0x84: locals[bc[pc + 1]].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(locals[bc[pc + 1]].i) + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(bc[pc + 2])))); pc += 3; break;
                    // conversions
                    case 0x85: { const std::int64_t v = sp[-1].i; sp[-1].j = v; ++sp; pc += 1; break; }
                    case 0x86: sp[-1].f = static_cast<float>(sp[-1].i); pc += 1; break;
                    case 0x87: { const double v = sp[-1].i; sp[-1].d = v; ++sp; pc += 1; break; }
                    case 0x88: sp[-2].i = static_cast<std::int32_t>(sp[-2].j); --sp; pc += 1; break;
                    case 0x89: sp[-2].f = static_cast<float>(sp[-2].j); --sp; pc += 1; break;
                    case 0x8A: sp[-2].d = static_cast<double>(sp[-2].j); pc += 1; break;
                    case 0x8B: sp[-1].i = d2i(static_cast<double>(sp[-1].f)); pc += 1; break;
                    case 0x8C: { const std::int64_t v = d2l(static_cast<double>(sp[-1].f)); sp[-1].j = v; ++sp; pc += 1; break; }
                    case 0x8D: { const double v = static_cast<double>(sp[-1].f); sp[-1].d = v; ++sp; pc += 1; break; }
                    case 0x8E: sp[-2].i = d2i(sp[-2].d); --sp; pc += 1; break;
                    case 0x8F: sp[-2].j = d2l(sp[-2].d); pc += 1; break;
                    case 0x90: sp[-2].f = static_cast<float>(sp[-2].d); --sp; pc += 1; break;
                    case 0x91: sp[-1].i = static_cast<std::int8_t>(sp[-1].i); pc += 1; break;
                    case 0x92: sp[-1].i = static_cast<std::uint16_t>(sp[-1].i); pc += 1; break;
                    case 0x93: sp[-1].i = static_cast<std::int16_t>(sp[-1].i); pc += 1; break;
                    // comparisons
                    case 0x94: { const std::int64_t a = sp[-4].j, b = sp[-2].j; sp -= 4; *sp++ = valueOfInt(a < b ? -1 : a > b ? 1 : 0); pc += 1; break; }
                    case 0x95: case 0x96: {
                        const float a = sp[-2].f, b = sp[-1].f;
                        sp -= 2;
                        *sp++ = valueOfInt(std::isnan(a) || std::isnan(b) ? (op == 0x95 ? -1 : 1) : a < b ? -1 : a > b ? 1 : 0);
                        pc += 1;
                        break;
                    }
                    case 0x97: case 0x98: {
                        const double a = sp[-4].d, b = sp[-2].d;
                        sp -= 4;
                        *sp++ = valueOfInt(std::isnan(a) || std::isnan(b) ? (op == 0x97 ? -1 : 1) : a < b ? -1 : a > b ? 1 : 0);
                        pc += 1;
                        break;
                    }
                    case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: {
                        const std::int32_t v = (--sp)->i;
                        const bool jump = op == 0x99 ? v == 0 : op == 0x9A ? v != 0 : op == 0x9B ? v < 0 : op == 0x9C ? v >= 0 : op == 0x9D ? v > 0 : v <= 0;
                        pc += jump ? rd16(bc + pc + 1) : 3;
                        break;
                    }
                    case 0x9F: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: {
                        const std::int32_t b = sp[-1].i, a = sp[-2].i;
                        sp -= 2;
                        const bool jump = op == 0x9F ? a == b : op == 0xA0 ? a != b : op == 0xA1 ? a < b : op == 0xA2 ? a >= b : op == 0xA3 ? a > b : a <= b;
                        pc += jump ? rd16(bc + pc + 1) : 3;
                        break;
                    }
                    case 0xA5: case 0xA6: {
                        const bool same = sp[-2].a == sp[-1].a;
                        sp -= 2;
                        pc += (op == 0xA5) == same ? rd16(bc + pc + 1) : 3;
                        break;
                    }
                    case 0xA7: pc += rd16(bc + pc + 1); break;
                    case 0xC8: pc += rd32(bc + pc + 1); break;
                    case 0xAA: {  // tableswitch
                        const int p = (pc + 4) & ~3;
                        const std::int32_t low = rd32(bc + p + 4), high = rd32(bc + p + 8), index = (--sp)->i;
                        pc += index < low || index > high ? rd32(bc + p) : rd32(bc + p + 12 + 4 * (index - low));
                        break;
                    }
                    case 0xAB: {  // lookupswitch
                        const int p = (pc + 4) & ~3;
                        const std::int32_t pairs = rd32(bc + p + 4), key = (--sp)->i;
                        std::int32_t offset = rd32(bc + p);
                        for (std::int32_t i = 0; i < pairs; ++i) {
                            if (rd32(bc + p + 8 + 8 * i) == key) {
                                offset = rd32(bc + p + 12 + 8 * i);
                                break;
                            }
                        }
                        pc += offset;
                        break;
                    }
                    // returns
                    case 0xAC: case 0xAE: case 0xB0: return sp[-1];
                    case 0xAD: case 0xAF: return sp[-2];
                    case 0xB1: return Value();
                    // fields
                    case 0xB2: {  // getstatic
                        FieldRef& f = fieldRef(cls, rdu16(bc + pc + 1));
                        Value v;
                        if (f.info != nullptr && f.declaring->file != nullptr) {
                            if (f.declaring->state != Class::State::Ready) ensureInitialized(f.declaring);
                            v = f.declaring->statics[static_cast<std::size_t>(f.info->index)];
                        } else {
                            v = shimStatic(f.cls, f);
                        }
                        *sp = v;
                        sp += wide(f.kind) ? 2 : 1;
                        pc += 3;
                        break;
                    }
                    case 0xB3: {  // putstatic
                        FieldRef& f = fieldRef(cls, rdu16(bc + pc + 1));
                        sp -= wide(f.kind) ? 2 : 1;
                        if (f.info != nullptr && f.declaring->file != nullptr) {
                            if (f.declaring->state != Class::State::Ready) ensureInitialized(f.declaring);
                            f.declaring->statics[static_cast<std::size_t>(f.info->index)] = sp[0];
                        }
                        pc += 3;
                        break;
                    }
                    case 0xB4: {  // getfield
                        FieldRef& f = fieldRef(cls, rdu16(bc + pc + 1));
                        Object* o = sp[-1].a;
                        if (o == nullptr) throwNew("java/lang/NullPointerException", "Cannot read field \"" + f.name + "\" because value is null");
                        Value v;
                        if (f.info != nullptr && static_cast<std::size_t>(f.info->index) < o->fields.size()) v = o->fields[static_cast<std::size_t>(f.info->index)];
                        else if (o->shim || o->cls->shim) v = shimField(o, f.name, f.desc);  // (a field of a game class: a stand-in)
                        sp[-1] = v;
                        if (wide(f.kind)) ++sp;
                        pc += 3;
                        break;
                    }
                    case 0xB5: {  // putfield
                        FieldRef& f = fieldRef(cls, rdu16(bc + pc + 1));
                        const int width = wide(f.kind) ? 2 : 1;
                        Object* o = sp[-width - 1].a;
                        if (o == nullptr) throwNew("java/lang/NullPointerException", "Cannot assign field \"" + f.name + "\" because value is null");
                        if (f.info != nullptr && static_cast<std::size_t>(f.info->index) < o->fields.size()) o->fields[static_cast<std::size_t>(f.info->index)] = sp[-width];
                        else if ((o->shim || o->cls->shim) && f.kind == 'A') shimOf(o).fields[f.name] = sp[-width].a;
                        sp -= width + 1;
                        pc += 3;
                        break;
                    }
                    // calls
                    case 0xB6: case 0xB7: case 0xB8: case 0xB9: {
                        MethodRef& r = methodRef(cls, rdu16(bc + pc + 1));
                        const int n = r.argSlots + (op == 0xB8 ? 0 : 1);
                        Value* args = sp - n;
                        const Value result = dispatch(r, op, args);
                        sp = args;
                        if (r.ret != 'V') {
                            *sp = result;
                            sp += wide(r.ret) ? 2 : 1;
                        }
                        pc += op == 0xB9 ? 5 : 3;
                        break;
                    }
                    case 0xBA: {
                        int slots = 0;
                        char ret = 'A';
                        const Value result = invokeDynamic(cls, rdu16(bc + pc + 1), sp, slots, ret);
                        sp -= slots;
                        if (ret != 'V') {
                            *sp = result;
                            sp += wide(ret) ? 2 : 1;
                        }
                        pc += 5;
                        break;
                    }
                    // objects and arrays
                    case 0xBB: {
                        Class* c = classAt(rdu16(bc + pc + 1));
                        if (c->state != Class::State::Ready) ensureInitialized(c);
                        Object* o = newObject(c);
                        if (c->shim) (void)shimOf(o);
                        *sp++ = valueOfRef(o);
                        pc += 3;
                        break;
                    }
                    case 0xBC: {
                        static const char kTypes[] = {0, 0, 0, 0, 'Z', 'C', 'F', 'D', 'B', 'S', 'I', 'J'};
                        sp[-1] = valueOfRef(newArray(kTypes[bc[pc + 1] & 15], sp[-1].i));
                        pc += 2;
                        break;
                    }
                    case 0xBD: sp[-1] = valueOfRef(newArray('L', sp[-1].i)); pc += 3; break;
                    case 0xBE: {
                        Object* a = sp[-1].a;
                        if (a == nullptr) throwNew("java/lang/NullPointerException", "Cannot read the array length because value is null");
                        sp[-1] = valueOfInt(static_cast<std::int32_t>(a->elems.size()));
                        pc += 1;
                        break;
                    }
                    case 0xBF: {
                        Object* e = sp[-1].a;
                        if (e == nullptr) throwNew("java/lang/NullPointerException");
                        throwDepth_ = depth_;
                        throw JavaThrow{e};
                    }
                    case 0xC0: {
                        Object* o = sp[-1].a;
                        Class* c = classAt(rdu16(bc + pc + 1));
                        if (o != nullptr && !instanceOf(o, c)) throwNew("java/lang/ClassCastException", "class " + o->cls->name + " cannot be cast to class " + c->name);
                        pc += 3;
                        break;
                    }
                    case 0xC1: {
                        Object* o = sp[-1].a;
                        sp[-1] = valueOfInt(isInstance(o, classAt(rdu16(bc + pc + 1))) ? 1 : 0);
                        pc += 3;
                        break;
                    }
                    case 0xC2: case 0xC3: --sp; pc += 1; break;
                    case 0xC4: {  // wide
                        const std::uint8_t inner = bc[pc + 1];
                        const int index = rdu16(bc + pc + 2);
                        switch (inner) {
                            case 0x15: case 0x17: case 0x19: *sp++ = locals[index]; pc += 4; break;
                            case 0x16: case 0x18: *sp = locals[index]; sp += 2; pc += 4; break;
                            case 0x36: case 0x38: case 0x3A: locals[index] = *--sp; pc += 4; break;
                            case 0x37: case 0x39: sp -= 2; locals[index] = sp[0]; pc += 4; break;
                            case 0x84: locals[index].i = static_cast<std::int32_t>(static_cast<std::uint32_t>(locals[index].i) + static_cast<std::uint32_t>(static_cast<std::int32_t>(rd16(bc + pc + 4)))); pc += 6; break;
                            default: throwNew("java/lang/VerifyError", "wide " + std::to_string(inner));
                        }
                        break;
                    }
                    case 0xC5: {  // multianewarray
                        Class* c = classAt(rdu16(bc + pc + 1));
                        const int dims = bc[pc + 3];
                        int counts[8] = {};
                        for (int i = 0; i < dims && i < 8; ++i) counts[i] = sp[-dims + i].i;
                        sp -= dims;
                        *sp++ = valueOfRef(multi(c->name, counts, dims, 0));
                        pc += 4;
                        break;
                    }
                    case 0xC6: pc += (--sp)->a == nullptr ? rd16(bc + pc + 1) : 3; break;
                    case 0xC7: pc += (--sp)->a != nullptr ? rd16(bc + pc + 1) : 3; break;
                    default: throwNew("java/lang/VerifyError", "an opcode this machine does not know: " + std::to_string(op));
                }
            }
        } catch (JavaThrow& thrown) {
            const Handler* found = nullptr;
            for (const Handler& h : code.handlers) {
                if (pc < h.start || pc >= h.end) continue;
                if (h.catchType == 0 || instanceOf(thrown.exception, loadClass(cf.className(h.catchType)))) {
                    found = &h;
                    break;
                }
            }
            if (found == nullptr) throw;
            sp = stack;
            *sp++ = valueOfRef(thrown.exception);
            pc = found->handler;
        }
    }
}

}  // namespace falcon::jvm
