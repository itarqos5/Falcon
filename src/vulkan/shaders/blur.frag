#version 450
// The Vulkan twin of kBlurFragmentSource in src/gfx/panorama_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    vec2 uInSize;
    vec2 uBlurDir;
    float uRadius;
} u;
layout(set = 1, binding = 0) uniform sampler2D uInput;
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;
void main() {
    vec2 oneTexel = 1.0 / u.uInSize;
    vec2 sampleStep = oneTexel * u.uBlurDir;
    vec4 blurred = vec4(0.0);
    for (float a = -u.uRadius + 0.5; a <= u.uRadius; a += 2.0) {
        blurred += texture(uInput, texCoord + sampleStep * a);
    }
    blurred += texture(uInput, texCoord + sampleStep * u.uRadius) / 2.0;
    fragColor = blurred / (u.uRadius + 0.5);
}
