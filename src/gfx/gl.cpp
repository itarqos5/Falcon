#include "falcon/gfx/gl.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace falcon::gl {

#define FALCON_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
FALCON_GL_FUNCTIONS(FALCON_GL_DEFINE)
FALCON_GL_OPTIONAL_FUNCTIONS(FALCON_GL_DEFINE)
#undef FALCON_GL_DEFINE

int contextMajor = 0;
int contextMinor = 0;

void load() {
    std::string missing;
#define FALCON_GL_LOAD(ret, name, args)                                                  \
    name = reinterpret_cast<PFN_##name>(glfwGetProcAddress("gl" #name));               \
    if (name == nullptr) missing += " gl" #name;
    FALCON_GL_FUNCTIONS(FALCON_GL_LOAD)
#undef FALCON_GL_LOAD
    if (!missing.empty()) {
        throw std::runtime_error("OpenGL 3.3 is required; missing entry points:" + missing);
    }
    // The newer entry points are optional: their absence only means the older way of drawing is used.
#define FALCON_GL_LOAD_OPTIONAL(ret, name, args) name = reinterpret_cast<PFN_##name>(glfwGetProcAddress("gl" #name));
    FALCON_GL_OPTIONAL_FUNCTIONS(FALCON_GL_LOAD_OPTIONAL)
#undef FALCON_GL_LOAD_OPTIONAL
    const char* version = reinterpret_cast<const char*>(GetString(VERSION));
    if (version == nullptr || std::sscanf(version, "%d.%d", &contextMajor, &contextMinor) != 2) {
        contextMajor = 3;
        contextMinor = 3;
    }
}

bool multiDrawIndirectAvailable() {
    static const bool forcedOff = std::getenv("FALCON_NO_MDI") != nullptr;  // (to compare with the one-call-per-section way of drawing)
    return !forcedOff && MultiDrawElementsIndirect != nullptr && (contextMajor > 4 || (contextMajor == 4 && contextMinor >= 3));
}

}  // namespace falcon::gl
