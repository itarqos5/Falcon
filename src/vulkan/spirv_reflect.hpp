#pragma once

// Just enough SPIR-V reading to turn the names of GL uniforms into offsets and bindings: the one uniform block (set 0, binding 0), the samplers (set 1) and the vertex inputs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace falcon::vulkan {

struct SpirvInfo {
    struct Member {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };
    struct Sampler {
        std::string name;
        std::uint32_t binding = 0;
        int kind = 0;  // 0: 2D, 1: 2D array, 2: 3D, 3: cube (the order of GlTexture's kinds)
    };
    struct Input {
        std::uint32_t location = 0;
        std::uint32_t components = 4;
    };
    std::vector<Member> block;  // the members of the uniform block at set 0, binding 0
    std::uint32_t blockSize = 0;
    std::vector<Sampler> samplers;
    std::vector<Input> inputs;
};

// False (with `error`) for a module that is not SPIR-V or that holds something this reader does not know.
[[nodiscard]] bool reflectSpirv(const std::uint32_t* words, std::size_t count, SpirvInfo& out, std::string& error);

}  // namespace falcon::vulkan
