#version 450
// The Vulkan twin of kLineVertex in src/render/world_renderer.cpp (the outline of the block the player looks at).
layout(location = 0) in vec3 aPos;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
} u;
void main() {
    gl_Position = u.uViewProj * vec4(aPos, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
