#include "shader_registry.hpp"

#include "spirv_data.hpp"

#include <cstring>

namespace falcon::vulkan {

const SpirvBlob* findTwin(bool vertexStage, const std::string& source, std::uint32_t& specMask, std::string& why) {
    specMask = 0;
    if (source.find("#define LIGHT_LUT") != std::string::npos) specMask |= kSpecLightLut;
    if (source.find("#define FLAT_NORMAL") != std::string::npos) specMask |= kSpecFlatNormal;
    const std::string tag = "// vk:";
    const std::size_t at = source.find(tag);
    if (at == std::string::npos) {
        why = "the shader has no \"// vk: <name>\" line, so there is no Vulkan twin to use";
        return nullptr;
    }
    std::size_t from = at + tag.size();
    while (from < source.size() && source[from] == ' ') ++from;
    std::size_t to = from;
    while (to < source.size() && source[to] != '\n' && source[to] != '\r' && source[to] != ' ') ++to;
    const std::string name = source.substr(from, to - from);
    const bool wantsVertex = name.size() > 5 && name.compare(name.size() - 5, 5, ".vert") == 0;
    if (wantsVertex != vertexStage) {
        why = "the shader is tagged '" + name + "' but is a " + (vertexStage ? "vertex" : "fragment") + " shader";
        return nullptr;
    }
    for (std::size_t i = 0; i < kSpirvBlobCount; ++i) {
        if (kSpirvBlobs[i].name != nullptr && name == kSpirvBlobs[i].name) return &kSpirvBlobs[i];
    }
    why = "there is no Vulkan shader '" + name + "' in this build";
    return nullptr;
}

}  // namespace falcon::vulkan
