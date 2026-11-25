#version 450
// The Vulkan twin of kWorldFragmentBody in src/render/world_renderer.cpp. The two #define variants of the GLSL 330 shader are specialization constants here: the world renderer
// chooses them with "#define LIGHT_LUT" and "#define FLAT_NORMAL" lines, which the shader registry turns into the values (constant_id 0 and 1).
layout(constant_id = 0) const bool LIGHT_LUT = true;
layout(constant_id = 1) const bool FLAT_NORMAL = true;
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in float vLayer;
layout(location = 2) flat in float vFace;
layout(location = 3) in vec3 vTint;
layout(location = 4) in vec2 vLight;
layout(location = 5) in vec3 vPos;
layout(location = 6) in float vDist;
layout(std140, set = 0, binding = 0) uniform Block {
    mat4 uViewProj;
    vec3 uOffset;
    float uInstanced;
    vec3 uFogColor;
    vec2 uFogRange;
    vec2 uEnvFog;
    float uAlphaCut;
    float uGamma;
    float uFade;
    vec3 uHeldOrigin;
    float uHeldOn;
} u;
layout(set = 1, binding = 0) uniform sampler2DArray uTex;
layout(set = 1, binding = 1) uniform sampler3D uHeldLight;
layout(set = 1, binding = 2) uniform sampler2D uLightmap;
layout(location = 0) out vec4 frag;

vec3 lightColor(float sky, float blk) {
    if (LIGHT_LUT) {
        return texture(uLightmap, vec2(sky, blk) * (15.0 / 16.0) + vec2(0.5 / 16.0)).rgb;
    }
    float block_brightness = (blk / (4.0 - 3.0 * blk)) * 1.4;
    float sky_brightness = sky / (4.0 - 3.0 * sky);
    float parabolic = (2.0 * blk - 1.0) * (2.0 * blk - 1.0);
    vec3 color = vec3(sky_brightness) + mix(vec3(1.0, 0.84705883, 0.54901963), vec3(1.0), 0.9 * parabolic) * block_brightness;
    color = clamp(color, 0.0, 1.0);
    float m = max(max(color.r, color.g), color.b);
    if (m > 0.0) {
        float inverted = 1.0 - m;
        vec3 notGamma = color * ((1.0 - inverted * inverted * inverted * inverted) / m);
        color = mix(color, notGamma, u.uGamma);
    }
    return color;
}

// East, west, up, down, south, north (the mesher's Face order): the even ones point the positive way.
vec3 faceNormal(int f) {
    float s = (f & 1) == 0 ? 1.0 : -1.0;
    return f < 2 ? vec3(s, 0.0, 0.0) : (f < 4 ? vec3(0.0, s, 0.0) : vec3(0.0, 0.0, s));
}

void main() {
    vec4 c = texture(uTex, vec3(vUv, vLayer));
    if (c.a < u.uAlphaCut) discard;
    float blockLight = vLight.y;
    if (u.uHeldOn > 0.5) {
        vec3 n;
        if (FLAT_NORMAL) {
            n = vFace > 0.5 ? faceNormal(int(vFace + 0.5) - 1) : normalize(cross(dFdx(vPos), dFdy(vPos)));
        } else {
            n = normalize(cross(dFdx(vPos), dFdy(vPos)));
        }
        if (dot(n, vPos) > 0.0) n = -n;
        blockLight = max(blockLight, texture(uHeldLight, (vPos + n * 0.5 - u.uHeldOrigin) * (1.0 / 32.0)).r);
    }
    float fog = max(clamp((vDist - u.uFogRange.x) / (u.uFogRange.y - u.uFogRange.x), 0.0, 1.0), clamp((vDist - u.uEnvFog.x) / (u.uEnvFog.y - u.uEnvFog.x), 0.0, 1.0));
    frag = vec4(mix(c.rgb * (vTint * lightColor(vLight.x, blockLight)), u.uFogColor, fog), c.a * u.uFade);
}
