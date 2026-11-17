#version 450
// The Vulkan twin of kVertexShader in src/render/lod_renderer.cpp (the levels of detail beyond the loaded chunks). Every uniform of the program is in the block; the fragment shader
// declares the same.
layout(location = 0) in vec4 aPos;     // x and z in cells, y in blocks, inside the tile; w: the layer of the picture
layout(location = 1) in vec4 aColor;   // rgb: the tint with the shade of the face in it; a: the face (0 top, 1 north, 2 south, 3 west, 4 east) and, from bit 3, the corner of its quad
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uOffset;
    float uCell;
    vec3 uFogColor;
    vec2 uFogRange;
    vec2 uEnvFog;
    vec2 uHoleOrigin;
    float uHoleRadius2;
} u;
layout(location = 0) out vec3 vColor;
layout(location = 1) out float vDist;
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out float vLayer;
layout(location = 4) flat out float vFlat;
layout(location = 5) out vec2 vXZ;
void main() {
    vec3 block = vec3(aPos.x * u.uCell, aPos.y, aPos.z * u.uCell);
    vec3 p = block + u.uOffset;
    float dist = length(p);
    int bits = int(aColor.a * 255.0 + 0.5);
    int face = bits & 7;
    int corner = (bits >> 3) & 3;
    vFlat = float((bits >> 5) & 1);
    vXZ = p.xz;
    float grow = min(0.04, 0.005 + 0.00002 * dist);
    float alongX = corner >= 2 ? 1.0 : -1.0;
    float alongZ = (corner == 1 || corner == 2) ? 1.0 : -1.0;
    float down = (corner == 0 || corner == 3) ? -1.0 : 0.0;
    vec3 grown = vec3(0.0);
    if (face == 0) grown = vec3(alongX, 0.0, alongZ) * grow;
    else if (face < 3) grown = vec3(alongX * grow, down * grow, 0.0);
    else grown = vec3(0.0, down * grow, alongX * grow);
    gl_Position = u.uViewProj * vec4(p + grown, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    float ao = (face != 0 && (corner == 0 || corner == 3)) ? 0.82 : 1.0;
    vColor = aColor.rgb * ao;
    vDist = dist;
    vUv = face == 0 ? block.xz : (face < 3 ? vec2(block.x, -block.y) : vec2(block.z, -block.y));
    vLayer = aPos.w;
}
