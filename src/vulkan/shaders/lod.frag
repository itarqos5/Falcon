#version 450
// The Vulkan twin of kFragmentShader in src/render/lod_renderer.cpp.
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vDist;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uOffset;
    float uCell;
    vec3 uFogColor;
    vec2 uFogRange;
    vec2 uEnvFog;
} u;
layout(location = 0) out vec4 frag;
void main() {
    float fog = max(clamp((vDist - u.uFogRange.x) / (u.uFogRange.y - u.uFogRange.x), 0.0, 1.0), clamp((vDist - u.uEnvFog.x) / (u.uEnvFog.y - u.uEnvFog.x), 0.0, 1.0));
    frag = vec4(mix(vColor, u.uFogColor, fog), 1.0);
}
