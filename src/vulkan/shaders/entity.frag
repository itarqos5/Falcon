#version 450
// The Vulkan twin of kFragment in src/render/entity_draw.hpp.
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProj;
    vec3 uLight;
    vec3 uTint;
    float uHurt;
} u;
layout(set = 1, binding = 0) uniform sampler2D uSkin;
layout(location = 0) out vec4 frag;
void main() {
    vec4 c = texture(uSkin, vUv);
    if (c.a < 0.1) discard;
    vec3 lit = c.rgb * u.uTint * vShade;
    lit = mix(vec3(1.0, 0.0, 0.0), lit, 1.0 - 0.3 * u.uHurt);
    frag = vec4(lit * u.uLight, 1.0);
}
