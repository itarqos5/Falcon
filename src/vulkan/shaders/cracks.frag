#version 450
// The Vulkan twin of kCrackFragment in src/render/world_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
} u;
layout(set = 1, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUv);
    if (c.a < 0.01) discard;
    frag = c;
}
