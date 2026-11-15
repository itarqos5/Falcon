#version 450
// The Vulkan twin of kVertex in src/render/entity_draw.hpp (mobs and the bodies of other players).
layout(location = 0) in vec3 aPos;   // relative to the camera
layout(location = 1) in vec2 aUv;    // 0..1 on the 64x64 skin
layout(location = 2) in float aShade;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProj;
    vec3 uLight;
    vec3 uTint;
    float uHurt;
} u;
layout(location = 0) out vec2 vUv;
layout(location = 1) out float vShade;
void main() {
    gl_Position = u.uProj * vec4(aPos, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vUv = aUv;
    vShade = aShade;
}
