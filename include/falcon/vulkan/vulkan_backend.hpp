#pragma once

#include <string>

struct GLFWwindow;

// The Vulkan renderer (src/vulkan/).
//
// The game draws through the functions of falcon/gfx/gl.hpp, which are pointers: with OpenGL they point into the graphics driver, with Vulkan they point at an
// implementation of the same calls on Vulkan (src/vulkan/gl_*.cpp: buffers, textures, programs, vertex arrays, framebuffers, the draws and the state that goes with them,
// for exactly the part of OpenGL 3.3 and 4.3 the renderers use). Nothing above that layer knows which one it is talking to. See docs/RENDERING.md.
//
// Nothing here needs the Vulkan SDK at run time: the loader (vulkan-1.dll, libvulkan.so.1) is found when the game starts, and a PC without one simply uses OpenGL.

namespace falcon::vulkan {

// Whether this build contains the Vulkan renderer at all (-DFALCON_VULKAN=OFF leaves it out).
[[nodiscard]] bool compiledIn();

// Whether Vulkan can draw the game on this PC: the loader is there, and a device of version 1.3 with the features the renderer needs (dynamic rendering, synchronization2,
// the vertex formats the meshes use). Looks once and remembers; needs glfwInit() to have been called. `reason` (may be null) says why not, or names the device.
[[nodiscard]] bool usable(std::string* reason = nullptr);

// Builds the instance, the device and the frame resources for a window that was made with GLFW_NO_API, and puts the Vulkan implementation into gl::. `presentable` false: the
// window is never shown (captures, --background), so there is no swapchain and the frames are only drawn. False, with `reason`, leaves nothing behind (the caller makes an
// OpenGL window instead).
[[nodiscard]] bool start(GLFWwindow* window, bool presentable, bool vsync, std::string& reason);

// Waits for the device and destroys everything. gl:: is not usable afterwards.
void stop();

[[nodiscard]] bool active();

// The end of a frame (what swapping the buffers is to OpenGL): the frame is submitted and shown.
void present();

void setVsync(bool vsync);

// The window's size or mode changed (resize, fullscreen): the swapchain and the frame's pictures are rebuilt before the next frame.
void resized();

// The name of the device in use ("AMD Radeon RX 580 2048SP"), and the Vulkan version it runs ("1.3.260").
[[nodiscard]] const std::string& deviceName();
[[nodiscard]] const std::string& apiVersionText();

// The messages the validation layer has reported since start (FALCON_VK_VALIDATION=1 turns the layer on when it is installed).
[[nodiscard]] int validationErrors();

}  // namespace falcon::vulkan
