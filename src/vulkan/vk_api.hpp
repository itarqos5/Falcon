#pragma once

// The Vulkan entry points, loaded by hand (nothing is linked against a Vulkan library). The same way gfx/gl.hpp does it for OpenGL: one list, expanded for the declarations,
// the definitions and the loading.

#include <vulkan/vulkan.h>

#include <string>

namespace falcon::vulkan::api {

// Found with glfwGetInstanceProcAddress(nullptr, ...): the loader's own functions.
#define FALCON_VK_GLOBAL_FUNCTIONS(X) \
    X(vkCreateInstance)               \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)

#define FALCON_VK_INSTANCE_FUNCTIONS(X)           \
    X(vkDestroyInstance)                          \
    X(vkEnumeratePhysicalDevices)                 \
    X(vkGetPhysicalDeviceProperties)              \
    X(vkGetPhysicalDeviceFeatures2)               \
    X(vkGetPhysicalDeviceQueueFamilyProperties)   \
    X(vkGetPhysicalDeviceMemoryProperties)        \
    X(vkGetPhysicalDeviceFormatProperties)        \
    X(vkEnumerateDeviceExtensionProperties)       \
    X(vkCreateDevice)                             \
    X(vkGetDeviceProcAddr)

// Only with a window that can show frames.
#define FALCON_VK_SURFACE_FUNCTIONS(X)            \
    X(vkDestroySurfaceKHR)                        \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)       \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)  \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)       \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

// Only with the validation layer.
#define FALCON_VK_DEBUG_FUNCTIONS(X)              \
    X(vkCreateDebugUtilsMessengerEXT)             \
    X(vkDestroyDebugUtilsMessengerEXT)

#define FALCON_VK_DEVICE_FUNCTIONS(X)             \
    X(vkDestroyDevice)                            \
    X(vkGetDeviceQueue)                           \
    X(vkDeviceWaitIdle)                           \
    X(vkQueueSubmit2)                             \
    X(vkCreateCommandPool)                        \
    X(vkDestroyCommandPool)                       \
    X(vkResetCommandPool)                         \
    X(vkAllocateCommandBuffers)                   \
    X(vkBeginCommandBuffer)                       \
    X(vkEndCommandBuffer)                         \
    X(vkCreateFence)                              \
    X(vkDestroyFence)                             \
    X(vkResetFences)                              \
    X(vkWaitForFences)                            \
    X(vkGetFenceStatus)                           \
    X(vkCreateSemaphore)                          \
    X(vkDestroySemaphore)                         \
    X(vkAllocateMemory)                           \
    X(vkFreeMemory)                               \
    X(vkMapMemory)                                \
    X(vkUnmapMemory)                              \
    X(vkCreateBuffer)                             \
    X(vkDestroyBuffer)                            \
    X(vkGetBufferMemoryRequirements)              \
    X(vkBindBufferMemory)                         \
    X(vkCreateImage)                              \
    X(vkDestroyImage)                             \
    X(vkGetImageMemoryRequirements)               \
    X(vkBindImageMemory)                          \
    X(vkCreateImageView)                          \
    X(vkDestroyImageView)                         \
    X(vkCreateSampler)                            \
    X(vkDestroySampler)                           \
    X(vkCreateShaderModule)                       \
    X(vkDestroyShaderModule)                      \
    X(vkCreateDescriptorSetLayout)                \
    X(vkDestroyDescriptorSetLayout)               \
    X(vkCreatePipelineLayout)                     \
    X(vkDestroyPipelineLayout)                    \
    X(vkCreateDescriptorPool)                     \
    X(vkDestroyDescriptorPool)                    \
    X(vkResetDescriptorPool)                      \
    X(vkAllocateDescriptorSets)                   \
    X(vkUpdateDescriptorSets)                     \
    X(vkCreateGraphicsPipelines)                  \
    X(vkDestroyPipeline)                          \
    X(vkCreatePipelineCache)                      \
    X(vkDestroyPipelineCache)                     \
    X(vkCreateQueryPool)                          \
    X(vkDestroyQueryPool)                         \
    X(vkGetQueryPoolResults)                      \
    X(vkCmdBeginRendering)                        \
    X(vkCmdEndRendering)                          \
    X(vkCmdBindPipeline)                          \
    X(vkCmdSetViewport)                           \
    X(vkCmdSetScissor)                            \
    X(vkCmdBindDescriptorSets)                    \
    X(vkCmdBindVertexBuffers)                     \
    X(vkCmdBindIndexBuffer)                       \
    X(vkCmdDraw)                                  \
    X(vkCmdDrawIndexed)                           \
    X(vkCmdDrawIndexedIndirect)                   \
    X(vkCmdCopyBuffer)                            \
    X(vkCmdCopyBufferToImage)                     \
    X(vkCmdCopyImageToBuffer)                     \
    X(vkCmdBlitImage)                             \
    X(vkCmdPipelineBarrier2)                      \
    X(vkCmdClearAttachments)                      \
    X(vkCmdResetQueryPool)                        \
    X(vkCmdWriteTimestamp2)

// Only with a swapchain.
#define FALCON_VK_SWAPCHAIN_FUNCTIONS(X)          \
    X(vkCreateSwapchainKHR)                       \
    X(vkDestroySwapchainKHR)                      \
    X(vkGetSwapchainImagesKHR)                    \
    X(vkAcquireNextImageKHR)                      \
    X(vkQueuePresentKHR)

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define FALCON_VK_DECLARE(name) extern PFN_##name name;
FALCON_VK_GLOBAL_FUNCTIONS(FALCON_VK_DECLARE)
FALCON_VK_INSTANCE_FUNCTIONS(FALCON_VK_DECLARE)
FALCON_VK_SURFACE_FUNCTIONS(FALCON_VK_DECLARE)
FALCON_VK_DEBUG_FUNCTIONS(FALCON_VK_DECLARE)
FALCON_VK_DEVICE_FUNCTIONS(FALCON_VK_DECLARE)
FALCON_VK_SWAPCHAIN_FUNCTIONS(FALCON_VK_DECLARE)
#undef FALCON_VK_DECLARE

// Finds the loader (through GLFW, which must be initialised) and the global functions. False (with `reason`) when there is no Vulkan on this PC.
[[nodiscard]] bool loadGlobal(std::string& reason);
// The functions that need an instance; `surface` also loads the surface ones, `debug` the messenger ones. Missing ones are reported in `reason`.
[[nodiscard]] bool loadInstance(VkInstance instance, bool surface, bool debug, std::string& reason);
// The functions of a device; `swapchain` also loads the swapchain ones.
[[nodiscard]] bool loadDevice(VkDevice device, bool swapchain, std::string& reason);
// Forgets everything (after the device and the instance are gone).
void unload();

// A VkResult in words, for the messages.
[[nodiscard]] const char* resultName(VkResult result);

}  // namespace falcon::vulkan::api
