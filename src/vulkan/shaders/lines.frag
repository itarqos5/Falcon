#version 450
// The Vulkan twin of kLineFragment in src/render/world_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
} u;
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 frag;
void main() { frag = vColor; }
