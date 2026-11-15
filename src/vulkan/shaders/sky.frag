#version 450
// The Vulkan twin of kSkyFragment in src/render/world_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    vec3 uForward;
    vec3 uRight;
    vec3 uUp;
    vec2 uTan;
    vec3 uHorizon;
    vec3 uZenith;
} u;
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 frag;
void main() {
    vec3 d = normalize(u.uForward + u.uRight * (vNdc.x * u.uTan.x) + u.uUp * (vNdc.y * u.uTan.y));
    float t = clamp(d.y, 0.0, 1.0);
    frag = vec4(mix(u.uHorizon, u.uZenith, pow(t, 0.55)), 1.0);
}
