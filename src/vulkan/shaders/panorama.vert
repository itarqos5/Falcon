#version 450
// The Vulkan twin of kVertexSource in src/gfx/panorama_renderer.cpp (the cube of the title screen's background).
layout(location = 0) in vec3 aPos;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uProjection;
    mat4 uView;
} u;
layout(location = 0) out vec3 vDirection;
void main() {
    gl_Position = u.uProjection * u.uView * vec4(aPos, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vDirection = aPos;
}
