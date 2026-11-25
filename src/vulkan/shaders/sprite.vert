#version 450
// The Vulkan twin of kVertexShader in src/render/sprite_renderer.cpp (the clouds, the rain, a broken block's bits and the shadow under a mob). Every uniform of the program is
// in the block; the fragment shader declares the same.
layout(location = 0) in vec3 aPos;     // the corner, in the world, relative to the camera
layout(location = 1) in vec2 aUv;      // where it is on the picture
layout(location = 2) in vec4 aColor;   // rgb: the tint; a: how opaque the quad is
layout(location = 3) in vec2 aInfo;    // x: the layer of the block pictures; y: 1 when the block pictures are used, 0 for the plain picture
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uFogColor;
    float uFullbright;
    vec2 uFogRange;
} u;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vDist;
layout(location = 3) flat out float vLayer;
layout(location = 4) flat out float vFromBlocks;
void main() {
    gl_Position = u.uViewProj * vec4(aPos, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vUv = aUv;
    vColor = aColor;
    vDist = length(aPos);
    vLayer = aInfo.x;
    vFromBlocks = aInfo.y;
}
