#version 450
// The Vulkan twin of kCrackVertex in src/render/world_renderer.cpp (the cracks of a block that is being dug).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
} u;
layout(location = 0) out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = u.uViewProj * vec4(aPos, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
