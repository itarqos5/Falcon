#version 450
// The Vulkan twin of the fragment shader in src/gfx/gui_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProjection;
    float uCutoff;
} u;
layout(set = 1, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 fragColor;
void main() {
    vec4 color = texture(uTexture, vUV) * vColor;
    if (color.a == 0.0 || color.a < u.uCutoff) {
        discard;
    }
    fragColor = color;
}
