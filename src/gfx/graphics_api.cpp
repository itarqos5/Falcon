#include "falcon/gfx/graphics_api.hpp"

#ifdef FALCON_HAS_VULKAN
#include "falcon/vulkan/vulkan_backend.hpp"
#endif

namespace falcon {

std::string_view graphicsApiId(GraphicsApi api) {
    switch (api) {
        case GraphicsApi::OpenGL: return "opengl";
        case GraphicsApi::Vulkan: return "vulkan";
    }
    return "opengl";
}

GraphicsApi parseGraphicsApi(std::string_view id, GraphicsApi fallback) {
    if (id == "opengl") return GraphicsApi::OpenGL;
    if (id == "vulkan") return GraphicsApi::Vulkan;
    return fallback;
}

std::string_view graphicsApiTranslationKey(GraphicsApi api) {
    switch (api) {
        case GraphicsApi::OpenGL: return "options.graphicsApi.opengl";
        case GraphicsApi::Vulkan: return "options.graphicsApi.vulkan";
    }
    return "options.graphicsApi.opengl";
}

bool graphicsApiRenders(GraphicsApi api) {
    if (api == GraphicsApi::OpenGL) return true;
#ifdef FALCON_HAS_VULKAN
    return vulkan::compiledIn();
#else
    return false;
#endif
}

GraphicsApi effectiveGraphicsApi(GraphicsApi requested, std::string* note) {
    if (graphicsApiRenders(requested)) {
        if (note != nullptr) note->clear();
        return requested;
    }
    if (note != nullptr) {
        *note = "this build has no " + std::string(graphicsApiId(requested)) + " renderer: drawing with opengl";
    }
    return GraphicsApi::OpenGL;
}

std::string graphicsApiTooltip(GraphicsApi api) {
    std::string text = "Which graphics API to use for rendering. Takes effect the next time the game starts.";
    switch (api) {
        case GraphicsApi::OpenGL: break;
        case GraphicsApi::Vulkan:
            if (graphicsApiRenders(api)) {
                text += "\n\nNeeds a graphics card and driver with Vulkan 1.3. When this PC has none, or Vulkan does not start, the game draws with OpenGL instead "
                        "(the log says why).";
            } else {
                text += "\n\nThis build has no Vulkan renderer, so the game draws with OpenGL (your choice is remembered).";
            }
            break;
    }
    return text;
}

}  // namespace falcon
