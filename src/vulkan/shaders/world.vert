#version 450
// The Vulkan twin of kWorldVertexBody in src/render/world_renderer.cpp (the terrain). Every uniform of the program is in the block; the fragment shader declares the same.
layout(location = 0) in vec4 aPos;    // x, y, z inside the section (1/1024 block), and the texture layer
layout(location = 1) in vec2 aUv;     // 1/1024 of a texture repeat
layout(location = 2) in vec4 aColor;  // tint colour, and the shade of the face's direction
layout(location = 3) in vec4 aLight;  // sky light, block light, the face's direction (1 + the Face number; 0: not known)
layout(location = 4) in vec3 aSectionOffset;  // when drawing many sections in one call: where the section's corner is (one per command)
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uOffset;
    float uInstanced;
    vec3 uFogColor;
    vec2 uFogRange;
    vec2 uEnvFog;
    float uAlphaCut;
    float uGamma;
    float uFade;
    vec3 uHeldOrigin;
    float uHeldOn;
} u;
layout(location = 0) out vec2 vUv;
layout(location = 1) flat out float vLayer;
layout(location = 2) flat out float vFace;
layout(location = 3) out vec3 vTint;
layout(location = 4) out vec2 vLight;
layout(location = 5) out vec3 vPos;
layout(location = 6) out float vDist;
void main() {
    vFace = floor(aLight.z * 255.0 + 0.5);
    vec3 p = aPos.xyz * (1.0 / 1024.0) + (u.uInstanced > 0.5 ? aSectionOffset : u.uOffset);
    gl_Position = u.uViewProj * vec4(p, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vUv = aUv * (1.0 / 1024.0);
    vLayer = aPos.w;
    vTint = aColor.rgb * aColor.a;
    vLight = aLight.xy;
    vPos = p;
    vDist = length(p);
}
