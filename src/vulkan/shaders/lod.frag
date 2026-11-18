#version 450
// The Vulkan twin of kFragmentShader in src/render/lod_renderer.cpp.
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vDist;
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in float vLayer;
layout(location = 4) flat in float vFlat;
layout(location = 5) in vec2 vXZ;
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
layout(set = 1, binding = 0) uniform sampler2DArray uTex;
layout(set = 1, binding = 2) uniform sampler2D uLightmap;
layout(location = 0) out vec4 frag;
void main() {
    vec2 inChunk = (vXZ - u.uHoleOrigin) * (1.0 / 16.0);
    vec2 chunk = floor(inChunk);
    bool underChunks = dot(chunk, chunk) <= u.uHoleRadius2;
    bool atTheEdge = false;
    if (underChunks) {
        vec2 edge = (inChunk - chunk) * 16.0;
        float margin = 1.5 * max(fwidth(vXZ.x), fwidth(vXZ.y));
        if ((edge.x < margin && dot(chunk - vec2(1.0, 0.0), chunk - vec2(1.0, 0.0)) > u.uHoleRadius2) || (edge.x > 16.0 - margin && dot(chunk + vec2(1.0, 0.0), chunk + vec2(1.0, 0.0)) > u.uHoleRadius2) ||
            (edge.y < margin && dot(chunk - vec2(0.0, 1.0), chunk - vec2(0.0, 1.0)) > u.uHoleRadius2) || (edge.y > 16.0 - margin && dot(chunk + vec2(0.0, 1.0), chunk + vec2(0.0, 1.0)) > u.uHoleRadius2)) {
            underChunks = false;
            atTheEdge = true;
        }
    }
    if (underChunks && vFlat < 0.5) discard;
    vec4 t = texture(uTex, vec3(vUv, vLayer), mix(-1.0, 8.0, vFlat));
    vec3 rgb = t.rgb;
    if (t.a < 0.25) rgb *= 0.45;
    vec3 light = texture(uLightmap, vec2(1.0, 0.0) * (15.0 / 16.0) + vec2(0.5 / 16.0)).rgb;
    float d = max(0.0, vDist - u.uFogRange.x);
    float span = max(1.0, u.uFogRange.y - u.uFogRange.x);
    float fogNorm = clamp(d / span, 0.0, 1.0);
    float fog = 1.0 - exp(-fogNorm * fogNorm * 3.5);
    float env = clamp((vDist - u.uEnvFog.x) / max(1.0, u.uEnvFog.y - u.uEnvFog.x), 0.0, 1.0);
    fog = max(fog, env);
    vec3 col = rgb * vColor;
    if (t.a < 0.85 && t.a > 0.25) {
        float fresnel = pow(clamp(vDist / max(100.0, u.uFogRange.y), 0.0, 1.0), 3.0) * 0.35;
        col = mix(col, u.uFogColor, fresnel);
    }
    vec3 lit = col * light;
    if (underChunks) lit = vec3(0.065);
    else if (atTheEdge && vFlat > 0.5) lit = mix(vec3(0.065), lit, 0.5);
    frag = vec4(mix(lit, u.uFogColor, fog), 1.0);
}
