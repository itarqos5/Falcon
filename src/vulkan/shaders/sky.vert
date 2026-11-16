#version 450
// The Vulkan twin of kSkyVertex in src/render/world_renderer.cpp: one triangle that covers the screen, from the vertex number alone.
layout(std140, set = 0, binding = 0) uniform Block {
    vec3 uForward;
    vec3 uRight;
    vec3 uUp;
    vec2 uTan;
    vec3 uHorizon;
    vec3 uZenith;
    vec3 uSunDir;
    vec2 uAngle;
    float uStars;
    vec4 uGlow;
    float uMoon;
} u;
layout(location = 0) out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
