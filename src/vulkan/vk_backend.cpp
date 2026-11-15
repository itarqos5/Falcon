#include "falcon/vulkan/vulkan_backend.hpp"

#include "gl_state.hpp"
#include "spirv_data.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace falcon::vulkan {

namespace {

bool g_active = false;

void clearGlFunctions() {
#define FALCON_GLEMU_CLEAR(ret, name, args) gl::name = nullptr;
    FALCON_GL_FUNCTIONS(FALCON_GLEMU_CLEAR)
    FALCON_GL_OPTIONAL_FUNCTIONS(FALCON_GLEMU_CLEAR)
#undef FALCON_GLEMU_CLEAR
}

}  // namespace

bool compiledIn() { return true; }

bool usable(std::string* reason) {
    static bool looked = false;
    static bool ok = false;
    static std::string text;
    if (!looked) {
        looked = true;
        if (kSpirvBlobCount == 0) {
            text = "this build has no Vulkan shaders";
        } else {
            ok = probeDevice(text);
        }
    }
    if (reason != nullptr) *reason = text;
    return ok;
}

bool start(GLFWwindow* window, bool presentable, bool vsync, std::string& reason) {
    if (g_active) return true;
    if (kSpirvBlobCount == 0) {
        reason = "this build has no Vulkan shaders";
        return false;
    }
    if (!createCore(window, presentable, reason)) return false;
    try {
        resetGlState();
        // What a vertex input with no array behind it reads: (0, 0, 0, 1), as OpenGL's constant attribute does.
        GlState& s = gls();
        if (!createBuffer(16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, s.zeroBuffer)) throw std::runtime_error("Vulkan: no memory for a small buffer");
        const float defaults[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(s.zeroBuffer.memory.mapped, defaults, sizeof defaults);
        installGlFunctions();
        setSwapchainVsync(vsync);
        prepareFrame();
        createDummyTextures();
    } catch (const std::exception& e) {
        reason = e.what();
        g_active = true;  // (so that stop() cleans up what was made)
        stop();
        return false;
    }
    g_active = true;
    return true;
}

void stop() {
    if (!g_active) return;
    Core& c = core();
    if (c.device != VK_NULL_HANDLE) {
        endRendering();
        api::vkDeviceWaitIdle(c.device);
        destroyProgramObjects();
        destroyTextureObjects();
        destroyBufferObjects();
        destroyBackbuffer();
        destroySwapchain();
    }
    const bool validation = c.validation;
    destroyCore();
    clearGlFunctions();
    resetGlState();
    g_active = false;
    if (validation) std::fprintf(stderr, "[vulkan] validation: %d error(s)\n", core().validationErrors);
}

bool active() { return g_active; }

void present() {
    if (!g_active) return;
    endRendering();
    presentBackbuffer();
}

void setVsync(bool vsync) {
    if (g_active) setSwapchainVsync(vsync);
}

void resized() {
    if (g_active) markSwapchainDirty();
}

const std::string& deviceName() { return core().deviceName; }
const std::string& apiVersionText() { return core().versionText; }
int validationErrors() { return core().validationErrors; }

}  // namespace falcon::vulkan
