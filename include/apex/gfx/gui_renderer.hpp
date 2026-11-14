#pragma once

#include "apex/gfx/shader.hpp"
#include "apex/gfx/texture.hpp"
#include "apex/util/math.hpp"

#include <cstdint>
#include <vector>

namespace apex {

// Batched 2D quad renderer working in GUI units (framebuffer pixels / gui scale).
// Colours are 0xAARRGGBB like Minecraft's ARGB ints and modulate the texture.
class GuiRenderer {
public:
    GuiRenderer();
    ~GuiRenderer();
    GuiRenderer(const GuiRenderer&) = delete;
    GuiRenderer& operator=(const GuiRenderer&) = delete;

    // Starts a frame: sets viewport/blend state and the GUI projection.
    void begin(int framebufferWidth, int framebufferHeight, int guiScale);
    // The current GUI scale: how many framebuffer pixels one GUI unit spans.
    [[nodiscard]] int guiScale() const { return guiScale_; }
    // Re-applies the GUI GL state after something else (e.g. the panorama) changed it.
    void restoreState();
    void flush();
    void end();

    // Transform stack applied to every vertex (translate/rotate/scale for the splash text).
    void pushPose();
    void popPose();
    [[nodiscard]] Pose2D& pose() { return pose_; }

    // Text mode mimics Minecraft's text shader, which drops fragments with alpha < 0.1.
    void setTextMode(bool enabled);

    // Clips drawing to a rectangle given in GUI units (top-left origin). Flushes pending quads.
    void enableScissor(int x0, int y0, int x1, int y1);
    void disableScissor();

    void quad(const Texture2D& texture, float x0, float y0, float x1, float y1, float u0, float v0,
              float u1, float v1, std::uint32_t argb);
    void fill(float x0, float y0, float x1, float y1, std::uint32_t argb);

private:
    struct Vertex {
        float x, y, u, v;
        std::uint8_t r, g, b, a;
    };

    void pushVertex(float x, float y, float u, float v, std::uint32_t argb);

    ShaderProgram shader_;
    Texture2D white_;
    gl::GLuint vao_ = 0;
    gl::GLuint vbo_ = 0;
    gl::GLint projLoc_ = -1;
    gl::GLint texLoc_ = -1;
    gl::GLint cutoffLoc_ = -1;

    std::vector<Vertex> vertices_;
    gl::GLuint currentTexture_ = 0;
    bool textMode_ = false;

    Mat4 projection_;
    Pose2D pose_;
    std::vector<Pose2D> poseStack_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    int guiScale_ = 1;
};

}  // namespace apex
