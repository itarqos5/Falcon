#include "vk_api.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace falcon::vulkan::api {

PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

#define FALCON_VK_DEFINE(name) PFN_##name name = nullptr;
FALCON_VK_GLOBAL_FUNCTIONS(FALCON_VK_DEFINE)
FALCON_VK_INSTANCE_FUNCTIONS(FALCON_VK_DEFINE)
FALCON_VK_SURFACE_FUNCTIONS(FALCON_VK_DEFINE)
FALCON_VK_DEBUG_FUNCTIONS(FALCON_VK_DEFINE)
FALCON_VK_DEVICE_FUNCTIONS(FALCON_VK_DEFINE)
FALCON_VK_SWAPCHAIN_FUNCTIONS(FALCON_VK_DEFINE)
#undef FALCON_VK_DEFINE

namespace {
VkDevice g_device = VK_NULL_HANDLE;
}

bool loadGlobal(std::string& reason) {
    if (vkGetInstanceProcAddr != nullptr) return true;
    if (glfwVulkanSupported() == GLFW_FALSE) {
        reason = "no Vulkan loader on this PC (vulkan-1.dll / libvulkan.so.1)";
        return false;
    }
    // GLFW has loaded the loader: its own function is what everything else is found with.
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
    if (vkGetInstanceProcAddr == nullptr) {
        reason = "the Vulkan loader does not give out vkGetInstanceProcAddr";
        return false;
    }
    std::string missing;
#define FALCON_VK_LOAD(name)                                                                            \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));                \
    if (name == nullptr) missing += " " #name;
    FALCON_VK_GLOBAL_FUNCTIONS(FALCON_VK_LOAD)
#undef FALCON_VK_LOAD
    if (!missing.empty()) {
        reason = "the Vulkan loader is missing:" + missing;
        vkGetInstanceProcAddr = nullptr;
        return false;
    }
    return true;
}

bool loadInstance(VkInstance instance, bool surface, bool debug, std::string& reason) {
    std::string missing;
#define FALCON_VK_LOAD(name)                                                                    \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));              \
    if (name == nullptr) missing += " " #name;
    FALCON_VK_INSTANCE_FUNCTIONS(FALCON_VK_LOAD)
    if (surface) {
        FALCON_VK_SURFACE_FUNCTIONS(FALCON_VK_LOAD)
    }
    if (debug) {
        FALCON_VK_DEBUG_FUNCTIONS(FALCON_VK_LOAD)
    }
#undef FALCON_VK_LOAD
    if (!missing.empty()) {
        reason = "the Vulkan instance is missing:" + missing;
        return false;
    }
    return true;
}

bool loadDevice(VkDevice device, bool swapchain, std::string& reason) {
    std::string missing;
    g_device = device;
#define FALCON_VK_LOAD(name)                                                                    \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                  \
    if (name == nullptr) missing += " " #name;
    FALCON_VK_DEVICE_FUNCTIONS(FALCON_VK_LOAD)
    if (swapchain) {
        FALCON_VK_SWAPCHAIN_FUNCTIONS(FALCON_VK_LOAD)
    }
#undef FALCON_VK_LOAD
    if (!missing.empty()) {
        reason = "the Vulkan device is missing:" + missing;
        return false;
    }
    return true;
}

void unload() {
    g_device = VK_NULL_HANDLE;
#define FALCON_VK_CLEAR(name) name = nullptr;
    FALCON_VK_INSTANCE_FUNCTIONS(FALCON_VK_CLEAR)
    FALCON_VK_SURFACE_FUNCTIONS(FALCON_VK_CLEAR)
    FALCON_VK_DEBUG_FUNCTIONS(FALCON_VK_CLEAR)
    FALCON_VK_DEVICE_FUNCTIONS(FALCON_VK_CLEAR)
    FALCON_VK_SWAPCHAIN_FUNCTIONS(FALCON_VK_CLEAR)
#undef FALCON_VK_CLEAR
}

const char* resultName(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "a Vulkan error";
    }
}

}  // namespace falcon::vulkan::api
