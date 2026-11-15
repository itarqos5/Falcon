#pragma once

// The game's shaders are GLSL 330 text, handed to gl::ShaderSource. Vulkan takes SPIR-V, so each of them has a twin written for Vulkan (src/vulkan/shaders/), compiled
// ahead of time. The text of a shader says which twin it has with a comment line, "// vk: gui.frag"; the lines "#define LIGHT_LUT" and "#define FLAT_NORMAL" that the
// world shader is given by the world renderer choose between the variants of its twin (the twin has them as specialization constants).

#include <cstdint>
#include <string>

namespace falcon::vulkan {

struct SpirvBlob;

// Bits of the specialization mask.
constexpr std::uint32_t kSpecLightLut = 1u;
constexpr std::uint32_t kSpecFlatNormal = 2u;

// The twin of a shader source of `vertexStage` or fragment kind: its SPIR-V, or null with `why` saying what is wrong with the source.
[[nodiscard]] const SpirvBlob* findTwin(bool vertexStage, const std::string& source, std::uint32_t& specMask, std::string& why);

}  // namespace falcon::vulkan
