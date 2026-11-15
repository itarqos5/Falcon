#version 450
// The Vulkan twin of the vertex shader in src/gfx/gui_renderer.cpp. The block at set 0 is every uniform of the program, with the names the GLSL 330 shader gives them;
// the same block is in the fragment shader. A clip-space depth of OpenGL (-w..w) is turned into Vulkan's (0..w) at the end of every vertex shader.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProjection;
    float uCutoff;
} u;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
void main() {
    gl_Position = u.uProjection * vec4(aPos, 0.0, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vUV = aUV;
    vColor = aColor;
}
