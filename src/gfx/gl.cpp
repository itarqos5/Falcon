#include "falcon/gfx/gl.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

namespace falcon::gl {

#define FALCON_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
FALCON_GL_FUNCTIONS(FALCON_GL_DEFINE)
#undef FALCON_GL_DEFINE

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
}

}  // namespace falcon::gl
