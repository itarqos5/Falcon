#include "spirv_reflect.hpp"

#include <algorithm>
#include <map>

namespace falcon::vulkan {

namespace {

constexpr std::uint32_t kMagic = 0x07230203u;

enum Op : std::uint32_t {
    OpName = 5,
    OpMemberName = 6,
    OpTypeBool = 20,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeMatrix = 24,
    OpTypeImage = 25,
    OpTypeSampledImage = 27,
    OpTypeArray = 28,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpVariable = 59,
    OpDecorate = 71,
    OpMemberDecorate = 72,
};
enum Decoration : std::uint32_t { DecBuiltIn = 11, DecLocation = 30, DecMatrixStride = 7, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35 };
enum StorageClass : std::uint32_t { ScUniformConstant = 0, ScInput = 1, ScUniform = 2 };

struct Type {
    std::uint32_t op = 0;
    std::uint32_t a = 0;  // scalar: width; vector: component type; matrix: column type; pointer: pointee; sampled image: image; struct: -
    std::uint32_t b = 0;  // vector/matrix: count; image: dim; pointer: storage class
    std::uint32_t c = 0;  // image: arrayed
    std::vector<std::uint32_t> members;  // struct
};

std::string readString(const std::uint32_t* words, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        for (int byte = 0; byte < 4; ++byte) {
            const char ch = static_cast<char>((words[i] >> (8 * byte)) & 0xFF);
            if (ch == '\0') return out;
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

bool reflectSpirv(const std::uint32_t* words, std::size_t count, SpirvInfo& out, std::string& error) {
    if (count < 5 || words[0] != kMagic) {
        error = "not a SPIR-V module";
        return false;
    }
    std::map<std::uint32_t, std::string> names;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> memberNames;
    std::map<std::uint32_t, std::uint32_t> locations, bindings, sets;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> memberOffsets, memberStrides;
    std::map<std::uint32_t, bool> builtIns;
    std::map<std::uint32_t, Type> types;
    struct Variable {
        std::uint32_t id, type, storage;
    };
    std::vector<Variable> variables;

    for (std::size_t at = 5; at < count;) {
        const std::uint32_t head = words[at];
        const std::uint32_t op = head & 0xFFFFu;
        const std::size_t length = head >> 16;
        if (length == 0 || at + length > count) {
            error = "a broken SPIR-V instruction";
            return false;
        }
        const std::uint32_t* w = words + at;
        switch (op) {
            case OpName: names[w[1]] = readString(w + 2, length - 2); break;
            case OpMemberName: memberNames[{w[1], w[2]}] = readString(w + 3, length - 3); break;
            case OpDecorate:
                if (w[2] == DecLocation) locations[w[1]] = w[3];
                else if (w[2] == DecBinding) bindings[w[1]] = w[3];
                else if (w[2] == DecDescriptorSet) sets[w[1]] = w[3];
                else if (w[2] == DecBuiltIn) builtIns[w[1]] = true;
                break;
            case OpMemberDecorate:
                if (w[3] == DecOffset) memberOffsets[{w[1], w[2]}] = w[4];
                else if (w[3] == DecMatrixStride) memberStrides[{w[1], w[2]}] = w[4];
                break;
            case OpTypeBool: types[w[1]] = Type{op, 4, 0, 0, {}}; break;
            case OpTypeInt:
            case OpTypeFloat: types[w[1]] = Type{op, w[2] / 8, 0, 0, {}}; break;
            case OpTypeVector: types[w[1]] = Type{op, w[2], w[3], 0, {}}; break;
            case OpTypeMatrix: types[w[1]] = Type{op, w[2], w[3], 0, {}}; break;
            case OpTypeImage: types[w[1]] = Type{op, w[2], w[3], w[5], {}}; break;  // sampled type, dim, arrayed
            case OpTypeSampledImage: types[w[1]] = Type{op, w[2], 0, 0, {}}; break;
            case OpTypeArray: types[w[1]] = Type{op, w[2], 0, 0, {}}; break;
            case OpTypeStruct: {
                Type t{op, 0, 0, 0, {}};
                for (std::size_t i = 2; i < length; ++i) t.members.push_back(w[i]);
                types[w[1]] = std::move(t);
                break;
            }
            case OpTypePointer: types[w[1]] = Type{op, w[3], w[2], 0, {}}; break;
            case OpVariable: variables.push_back({w[2], w[1], w[3]}); break;
            default: break;
        }
        at += length;
    }

    // The size a member takes in the block: the scalars and vectors by their parts, a matrix by its column stride.
    const auto sizeOf = [&](std::uint32_t typeId, std::uint32_t stride, bool& ok) -> std::uint32_t {
        const auto it = types.find(typeId);
        if (it == types.end()) {
            ok = false;
            return 0;
        }
        const Type& t = it->second;
        if (t.op == OpTypeFloat || t.op == OpTypeInt || t.op == OpTypeBool) return t.a;
        if (t.op == OpTypeVector) {
            const auto scalar = types.find(t.a);
            if (scalar == types.end()) {
                ok = false;
                return 0;
            }
            return scalar->second.a * t.b;
        }
        if (t.op == OpTypeMatrix) return stride * t.b;
        ok = false;
        return 0;
    };

    bool found = false;
    for (const Variable& v : variables) {
        const auto pointer = types.find(v.type);
        if (pointer == types.end() || pointer->second.op != OpTypePointer) continue;
        const std::uint32_t pointee = pointer->second.a;
        const auto pointeeType = types.find(pointee);
        if (pointeeType == types.end()) continue;
        if (v.storage == ScUniform && sets[v.id] == 0 && bindings[v.id] == 0 && pointeeType->second.op == OpTypeStruct) {
            for (std::size_t m = 0; m < pointeeType->second.members.size(); ++m) {
                SpirvInfo::Member member;
                const auto name = memberNames.find({pointee, static_cast<std::uint32_t>(m)});
                member.name = name != memberNames.end() ? name->second : std::string();
                member.offset = memberOffsets[{pointee, static_cast<std::uint32_t>(m)}];
                bool ok = true;
                member.size = sizeOf(pointeeType->second.members[m], memberStrides[{pointee, static_cast<std::uint32_t>(m)}], ok);
                if (!ok) {
                    error = "the uniform block member '" + member.name + "' has a type this reader does not know";
                    return false;
                }
                out.blockSize = std::max(out.blockSize, member.offset + member.size);
                out.block.push_back(std::move(member));
            }
            found = true;
        } else if (v.storage == ScUniformConstant && pointeeType->second.op == OpTypeSampledImage) {
            const auto image = types.find(pointeeType->second.a);
            if (image == types.end()) continue;
            SpirvInfo::Sampler s;
            s.name = names[v.id];
            s.binding = bindings[v.id];
            const std::uint32_t dim = image->second.b;
            const bool arrayed = image->second.c != 0;
            s.kind = dim == 3 ? 3 : (dim == 2 ? 2 : (arrayed ? 1 : 0));
            out.samplers.push_back(std::move(s));
        } else if (v.storage == ScInput && builtIns.find(v.id) == builtIns.end() && locations.find(v.id) != locations.end()) {
            SpirvInfo::Input in;
            in.location = locations[v.id];
            in.components = pointeeType->second.op == OpTypeVector ? pointeeType->second.b : 1;
            out.inputs.push_back(in);
        }
    }
    (void)found;
    out.blockSize = (out.blockSize + 15) / 16 * 16;
    return true;
}

}  // namespace falcon::vulkan
