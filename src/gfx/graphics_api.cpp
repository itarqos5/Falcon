#include "apex/gfx/graphics_api.hpp"

namespace apex {

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

bool graphicsApiRenders(GraphicsApi api) { return api == GraphicsApi::OpenGL; }

GraphicsApi effectiveGraphicsApi(GraphicsApi requested, std::string* note) {
    if (graphicsApiRenders(requested)) {
        if (note != nullptr) note->clear();
        return requested;
    }
    if (note != nullptr) {
        *note = "the " + std::string(graphicsApiId(requested)) + " renderer is not built yet: drawing with opengl";
    }
    return GraphicsApi::OpenGL;
}

std::string graphicsApiTooltip(GraphicsApi api) {
    std::string text = "Which graphics API to use for rendering. Takes effect the next time the game starts.";
    switch (api) {
        case GraphicsApi::OpenGL: break;
        case GraphicsApi::Vulkan:
            text += "\n\nNot available in Falcon yet: the Vulkan renderer is still to be written, so the game keeps drawing with OpenGL "
                    "(your choice is remembered).";
            break;
    }
    return text;
}

}  // namespace apex
