#version 450
// The Vulkan twin of kFragmentSource in src/gfx/panorama_renderer.cpp.
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProjection;
    mat4 uView;
} u;
layout(set = 1, binding = 0) uniform samplerCube uSky;
layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = texture(uSky, vDirection);
}
