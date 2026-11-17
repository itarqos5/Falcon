#version 450
// The Vulkan twin of kSkyFragment in src/render/world_renderer.cpp.
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
layout(set = 1, binding = 0) uniform sampler2D uCelestial;
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 frag;
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
void main() {
    vec3 d = normalize(u.uForward + u.uRight * (vNdc.x * u.uTan.x) + u.uUp * (vNdc.y * u.uTan.y));
    float t = clamp(d.y, 0.0, 1.0);
    vec3 color = mix(u.uHorizon, u.uZenith, pow(t, 0.55));
    // The stars turn with the sun round the east-west line.
    if (u.uStars > 0.0 && d.y > -0.02) {
        vec3 dr = vec3(d.x * u.uAngle.y + d.y * u.uAngle.x, -d.x * u.uAngle.x + d.y * u.uAngle.y, d.z);
        vec3 cell = floor(dr * 160.0);
        float h = hash13(cell);
        if (h > 0.9955) color += vec3(clamp(u.uStars * 2.0 * (0.7 + 0.3 * fract(h * 531.0)), 0.0, 1.0));
    }
    float sunLength = length(u.uSunDir);
    if (sunLength > 0.5) {  // (no sun or moon where there is no day: under water, in the Nether)
        // The glow of the sunrise or the sunset: a disc 120 blocks across seen 100 blocks off, standing on the horizon towards the sun, fading out from its middle.
        vec3 hs = vec3(u.uSunDir.x, 0.0, u.uSunDir.z);
        float hl = length(hs);
        if (u.uGlow.a > 0.0 && hl > 1.0e-4) {
            vec3 s = hs / hl;
            float ds = dot(d, s);
            if (ds > 0.05) {
                vec3 p = cross(vec3(0.0, 1.0, 0.0), s);
                vec2 q = vec2(dot(d, p), d.y) * (100.0 / ds);
                float k = clamp(1.0 - length(q) / 120.0, 0.0, 1.0);
                color = mix(color, u.uGlow.rgb, u.uGlow.a * k);
            }
        }
        // The sun and the moon are the game's own pictures on squares in the sky (see the OpenGL shader).
        vec3 across = vec3(0.0, 0.0, 1.0);
        float toSun = dot(d, u.uSunDir);
        if (toSun > 0.0) {
            vec2 q = vec2(dot(d, across), dot(d, cross(across, u.uSunDir))) * (100.0 / toSun) / 30.0;
            if (abs(q.x) < 1.0 && abs(q.y) < 1.0) {
                vec4 t = textureLod(uCelestial, vec2(q.x * 0.5 + 0.5, (q.y * 0.5 + 0.5) / 9.0), 0.0);
                color += t.rgb * t.a;
            }
        }
        float toMoon = -toSun;
        if (toMoon > 0.0) {
            vec2 q = vec2(dot(d, across), dot(d, cross(across, -u.uSunDir))) * (100.0 / toMoon) / 20.0;
            if (abs(q.x) < 1.0 && abs(q.y) < 1.0) {
                vec4 t = textureLod(uCelestial, vec2(0.5 - q.x * 0.5, (0.5 - q.y * 0.5 + 1.0 + u.uMoon) / 9.0), 0.0);
                color += t.rgb * t.a;
            }
        }
    }
    frag = vec4(color, 1.0);
}
