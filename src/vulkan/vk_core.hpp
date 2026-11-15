#pragma once

// The Vulkan device and what lives next to it: memory, per-frame resources, command buffers, deferred destruction.

#include "vk_api.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace falcon::vulkan {

constexpr int kFramesInFlight = 2;
constexpr int kMaxSamplers = 8;                      // combined image samplers a program may have (set 1, bindings 0..7)
constexpr VkDeviceSize kUniformRange = 1024;         // the range of the dynamic uniform descriptor: no program's block is larger
constexpr std::uint32_t kQueriesPerFrame = 64;       // timestamp queries (two per GPU timer measurement)

// A block of device memory, mapped when it is visible to the host.
struct Memory {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

// A buffer with its own memory.
struct DeviceBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    Memory memory;
    VkDeviceSize size = 0;
};

struct DeviceImage {
    VkImage image = VK_NULL_HANDLE;
    Memory memory;
};

// A piece of host-visible memory for this frame only: what is uploaded to the GPU (staging), uniform data, vertex data of the buffers that are re-made every frame, the draw commands.
struct HostSlice {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    void* ptr = nullptr;
};

// A bump allocator over big host-visible buffers; emptied when its frame is done with.
class HostArena {
public:
    HostSlice allocate(VkDeviceSize size, VkDeviceSize alignment);
    void reset();
    void destroy();
    [[nodiscard]] VkDeviceSize bytesUsed() const { return used_; }

private:
    struct Chunk {
        DeviceBuffer buffer;
        VkDeviceSize used = 0;
    };
    std::vector<Chunk> chunks_;
    std::size_t current_ = 0;
    VkDeviceSize used_ = 0;
};

// What a set of samplers is bound to, for the descriptor set cache.
struct SamplerSetKey {
    VkImageView views[kMaxSamplers] = {};
    VkSampler samplers[kMaxSamplers] = {};
    bool operator==(const SamplerSetKey& other) const;
};
struct SamplerSetKeyHash {
    std::size_t operator()(const SamplerSetKey& key) const;
};

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;  // the swapchain image is ready to be drawn on
    bool recording = false;                 // cmd is between begin and end
    bool inFlight = false;                  // submitted and not yet waited for
    std::uint64_t serial = 0;               // the number of its submission (0: never)
    HostArena arena;
    std::vector<VkDescriptorPool> descriptorPools;
    std::size_t descriptorPoolIndex = 0;
    std::unordered_map<VkBuffer, VkDescriptorSet> uniformSets;
    std::unordered_map<SamplerSetKey, VkDescriptorSet, SamplerSetKeyHash> samplerSets;
    std::vector<std::function<void()>> garbage;  // destroyers of objects this frame's commands may still use
    VkQueryPool queryPool = VK_NULL_HANDLE;
    std::uint32_t queriesUsed = 0;
    std::vector<std::function<void()>> onRetire;  // run when the frame's commands are known to be done (timer results)
};

struct Core {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    std::string deviceName;
    std::string versionText;
    std::string vendorName;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    int validationErrors = 0;
    bool validation = false;

    GLFWwindow* window = nullptr;
    bool presentable = false;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // Fixed objects every pipeline shares.
    VkDescriptorSetLayout uniformLayout = VK_NULL_HANDLE;  // set 0: one dynamic uniform buffer
    VkDescriptorSetLayout samplerLayout = VK_NULL_HANDLE;  // set 1: kMaxSamplers combined image samplers
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    Frame frames[kFramesInFlight];
    int current = 0;                  // the frame being recorded
    bool frameBegun = false;          // beginFrame() has run for `current` and the frame has not been submitted
    std::uint64_t submitCount = 0;    // submissions so far; the open frame will be number submitCount + 1
    std::uint64_t frameNumber = 0;    // frames begun, for the caches that are per frame
};

[[nodiscard]] Core& core();
[[nodiscard]] Frame& frame();  // the frame being recorded (begins it if need be)

// Looks for a device that can draw the game without making one (a short-lived instance). The answer names the device, or says why there is none.
[[nodiscard]] bool probeDevice(std::string& info);

// ---- device creation ----------------------------------------------------------------------------------------------------------------------------------------
// The instance, the device and the objects that never change. `window` may be null when `presentable` is false.
[[nodiscard]] bool createCore(GLFWwindow* window, bool presentable, std::string& reason);
void destroyCore();

// ---- memory -------------------------------------------------------------------------------------------------------------------------------------------------
[[nodiscard]] int findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags avoided = 0);
[[nodiscard]] bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, DeviceBuffer& out);
void destroyBuffer(DeviceBuffer& buffer);
[[nodiscard]] bool createImage(const VkImageCreateInfo& info, DeviceImage& out);
void destroyImage(DeviceImage& image);

// ---- frames and commands ------------------------------------------------------------------------------------------------------------------------------------
// Waits for the frame slot `current` is about to use, retires what it held, and starts recording its command buffer.
void beginFrame();
// A command buffer that is recording (begins the frame first when there is none).
[[nodiscard]] VkCommandBuffer cmd();
// Ends the command buffer and submits it (with `waitSemaphore` at the transfer stage and `signalSemaphore` when given). The frame slot's fence is signalled by it.
void submitFrame(VkSemaphore waitSemaphore, VkSemaphore signalSemaphore);
// Moves on to the next frame slot (after submitFrame).
void advanceFrame();
// Submits what has been recorded and waits until the GPU has finished it, then goes on recording in a fresh command buffer of the same slot.
void flushAndWait();
// Waits for a submission by its number (0 or older ones: returns at once). `timeoutNs` 0: only looks. True when it is done.
[[nodiscard]] bool waitForSerial(std::uint64_t serial, std::uint64_t timeoutNs);
// Runs `destroyer` when the current frame's commands are done.
void defer(std::function<void()> destroyer);

// A descriptor set for the dynamic uniform buffer on `buffer` (cached for the frame), and one for a set of samplers.
[[nodiscard]] VkDescriptorSet uniformSetFor(VkBuffer buffer);
[[nodiscard]] VkDescriptorSet samplerSetFor(const SamplerSetKey& key, std::uint32_t usedMask);

// A sampler for these settings (cached for the life of the device).
[[nodiscard]] VkSampler samplerFor(VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipmap, VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w, float maxLod);

}  // namespace falcon::vulkan
