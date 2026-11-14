#include "apex/gfx/gl.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

namespace apex::gl {

#define APEX_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
APEX_GL_FUNCTIONS(APEX_GL_DEFINE)
#undef APEX_GL_DEFINE

void load() {
    std::string missing;
#define APEX_GL_LOAD(ret, name, args)                                                  \
    name = reinterpret_cast<PFN_##name>(glfwGetProcAddress("gl" #name));               \
    if (name == nullptr) missing += " gl" #name;
    APEX_GL_FUNCTIONS(APEX_GL_LOAD)
#undef APEX_GL_LOAD
    if (!missing.empty()) {
        throw std::runtime_error("OpenGL 3.3 is required; missing entry points:" + missing);
    }
}

}  // namespace apex::gl
