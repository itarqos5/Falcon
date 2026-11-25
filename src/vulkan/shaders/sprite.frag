#version 450
// The Vulkan twin of kFragmentShader in src/render/sprite_renderer.cpp.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vDist;
layout(location = 3) flat in float vLayer;
layout(location = 4) flat in float vFromBlocks;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uFogColor;
    float uFullbright;
    vec2 uFogRange;
} u;
layout(set = 1, binding = 0) uniform sampler2D uPicture;
layout(set = 1, binding = 1) uniform sampler2DArray uPictures;
layout(set = 1, binding = 2) uniform sampler2D uLightmap;
layout(location = 0) out vec4 frag;
void main() {
    vec4 t = vFromBlocks > 0.5 ? texture(uPictures, vec3(vUv, vLayer)) : texture(uPicture, vUv);
    float alpha = t.a * vColor.a;
    if (alpha < 0.02) discard;
    vec3 col = t.rgb * vColor.rgb;
    if (u.uFullbright < 0.5) col *= texture(uLightmap, vec2(15.5 / 16.0, 0.5 / 16.0)).rgb;
    float fog = clamp((vDist - u.uFogRange.x) / max(1.0, u.uFogRange.y - u.uFogRange.x), 0.0, 1.0);
    frag = vec4(mix(col, u.uFogColor, fog), alpha);
}
