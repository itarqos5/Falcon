#pragma once

// The SPIR-V of the Vulkan shaders (src/vulkan/shaders/), compiled by tools/compile_vk_shaders.mjs into spirv_data.cpp.

#include <cstddef>
#include <cstdint>

namespace falcon::vulkan {

struct SpirvBlob {
    const char* name;  // "gui.vert"
    const std::uint32_t* words;
    std::size_t wordCount;
};

extern const SpirvBlob kSpirvBlobs[];
extern const std::size_t kSpirvBlobCount;

}  // namespace falcon::vulkan
