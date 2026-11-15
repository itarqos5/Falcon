#version 450
// The Vulkan twin of kBlurVertexSource in src/gfx/panorama_renderer.cpp: one triangle that covers the target.
layout(std140, set = 0, binding = 0) uniform Block {
    vec2 uInSize;
    vec2 uBlurDir;
    float uRadius;
} u;
layout(location = 0) out vec2 texCoord;
void main() {
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    texCoord = uv;
}
