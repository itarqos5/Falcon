#pragma once

// The game draws into "the window's framebuffer". Here that is a picture of our own (the backbuffer, with a depth buffer), kept between frames; a frame ends by copying it onto
// a swapchain image. So a window that is never shown (captures, --background) needs no swapchain at all, and reading the frame back (screenshots) reads that picture.
//
// Rows are stored the way OpenGL stores them (the first row is the bottom of the picture): every target is drawn with an ordinary viewport, so a texture that was drawn into
// is sampled exactly as it is in OpenGL; only the copy onto the swapchain turns the picture over.

#include "vk_core.hpp"

namespace falcon::vulkan {

// A clear that has been asked for and not yet made (it becomes the load operation of the target's next pass).
struct ClearRequest {
    bool color = false;
    bool depth = false;
    float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct Backbuffer {
    DeviceImage color;
    DeviceImage depth;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool colorValid = false;  // has been drawn on since it was made (otherwise its first pass clears it)
    bool depthValid = false;
    ClearRequest clear;
};

[[nodiscard]] Backbuffer& backbuffer();

// Makes the backbuffer the size of the window's framebuffer (when that changed, or at the first use). Replaced pictures are destroyed when the GPU is done with them.
void ensureBackbuffer();
void destroyBackbuffer();

// An image barrier on every level and layer of an image.
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, std::uint32_t levels = 1, std::uint32_t layers = 1);
// Everything before this finishes before anything after it starts, and its writes are visible.
void fullBarrier(VkCommandBuffer cmd);

// The end of a frame: the backbuffer is copied onto the next swapchain image and shown (when the window is presentable), the frame's commands are submitted and the next
// frame slot is taken.
void presentBackbuffer();

void setSwapchainVsync(bool vsync);
void markSwapchainDirty();
void destroySwapchain();

}  // namespace falcon::vulkan
