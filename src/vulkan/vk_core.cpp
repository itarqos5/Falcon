#include "vk_core.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace falcon::vulkan {

namespace {

Core g_core;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) { return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment; }

constexpr VkDeviceSize kChunkSize = 64ull * 1024 * 1024;

std::unordered_map<std::uint64_t, VkSampler>& samplerCache() {
    static std::unordered_map<std::uint64_t, VkSampler> cache;
    return cache;
}

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: ") + what + " failed (" + api::resultName(result) + ")");
}

bool wantValidation() {
    const char* value = std::getenv("FALCON_VK_VALIDATION");
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool warning = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
    if (!error && !warning) return VK_FALSE;
    if (error) ++g_core.validationErrors;
    std::fprintf(stderr, "[vulkan] validation %s: %s\n", error ? "ERROR" : "warning", data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no text)");
    return VK_FALSE;
}

bool layerPresent(const char* name) {
    std::uint32_t count = 0;
    api::vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    api::vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

bool extensionPresent(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool formatHas(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags optimal, VkFormatFeatureFlags buffer = 0) {
    VkFormatProperties p{};
    api::vkGetPhysicalDeviceFormatProperties(device, format, &p);
    return (p.optimalTilingFeatures & optimal) == optimal && (p.bufferFeatures & buffer) == buffer;
}

struct Choice {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkPhysicalDeviceProperties properties{};
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
};

// Whether a device can draw the game, and why not.
bool suitable(VkPhysicalDevice device, VkSurfaceKHR surface, Choice& out, std::string& why) {
    VkPhysicalDeviceProperties props{};
    api::vkGetPhysicalDeviceProperties(device, &props);
    const std::string name = props.deviceName;
    if (props.apiVersion < VK_API_VERSION_1_3) {
        why = name + " is Vulkan " + std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + " (1.3 is needed)";
        return false;
    }
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    api::vkGetPhysicalDeviceFeatures2(device, &f2);
    if (f13.dynamicRendering == VK_FALSE || f13.synchronization2 == VK_FALSE) {
        why = name + " has no dynamic rendering / synchronization2";
        return false;
    }
    if (surface != VK_NULL_HANDLE) {
        std::uint32_t count = 0;
        api::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> extensions(count);
        api::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
        if (!extensionPresent(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            why = name + " cannot show frames (no swapchain)";
            return false;
        }
    }
    // The meshes' vertex formats (16-bit integers read as numbers) and the textures' formats.
    if (!formatHas(device, VK_FORMAT_R16G16_SSCALED, 0, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) || !formatHas(device, VK_FORMAT_R16G16B16A16_SSCALED, 0, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
        why = name + " cannot read 16-bit scaled vertex data";
        return false;
    }
    const VkFormatFeatureFlags filtered = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    // (RGBA8 is what pictures are copied, drawn into, blitted for mip levels and for the swapchain; the other two are only sampled.)
    if (!formatHas(device, VK_FORMAT_R8G8B8A8_UNORM, filtered | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
        !formatHas(device, VK_FORMAT_R8_UNORM, filtered) || !formatHas(device, VK_FORMAT_R16G16B16A16_SFLOAT, filtered)) {
        why = name + " lacks a texture format";
        return false;
    }
    bool haveDepth = false;
    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        if (formatHas(device, candidate, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            out.depthFormat = candidate;
            out.depthAspect = candidate == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
            haveDepth = true;
            break;
        }
    }
    if (!haveDepth) {
        why = name + " has no depth format";
        return false;
    }
    // A queue that draws (and shows, when there is a surface).
    std::uint32_t families = 0;
    api::vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
    std::vector<VkQueueFamilyProperties> list(families);
    api::vkGetPhysicalDeviceQueueFamilyProperties(device, &families, list.data());
    bool found = false;
    for (std::uint32_t i = 0; i < families && !found; ++i) {
        if ((list[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || list[i].timestampValidBits == 0) continue;
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present = VK_FALSE;
            api::vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
            if (present == VK_FALSE) continue;
        }
        out.family = i;
        found = true;
    }
    if (!found) {
        why = name + " has no queue that draws" + (surface != VK_NULL_HANDLE ? " and shows frames" : "");
        return false;
    }
    out.device = device;
    out.properties = props;
    out.multiDrawIndirect = false;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    api::vkGetPhysicalDeviceFeatures2(device, &features);
    out.multiDrawIndirect = features.features.multiDrawIndirect != VK_FALSE;
    out.drawIndirectFirstInstance = features.features.drawIndirectFirstInstance != VK_FALSE;
    return true;
}

int rank(const VkPhysicalDeviceProperties& p) {
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
        default: return 0;
    }
}

bool choose(VkInstance instance, VkSurfaceKHR surface, Choice& best, std::string& reason) {
    std::uint32_t count = 0;
    api::vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        reason = "no Vulkan device";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    api::vkEnumeratePhysicalDevices(instance, &count, devices.data());
    const char* wanted = std::getenv("FALCON_VK_DEVICE");  // (a part of a device's name, to choose one of several)
    bool have = false;
    std::string why;
    for (const VkPhysicalDevice device : devices) {
        Choice candidate;
        std::string problem;
        if (!suitable(device, surface, candidate, problem)) {
            if (!why.empty()) why += "; ";
            why += problem;
            continue;
        }
        if (wanted != nullptr && *wanted != '\0' && std::string(candidate.properties.deviceName).find(wanted) == std::string::npos) continue;
        if (!have || rank(candidate.properties) > rank(best.properties)) {
            best = candidate;
            have = true;
        }
    }
    if (!have) reason = why.empty() ? std::string("no device with that name") : why;
    return have;
}

const char* vendorOf(std::uint32_t id) {
    switch (id) {
        case 0x1002: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x106B: return "Apple";
        case 0x10005: return "Mesa";
        default: return "Unknown";
    }
}

bool createInstance(bool presentable, std::string& reason) {
    Core& c = g_core;
    std::vector<const char*> extensions;
    if (presentable) {
        std::uint32_t count = 0;
        const char** required = glfwGetRequiredInstanceExtensions(&count);
        if (required == nullptr) {
            reason = "this window system cannot show Vulkan frames";
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) extensions.push_back(required[i]);
    }
    std::vector<const char*> layers;
    c.validation = false;
    if (wantValidation()) {
        std::uint32_t extCount = 0;
        api::vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        api::vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
        if (layerPresent("VK_LAYER_KHRONOS_validation") && extensionPresent(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            c.validation = true;
        } else {
            std::fprintf(stderr, "[vulkan] FALCON_VK_VALIDATION is set but the validation layer is not installed\n");
        }
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Falcon";
    app.applicationVersion = 1;
    app.pEngineName = "Falcon";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    const VkResult result = api::vkCreateInstance(&info, nullptr, &c.instance);
    if (result != VK_SUCCESS) {
        reason = std::string("vkCreateInstance failed (") + api::resultName(result) + ")";
        c.instance = VK_NULL_HANDLE;
        return false;
    }
    if (!api::loadInstance(c.instance, presentable, c.validation, reason)) return false;
    if (c.validation) {
        VkDebugUtilsMessengerCreateInfoEXT m{};
        m.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = debugCallback;
        api::vkCreateDebugUtilsMessengerEXT(c.instance, &m, nullptr, &c.messenger);
    }
    return true;
}

void createFixedObjects() {
    Core& c = g_core;
    // set 0: the dynamic uniform buffer (every program's non-sampler uniforms in one block), set 1: the samplers.
    VkDescriptorSetLayoutBinding ub{};
    ub.binding = 0;
    ub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ub.descriptorCount = 1;
    ub.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo l0{};
    l0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    l0.bindingCount = 1;
    l0.pBindings = &ub;
    check(api::vkCreateDescriptorSetLayout(c.device, &l0, nullptr, &c.uniformLayout), "vkCreateDescriptorSetLayout");

    VkDescriptorSetLayoutBinding sb[kMaxSamplers]{};
    for (int i = 0; i < kMaxSamplers; ++i) {
        sb[i].binding = static_cast<std::uint32_t>(i);
        sb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sb[i].descriptorCount = 1;
        sb[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo l1{};
    l1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    l1.bindingCount = kMaxSamplers;
    l1.pBindings = sb;
    check(api::vkCreateDescriptorSetLayout(c.device, &l1, nullptr, &c.samplerLayout), "vkCreateDescriptorSetLayout");

    const VkDescriptorSetLayout layouts[2] = {c.uniformLayout, c.samplerLayout};
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 2;
    pl.pSetLayouts = layouts;
    check(api::vkCreatePipelineLayout(c.device, &pl, nullptr, &c.pipelineLayout), "vkCreatePipelineLayout");

    VkPipelineCacheCreateInfo pc{};
    pc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    check(api::vkCreatePipelineCache(c.device, &pc, nullptr, &c.pipelineCache), "vkCreatePipelineCache");

    for (Frame& f : c.frames) {
        VkCommandPoolCreateInfo cp{};
        cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        cp.queueFamilyIndex = c.queueFamily;
        check(api::vkCreateCommandPool(c.device, &cp, nullptr, &f.pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo ca{};
        ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ca.commandPool = f.pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        check(api::vkAllocateCommandBuffers(c.device, &ca, &f.cmd), "vkAllocateCommandBuffers");
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(api::vkCreateFence(c.device, &fi, nullptr, &f.fence), "vkCreateFence");
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(api::vkCreateSemaphore(c.device, &si, nullptr, &f.acquired), "vkCreateSemaphore");
        VkQueryPoolCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = kQueriesPerFrame;
        check(api::vkCreateQueryPool(c.device, &qi, nullptr, &f.queryPool), "vkCreateQueryPool");
    }
}

VkDescriptorPool newDescriptorPool() {
    constexpr std::uint32_t kSets = 512;
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSets}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets * kMaxSamplers}};
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = kSets;
    info.poolSizeCount = 2;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(api::vkCreateDescriptorPool(g_core.device, &info, nullptr, &pool), "vkCreateDescriptorPool");
    return pool;
}

// A set of `layout` from the frame's pools, starting another pool when one is full.
VkDescriptorSet allocateSet(Frame& f, VkDescriptorSetLayout layout) {
    for (;;) {
        if (f.descriptorPoolIndex >= f.descriptorPools.size()) f.descriptorPools.push_back(newDescriptorPool());
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = f.descriptorPools[f.descriptorPoolIndex];
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult result = api::vkAllocateDescriptorSets(g_core.device, &info, &set);
        if (result == VK_SUCCESS) return set;
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            ++f.descriptorPoolIndex;
            continue;
        }
        check(result, "vkAllocateDescriptorSets");
    }
}

void retire(Frame& f) {
    for (auto& fn : f.onRetire) fn();
    f.onRetire.clear();
    for (auto& fn : f.garbage) fn();
    f.garbage.clear();
}

void beginRecording(Frame& f) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(api::vkBeginCommandBuffer(f.cmd, &bi), "vkBeginCommandBuffer");
    f.recording = true;
}

}  // namespace

Core& core() { return g_core; }

Frame& frame() {
    if (!g_core.frameBegun) beginFrame();
    return g_core.frames[g_core.current];
}

// ---- HostArena ----------------------------------------------------------------------------------------------------------------------------------------------

HostSlice HostArena::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    if (alignment == 0) alignment = 4;
    // (Every chunk keeps kUniformRange of room at its end: a uniform descriptor covers that much from any offset.)
    while (current_ < chunks_.size()) {
        Chunk& chunk = chunks_[current_];
        const VkDeviceSize start = alignUp(chunk.used, alignment);
        if (start + size + kUniformRange <= chunk.buffer.size) {
            chunk.used = start + size;
            used_ += size;
            return HostSlice{chunk.buffer.buffer, start, static_cast<char*>(chunk.buffer.memory.mapped) + start};
        }
        ++current_;
    }
    Chunk chunk;
    const VkDeviceSize want = std::max(kChunkSize, alignUp(size + kUniformRange + alignment, 1024 * 1024));
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (!createBuffer(want, usage, true, chunk.buffer)) throw std::runtime_error("Vulkan: no memory for the frame's upload buffer");
    chunk.used = size;
    used_ += size;
    chunks_.push_back(chunk);
    current_ = chunks_.size() - 1;
    return HostSlice{chunk.buffer.buffer, 0, chunk.buffer.memory.mapped};
}

void HostArena::reset() {
    // A chunk is given back only after it has been unused for a good while (about ten seconds of frames). Allocating one is a vkAllocateMemory and a map, a stall of a few
    // milliseconds, and while chunks of the world stream in the big uploads come back every few frames: giving the chunk back after each one made that stall a regular one.
    constexpr std::uint32_t kKeepResets = 300;
    for (std::size_t i = 0; i < chunks_.size();) {
        Chunk& chunk = chunks_[i];
        chunk.idle = chunk.used == 0 ? chunk.idle + 1 : 0;
        if (chunk.idle > kKeepResets) {
            destroyBuffer(chunk.buffer);
            chunks_.erase(chunks_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            chunk.used = 0;
            ++i;
        }
    }
    current_ = 0;
    used_ = 0;
}

void HostArena::destroy() {
    for (Chunk& c : chunks_) destroyBuffer(c.buffer);
    chunks_.clear();
    current_ = 0;
    used_ = 0;
}

bool SamplerSetKey::operator==(const SamplerSetKey& other) const {
    return std::memcmp(views, other.views, sizeof(views)) == 0 && std::memcmp(samplers, other.samplers, sizeof(samplers)) == 0;
}

std::size_t SamplerSetKeyHash::operator()(const SamplerSetKey& key) const {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](const void* p) {
        h ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p));
        h *= 1099511628211ull;
    };
    for (int i = 0; i < kMaxSamplers; ++i) {
        mix(key.views[i]);
        mix(key.samplers[i]);
    }
    return static_cast<std::size_t>(h);
}

// ---- memory -------------------------------------------------------------------------------------------------------------------------------------------------

int findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags avoided) {
    const VkPhysicalDeviceMemoryProperties& m = g_core.memory;
    for (std::uint32_t i = 0; i < m.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 && (m.memoryTypes[i].propertyFlags & required) == required && (m.memoryTypes[i].propertyFlags & avoided) == 0) return static_cast<int>(i);
    }
    return -1;
}

namespace {
bool allocate(const VkMemoryRequirements& reqs, VkMemoryPropertyFlags required, VkMemoryPropertyFlags avoided, bool map, Memory& out) {
    int type = findMemoryType(reqs.memoryTypeBits, required, avoided);
    if (type < 0) type = findMemoryType(reqs.memoryTypeBits, required);  // (integrated devices have one kind of memory)
    if (type < 0) return false;
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = reqs.size;
    info.memoryTypeIndex = static_cast<std::uint32_t>(type);
    if (api::vkAllocateMemory(g_core.device, &info, nullptr, &out.memory) != VK_SUCCESS) return false;
    out.size = reqs.size;
    if (map && api::vkMapMemory(g_core.device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped) != VK_SUCCESS) {
        api::vkFreeMemory(g_core.device, out.memory, nullptr);
        out.memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}
}  // namespace

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, DeviceBuffer& out) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = std::max<VkDeviceSize>(size, 4);
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (api::vkCreateBuffer(g_core.device, &info, nullptr, &out.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements reqs{};
    api::vkGetBufferMemoryRequirements(g_core.device, out.buffer, &reqs);
    const bool ok = hostVisible ? allocate(reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, out.memory)
                                : allocate(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, out.memory);
    if (!ok || api::vkBindBufferMemory(g_core.device, out.buffer, out.memory.memory, 0) != VK_SUCCESS) {
        if (out.memory.memory != VK_NULL_HANDLE) api::vkFreeMemory(g_core.device, out.memory.memory, nullptr);
        api::vkDestroyBuffer(g_core.device, out.buffer, nullptr);
        out = DeviceBuffer{};
        return false;
    }
    out.size = info.size;
    return true;
}

void destroyBuffer(DeviceBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) api::vkDestroyBuffer(g_core.device, buffer.buffer, nullptr);
    if (buffer.memory.memory != VK_NULL_HANDLE) {
        if (buffer.memory.mapped != nullptr) api::vkUnmapMemory(g_core.device, buffer.memory.memory);
        api::vkFreeMemory(g_core.device, buffer.memory.memory, nullptr);
    }
    buffer = DeviceBuffer{};
}

bool createImage(const VkImageCreateInfo& info, DeviceImage& out) {
    if (api::vkCreateImage(g_core.device, &info, nullptr, &out.image) != VK_SUCCESS) return false;
    VkMemoryRequirements reqs{};
    api::vkGetImageMemoryRequirements(g_core.device, out.image, &reqs);
    if (!allocate(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, out.memory) || api::vkBindImageMemory(g_core.device, out.image, out.memory.memory, 0) != VK_SUCCESS) {
        if (out.memory.memory != VK_NULL_HANDLE) api::vkFreeMemory(g_core.device, out.memory.memory, nullptr);
        api::vkDestroyImage(g_core.device, out.image, nullptr);
        out = DeviceImage{};
        return false;
    }
    return true;
}

void destroyImage(DeviceImage& image) {
    if (image.image != VK_NULL_HANDLE) api::vkDestroyImage(g_core.device, image.image, nullptr);
    if (image.memory.memory != VK_NULL_HANDLE) api::vkFreeMemory(g_core.device, image.memory.memory, nullptr);
    image = DeviceImage{};
}

// ---- frames and commands ------------------------------------------------------------------------------------------------------------------------------------

void beginFrame() {
    Core& c = g_core;
    Frame& f = c.frames[c.current];
    if (f.inFlight) {
        check(api::vkWaitForFences(c.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        check(api::vkResetFences(c.device, 1, &f.fence), "vkResetFences");
        f.inFlight = false;
    }
    retire(f);
    check(api::vkResetCommandPool(c.device, f.pool, 0), "vkResetCommandPool");
    f.arena.reset();
    for (VkDescriptorPool pool : f.descriptorPools) api::vkResetDescriptorPool(c.device, pool, 0);
    f.descriptorPoolIndex = 0;
    f.uniformSets.clear();
    f.samplerSets.clear();
    f.queriesUsed = 0;
    beginRecording(f);
    api::vkCmdResetQueryPool(f.cmd, f.queryPool, 0, kQueriesPerFrame);
    c.frameBegun = true;
    ++c.frameNumber;
}

VkCommandBuffer cmd() { return frame().cmd; }

void submitFrame(VkSemaphore waitSemaphore, VkSemaphore signalSemaphore) {
    Core& c = g_core;
    Frame& f = frame();
    check(api::vkEndCommandBuffer(f.cmd), "vkEndCommandBuffer");
    f.recording = false;
    VkCommandBufferSubmitInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb.commandBuffer = f.cmd;
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = waitSemaphore;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = signalSemaphore;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cb;
    si.signalSemaphoreInfoCount = signalSemaphore != VK_NULL_HANDLE ? 1 : 0;
    si.pSignalSemaphoreInfos = &signal;
    check(api::vkQueueSubmit2(c.queue, 1, &si, f.fence), "vkQueueSubmit2");
    f.inFlight = true;
    f.serial = ++c.submitCount;
}

void advanceFrame() {
    Core& c = g_core;
    c.current = (c.current + 1) % kFramesInFlight;
    c.frameBegun = false;
}

void flushAndWait() {
    Core& c = g_core;
    Frame& f = frame();
    submitFrame(VK_NULL_HANDLE, VK_NULL_HANDLE);
    check(api::vkWaitForFences(c.device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    check(api::vkResetFences(c.device, 1, &f.fence), "vkResetFences");
    f.inFlight = false;
    retire(f);  // (everything recorded so far is done: timer results can be read and replaced objects destroyed)
    check(api::vkResetCommandPool(c.device, f.pool, 0), "vkResetCommandPool");
    beginRecording(f);
}

bool waitForSerial(std::uint64_t serial, std::uint64_t timeoutNs) {
    Core& c = g_core;
    if (serial == 0 || serial > c.submitCount) return true;
    for (Frame& f : c.frames) {
        if (f.serial != serial) continue;
        if (!f.inFlight) return true;
        const VkResult r = api::vkWaitForFences(c.device, 1, &f.fence, VK_TRUE, timeoutNs);
        if (r == VK_SUCCESS) return true;
        if (r == VK_TIMEOUT) return false;
        check(r, "vkWaitForFences");
    }
    return true;  // (the slot has been reused: it was waited for then)
}

void defer(std::function<void()> destroyer) { frame().garbage.push_back(std::move(destroyer)); }

VkDescriptorSet uniformSetFor(VkBuffer buffer) {
    Frame& f = frame();
    const auto it = f.uniformSets.find(buffer);
    if (it != f.uniformSets.end()) return it->second;
    const VkDescriptorSet set = allocateSet(f, g_core.uniformLayout);
    VkDescriptorBufferInfo bi{buffer, 0, kUniformRange};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w.pBufferInfo = &bi;
    api::vkUpdateDescriptorSets(g_core.device, 1, &w, 0, nullptr);
    f.uniformSets.emplace(buffer, set);
    return set;
}

VkDescriptorSet samplerSetFor(const SamplerSetKey& key, std::uint32_t usedMask) {
    Frame& f = frame();
    const auto it = f.samplerSets.find(key);
    if (it != f.samplerSets.end()) return it->second;
    const VkDescriptorSet set = allocateSet(f, g_core.samplerLayout);
    VkDescriptorImageInfo images[kMaxSamplers]{};
    VkWriteDescriptorSet writes[kMaxSamplers]{};
    std::uint32_t n = 0;
    for (int i = 0; i < kMaxSamplers; ++i) {
        if ((usedMask & (1u << i)) == 0) continue;
        images[n] = VkDescriptorImageInfo{key.samplers[i], key.views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstSet = set;
        writes[n].dstBinding = static_cast<std::uint32_t>(i);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[n].pImageInfo = &images[n];
        ++n;
    }
    if (n > 0) api::vkUpdateDescriptorSets(g_core.device, n, writes, 0, nullptr);
    f.samplerSets.emplace(key, set);
    return set;
}

VkSampler samplerFor(VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipmap, VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w, float maxLod) {
    std::uint32_t lodBits = 0;
    std::memcpy(&lodBits, &maxLod, sizeof lodBits);
    const std::uint64_t key = (static_cast<std::uint64_t>(minFilter) << 60) | (static_cast<std::uint64_t>(magFilter) << 56) | (static_cast<std::uint64_t>(mipmap) << 52) |
                              (static_cast<std::uint64_t>(u) << 44) | (static_cast<std::uint64_t>(v) << 36) | (static_cast<std::uint64_t>(w) << 28) | lodBits;
    auto& cache = samplerCache();
    const auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = magFilter;
    info.minFilter = minFilter;
    info.mipmapMode = mipmap;
    info.addressModeU = u;
    info.addressModeV = v;
    info.addressModeW = w;
    info.minLod = 0.0f;
    info.maxLod = maxLod;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    check(api::vkCreateSampler(g_core.device, &info, nullptr, &sampler), "vkCreateSampler");
    cache.emplace(key, sampler);
    return sampler;
}

// ---- creation and destruction -------------------------------------------------------------------------------------------------------------------------------

bool probeDevice(std::string& info) {
    std::string reason;
    if (!api::loadGlobal(reason)) {
        info = reason;
        return false;
    }
    Core& c = g_core;
    c = Core{};
    bool ok = false;
    if (!createInstance(false, reason)) {
        info = reason;
    } else {
        Choice choice;
        if (choose(c.instance, VK_NULL_HANDLE, choice, reason)) {
            info = std::string(choice.properties.deviceName) + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(choice.properties.apiVersion)) + "." +
                   std::to_string(VK_API_VERSION_MINOR(choice.properties.apiVersion)) + ")";
            ok = true;
        } else {
            info = reason;
        }
    }
    if (c.messenger != VK_NULL_HANDLE && api::vkDestroyDebugUtilsMessengerEXT != nullptr) api::vkDestroyDebugUtilsMessengerEXT(c.instance, c.messenger, nullptr);
    if (c.instance != VK_NULL_HANDLE && api::vkDestroyInstance != nullptr) api::vkDestroyInstance(c.instance, nullptr);
    c = Core{};
    api::unload();
    return ok;
}

bool createCore(GLFWwindow* window, bool presentable, std::string& reason) {
    Core& c = g_core;
    c = Core{};
    c.window = window;
    c.presentable = presentable && window != nullptr;
    if (!api::loadGlobal(reason)) return false;
    if (!createInstance(c.presentable, reason)) {
        destroyCore();
        return false;
    }
    if (c.presentable) {
        const VkResult r = glfwCreateWindowSurface(c.instance, window, nullptr, &c.surface);
        if (r != VK_SUCCESS) {
            reason = std::string("the window has no Vulkan surface (") + api::resultName(r) + ")";
            destroyCore();
            return false;
        }
    }
    Choice choice;
    if (!choose(c.instance, c.surface, choice, reason)) {
        destroyCore();
        return false;
    }
    c.physical = choice.device;
    c.queueFamily = choice.family;
    c.properties = choice.properties;
    c.multiDrawIndirect = choice.multiDrawIndirect;
    c.drawIndirectFirstInstance = choice.drawIndirectFirstInstance;
    c.depthFormat = choice.depthFormat;
    c.depthAspect = choice.depthAspect;
    api::vkGetPhysicalDeviceMemoryProperties(c.physical, &c.memory);
    c.deviceName = c.properties.deviceName;
    c.vendorName = vendorOf(c.properties.vendorID);
    c.versionText = std::to_string(VK_API_VERSION_MAJOR(c.properties.apiVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(c.properties.apiVersion)) + "." +
                    std::to_string(VK_API_VERSION_PATCH(c.properties.apiVersion));

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = c.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    f2.features.multiDrawIndirect = c.multiDrawIndirect ? VK_TRUE : VK_FALSE;
    f2.features.drawIndirectFirstInstance = c.drawIndirectFirstInstance ? VK_TRUE : VK_FALSE;
    std::vector<const char*> extensions;
    if (c.presentable) extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.pNext = &f2;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    di.ppEnabledExtensionNames = extensions.data();
    const VkResult made = api::vkCreateDevice(c.physical, &di, nullptr, &c.device);
    if (made != VK_SUCCESS) {
        reason = std::string("vkCreateDevice failed (") + api::resultName(made) + ")";
        c.device = VK_NULL_HANDLE;
        destroyCore();
        return false;
    }
    if (!api::loadDevice(c.device, c.presentable, reason)) {
        destroyCore();
        return false;
    }
    api::vkGetDeviceQueue(c.device, c.queueFamily, 0, &c.queue);
    try {
        createFixedObjects();
    } catch (const std::exception& e) {
        reason = e.what();
        destroyCore();
        return false;
    }
    return true;
}

void destroyCore() {
    Core& c = g_core;
    if (c.device != VK_NULL_HANDLE && api::vkDeviceWaitIdle != nullptr) api::vkDeviceWaitIdle(c.device);
    if (c.device != VK_NULL_HANDLE) {
        for (Frame& f : c.frames) {
            retire(f);
            f.arena.destroy();
            for (VkDescriptorPool p : f.descriptorPools) api::vkDestroyDescriptorPool(c.device, p, nullptr);
            f.descriptorPools.clear();
            if (f.queryPool != VK_NULL_HANDLE) api::vkDestroyQueryPool(c.device, f.queryPool, nullptr);
            if (f.acquired != VK_NULL_HANDLE) api::vkDestroySemaphore(c.device, f.acquired, nullptr);
            if (f.fence != VK_NULL_HANDLE) api::vkDestroyFence(c.device, f.fence, nullptr);
            if (f.pool != VK_NULL_HANDLE) api::vkDestroyCommandPool(c.device, f.pool, nullptr);
            f = Frame{};
        }
        for (auto& [key, sampler] : samplerCache()) api::vkDestroySampler(c.device, sampler, nullptr);
        samplerCache().clear();
        if (c.pipelineCache != VK_NULL_HANDLE) api::vkDestroyPipelineCache(c.device, c.pipelineCache, nullptr);
        if (c.pipelineLayout != VK_NULL_HANDLE) api::vkDestroyPipelineLayout(c.device, c.pipelineLayout, nullptr);
        if (c.samplerLayout != VK_NULL_HANDLE) api::vkDestroyDescriptorSetLayout(c.device, c.samplerLayout, nullptr);
        if (c.uniformLayout != VK_NULL_HANDLE) api::vkDestroyDescriptorSetLayout(c.device, c.uniformLayout, nullptr);
        api::vkDestroyDevice(c.device, nullptr);
        c.device = VK_NULL_HANDLE;
    }
    if (c.instance != VK_NULL_HANDLE && api::vkDestroyInstance != nullptr) {
        if (c.surface != VK_NULL_HANDLE && api::vkDestroySurfaceKHR != nullptr) api::vkDestroySurfaceKHR(c.instance, c.surface, nullptr);
        if (c.messenger != VK_NULL_HANDLE && api::vkDestroyDebugUtilsMessengerEXT != nullptr) api::vkDestroyDebugUtilsMessengerEXT(c.instance, c.messenger, nullptr);
        api::vkDestroyInstance(c.instance, nullptr);
    }
    const int errors = c.validationErrors;
    const bool validation = c.validation;
    c = Core{};
    c.validationErrors = errors;  // (read after stop())
    c.validation = validation;
    api::unload();
}

}  // namespace falcon::vulkan
