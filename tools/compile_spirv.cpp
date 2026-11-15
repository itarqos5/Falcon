// A small GLSL to SPIR-V compiler on glslang's libraries, for compiling the Vulkan shaders (tools/compile_vk_shaders.mjs) where no glslangValidator is installed. It takes the
// arguments that glslangValidator would be given there, and nothing else:
//
//   falcon_compile_spirv -V --target-env vulkan1.2 -o out.spv shader.vert
//
// The stage comes from the extension (.vert, .frag). Built by -DFALCON_COMPILE_SHADERS=ON (see the root CMakeLists.txt).
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool endsWith(const std::string& text, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string input, output;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--target-env" && i + 1 < argc) ++i;  // (vulkan1.2: what this tool always makes)
        else if (a == "-V") continue;
        else input = a;
    }
    if (input.empty() || output.empty()) {
        std::fputs("usage: falcon_compile_spirv -V --target-env vulkan1.2 -o <out.spv> <shader.vert|shader.frag>\n", stderr);
        return 2;
    }
    EShLanguage stage;
    if (endsWith(input, ".vert")) stage = EShLangVertex;
    else if (endsWith(input, ".frag")) stage = EShLangFragment;
    else {
        std::fprintf(stderr, "%s: the stage is taken from the extension (.vert or .frag)\n", input.c_str());
        return 2;
    }
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", input.c_str());
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    glslang::InitializeProcess();
    int status = 0;
    {
        const char* strings[1] = {text.c_str()};
        glslang::TShader shader(stage);
        shader.setStrings(strings, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
        const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
            std::fprintf(stderr, "%s:\n%s%s\n", input.c_str(), shader.getInfoLog(), shader.getInfoDebugLog());
            status = 1;
        } else {
            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(messages)) {
                std::fprintf(stderr, "%s (link):\n%s%s\n", input.c_str(), program.getInfoLog(), program.getInfoDebugLog());
                status = 1;
            } else {
                std::vector<unsigned int> words;
                spv::SpvBuildLogger logger;
                glslang::SpvOptions options;  // (names are kept: the game finds its uniforms by them)
                glslang::GlslangToSpv(*program.getIntermediate(stage), words, &logger, &options);
                const std::string messagesText = logger.getAllMessages();
                if (!messagesText.empty()) std::fprintf(stderr, "%s: %s\n", input.c_str(), messagesText.c_str());
                std::ofstream out(output, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(unsigned int)));
                if (!out) {
                    std::fprintf(stderr, "cannot write %s\n", output.c_str());
                    status = 2;
                }
            }
        }
    }
    glslang::FinalizeProcess();
    return status;
}
