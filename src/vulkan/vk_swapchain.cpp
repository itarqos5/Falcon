#include "vk_swapchain.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace falcon::vulkan {

namespace {

Backbuffer g_backbuffer;

struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkSemaphore> renderDone;  // one per image: signalled by the frame's submission, waited for by the present
    bool vsync = true;
    bool dirty = true;
};
Swapchain g_swap;

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: ") + what + " failed (" + api::resultName(result) + ")");
}

void framebufferSize(std::uint32_t& w, std::uint32_t& h) {
    int fw = 0, fh = 0;
    if (core().window != nullptr) glfwGetFramebufferSize(core().window, &fw, &fh);
    w = fw > 0 ? static_cast<std::uint32_t>(fw) : 0;
    h = fh > 0 ? static_cast<std::uint32_t>(fh) : 0;
}

VkImageView makeView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    check(api::vkCreateImageView(core().device, &info, nullptr, &view), "vkCreateImageView");
    return view;
}

void releaseBackbufferImages(Backbuffer& bb) {
    // The GPU may still be reading them: they go when the frame in progress is done.
    if (bb.color.image == VK_NULL_HANDLE && bb.depth.image == VK_NULL_HANDLE) return;
    Backbuffer old = bb;
    defer([old]() mutable {
        Core& c = core();
        if (old.colorView != VK_NULL_HANDLE) api::vkDestroyImageView(c.device, old.colorView, nullptr);
        if (old.depthView != VK_NULL_HANDLE) api::vkDestroyImageView(c.device, old.depthView, nullptr);
        destroyImage(old.color);
        destroyImage(old.depth);
    });
    bb.color = DeviceImage{};
    bb.depth = DeviceImage{};
    bb.colorView = VK_NULL_HANDLE;
    bb.depthView = VK_NULL_HANDLE;
}

void destroySwapchainObjects(bool destroyHandle) {
    Core& c = core();
    for (VkSemaphore s : g_swap.renderDone) api::vkDestroySemaphore(c.device, s, nullptr);
    g_swap.renderDone.clear();
    g_swap.images.clear();
    if (destroyHandle && g_swap.handle != VK_NULL_HANDLE) {
        api::vkDestroySwapchainKHR(c.device, g_swap.handle, nullptr);
        g_swap.handle = VK_NULL_HANDLE;
    }
}

// Makes (or remakes) the swapchain to fit the window. False when it cannot be made now (the window is minimised).
bool ensureSwapchain() {
    Core& c = core();
    std::uint32_t fw = 0, fh = 0;
    framebufferSize(fw, fh);
    if (fw == 0 || fh == 0) return false;
    if (g_swap.handle != VK_NULL_HANDLE && !g_swap.dirty && g_swap.extent.width == fw && g_swap.extent.height == fh) return true;

    api::vkDeviceWaitIdle(c.device);
    VkSurfaceCapabilitiesKHR caps{};
    check(api::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.physical, c.surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) throw std::runtime_error("Vulkan: the swapchain cannot be copied onto");

    std::uint32_t count = 0;
    api::vkGetPhysicalDeviceSurfaceFormatsKHR(c.physical, c.surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    api::vkGetPhysicalDeviceSurfaceFormatsKHR(c.physical, c.surface, &count, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {  // a plain UNORM format: the values of the picture go to the screen as they are
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) break;
        }
    }
    count = 0;
    api::vkGetPhysicalDeviceSurfacePresentModesKHR(c.physical, c.surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    api::vkGetPhysicalDeviceSurfacePresentModesKHR(c.physical, c.surface, &count, modes.data());
    const auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // (always there: waits for the display)
    if (!g_swap.vsync) mode = has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : (has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = std::clamp(fw, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(fh, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;
    std::uint32_t imageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount != 0) imageCount = std::min(imageCount, caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & alpha) == 0) {
        for (const VkCompositeAlphaFlagBitsKHR candidate : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if ((caps.supportedCompositeAlpha & candidate) != 0) {
                alpha = candidate;
                break;
            }
        }
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = c.surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = g_swap.handle;
    VkSwapchainKHR made = VK_NULL_HANDLE;
    const VkResult result = api::vkCreateSwapchainKHR(c.device, &info, nullptr, &made);
    destroySwapchainObjects(false);
    if (g_swap.handle != VK_NULL_HANDLE) api::vkDestroySwapchainKHR(c.device, g_swap.handle, nullptr);
    g_swap.handle = VK_NULL_HANDLE;
    check(result, "vkCreateSwapchainKHR");
    g_swap.handle = made;
    g_swap.format = chosen.format;
    g_swap.extent = extent;
    g_swap.dirty = false;
    count = 0;
    api::vkGetSwapchainImagesKHR(c.device, made, &count, nullptr);
    g_swap.images.resize(count);
    api::vkGetSwapchainImagesKHR(c.device, made, &count, g_swap.images.data());
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (std::uint32_t i = 0; i < count; ++i) {
        VkSemaphore s = VK_NULL_HANDLE;
        check(api::vkCreateSemaphore(c.device, &si, nullptr, &s), "vkCreateSemaphore");
        g_swap.renderDone.push_back(s);
    }
    return true;
}

}  // namespace

Backbuffer& backbuffer() { return g_backbuffer; }

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, std::uint32_t levels, std::uint32_t layers) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, levels, 0, layers};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    api::vkCmdPipelineBarrier2(cmd, &dep);
}

void fullBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 m{};
    m.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    m.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    m.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &m;
    api::vkCmdPipelineBarrier2(cmd, &dep);
}

void ensureBackbuffer() {
    Core& c = core();
    Backbuffer& bb = g_backbuffer;
    std::uint32_t w = 0, h = 0;
    framebufferSize(w, h);
    if (w == 0 || h == 0) {  // (a minimised window: the picture keeps its size)
        if (bb.color.image != VK_NULL_HANDLE) return;
        w = std::max<std::uint32_t>(w, 1);
        h = std::max<std::uint32_t>(h, 1);
    }
    if (bb.color.image != VK_NULL_HANDLE && bb.width == w && bb.height == h) return;
    releaseBackbufferImages(bb);
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!createImage(info, bb.color)) throw std::runtime_error("Vulkan: no memory for the window's picture");
    info.format = c.depthFormat;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!createImage(info, bb.depth)) throw std::runtime_error("Vulkan: no memory for the window's depth buffer");
    bb.colorView = makeView(bb.color.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    bb.depthView = makeView(bb.depth.image, c.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    bb.width = w;
    bb.height = h;
    bb.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bb.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bb.colorValid = false;
    bb.depthValid = false;
}

void destroyBackbuffer() {
    Core& c = core();
    Backbuffer& bb = g_backbuffer;
    if (c.device != VK_NULL_HANDLE) {
        if (bb.colorView != VK_NULL_HANDLE) api::vkDestroyImageView(c.device, bb.colorView, nullptr);
        if (bb.depthView != VK_NULL_HANDLE) api::vkDestroyImageView(c.device, bb.depthView, nullptr);
        destroyImage(bb.color);
        destroyImage(bb.depth);
    }
    bb = Backbuffer{};
}

void presentBackbuffer() {
    Core& c = core();
    if (!c.frameBegun) return;  // (nothing was drawn this frame)
    Backbuffer& bb = g_backbuffer;
    Frame& f = frame();
    const bool wantsPresent = c.presentable && c.surface != VK_NULL_HANDLE;
    bool shown = false;
    std::uint32_t index = 0;
    if (wantsPresent && bb.color.image != VK_NULL_HANDLE) {
        for (int attempt = 0; attempt < 2 && !shown; ++attempt) {
            if (!ensureSwapchain()) break;
            const VkResult r = api::vkAcquireNextImageKHR(c.device, g_swap.handle, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &index);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
                shown = true;
                if (r == VK_SUBOPTIMAL_KHR) g_swap.dirty = true;
            } else if (r == VK_ERROR_OUT_OF_DATE_KHR) {
                g_swap.dirty = true;
            } else {
                check(r, "vkAcquireNextImageKHR");
            }
        }
    }
    if (!shown) {
        submitFrame(VK_NULL_HANDLE, VK_NULL_HANDLE);
        advanceFrame();
        return;
    }

    VkCommandBuffer cb = f.cmd;
    transitionImage(cb, bb.color.image, VK_IMAGE_ASPECT_COLOR_BIT, bb.colorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    bb.colorLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImage target = g_swap.images[index];
    transitionImage(cb, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(bb.width), static_cast<std::int32_t>(bb.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    // The picture's first row is its bottom: the copy turns it over.
    blit.dstOffsets[0] = {0, static_cast<std::int32_t>(g_swap.extent.height), 0};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(g_swap.extent.width), 0, 1};
    const bool sameSize = bb.width == g_swap.extent.width && bb.height == g_swap.extent.height;
    api::vkCmdBlitImage(cb, bb.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    transitionImage(cb, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    submitFrame(f.acquired, g_swap.renderDone[index]);
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_swap.renderDone[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_swap.handle;
    pi.pImageIndices = &index;
    const VkResult r = api::vkQueuePresentKHR(c.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) g_swap.dirty = true;
    else if (r != VK_SUCCESS) check(r, "vkQueuePresentKHR");
    advanceFrame();
}

void setSwapchainVsync(bool vsync) {
    if (g_swap.vsync == vsync) return;
    g_swap.vsync = vsync;
    g_swap.dirty = true;
}

void markSwapchainDirty() { g_swap.dirty = true; }

void destroySwapchain() {
    Core& c = core();
    if (c.device == VK_NULL_HANDLE) return;
    api::vkDeviceWaitIdle(c.device);
    destroySwapchainObjects(true);
    g_swap.dirty = true;
}

}  // namespace falcon::vulkan
