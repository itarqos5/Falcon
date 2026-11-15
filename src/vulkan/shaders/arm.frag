#version 450
// The Vulkan twin of kArmFragment in src/render/hand_renderer.cpp.
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProj;
    vec3 uLight;
} u;
layout(set = 1, binding = 0) uniform sampler2D uSkin;
layout(location = 0) out vec4 frag;
void main() {
    vec4 c = texture(uSkin, vUv);
    if (c.a < 0.1) discard;
    frag = vec4(c.rgb * vShade * u.uLight, 1.0);
}
