#version 450
// The Vulkan twin of kVertexShader in src/render/lod_renderer.cpp (the levels of detail beyond the loaded chunks). Every uniform of the program is in the block; the fragment shader
// declares the same.
layout(location = 0) in vec4 aPos;     // x and z in cells, y in blocks, inside the tile
layout(location = 1) in vec4 aColor;   // the colour of the face
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uOffset;
    float uCell;
    vec3 uFogColor;
    vec2 uFogRange;
    vec2 uEnvFog;
} u;
layout(location = 0) out vec3 vColor;
layout(location = 1) out float vDist;
void main() {
    vec3 p = vec3(aPos.x * u.uCell, aPos.y, aPos.z * u.uCell) + u.uOffset;
    gl_Position = u.uViewProj * vec4(p, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vColor = aColor.rgb;
    vDist = length(p);
}
