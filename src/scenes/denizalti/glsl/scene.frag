#version 460 core
// Scene 8 · one sample's colour. Every sample is shaded on its own (sample shading: true supersampling); the
// surface's feature bends the normal down to ~2 samples (what the geometry did not take, the normal does).
// D0: a stand-in light (two lamps, no shadows) until the oven's light maps exist (D1).
#include "common.glsl"

in F {
    sample vec3 p, n;
    sample vec4 t;
    sample vec2 uv, uv2, fp;
    flat uint mat, feat;
    sample float edge, size, mask;
} i;

layout(location = 0) out vec4 outColor;

struct M {
    vec3 base;     // albedo (dielectric) or F0 (metal)
    float rough;
    float metal;
    vec3 emit;
};

M material(uint m) {
    M r;
    r.emit = vec3(0);
    r.metal = 0.0;
    r.rough = 0.5;
    r.base = vec3(0.5);
    if (m == MAT_PAINT_HULL) { r.base = vec3(0.62, 0.66, 0.60); r.rough = 0.42; }
    else if (m == MAT_PAINT_PIPE) { r.base = vec3(0.52, 0.53, 0.50); r.rough = 0.38; }
    else if (m == MAT_PAINT_DARK) { r.base = vec3(0.035); r.rough = 0.55; }
    else if (m == MAT_BRASS) { r.base = vec3(0.87, 0.72, 0.42); r.rough = 0.28; r.metal = 1.0; }
    else if (m == MAT_STEEL) { r.base = vec3(0.56, 0.57, 0.58); r.rough = 0.33; r.metal = 1.0; }
    else if (m == MAT_CHROME) { r.base = vec3(0.55, 0.56, 0.57); r.rough = 0.08; r.metal = 1.0; }
    else if (m == MAT_RUBBER) { r.base = vec3(0.025); r.rough = 0.75; }
    else if (m == MAT_DECK) { r.base = vec3(0.16, 0.16, 0.15); r.rough = 0.55; }
    else if (m == MAT_GLASS) { r.base = vec3(0.02); r.rough = 0.02; }
    else if (m == MAT_DIAL) { r.base = vec3(0.80, 0.79, 0.74); r.rough = 0.45; }
    else if (m == MAT_RED) { r.base = vec3(0.45, 0.035, 0.025); r.rough = 0.36; }
    else if (m == MAT_EMISSIVE) { r.base = vec3(0.8); r.emit = vec3(60.0, 42.0, 22.0); }
    return r;
}

float D_GGX(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float V_Smith(float nv, float nl, float a) {
    float k = a * 0.5;
    return 0.25 / ((nv * (1.0 - k) + k) * (nl * (1.0 - k) + k));
}
vec3 F_Schlick(vec3 f0, float vh) { return f0 + (1.0 - f0) * pow(1.0 - vh, 5.0); }

vec3 lamp(vec3 p, vec3 N, vec3 V, M m, vec3 lp, vec3 I) {
    vec3 L = lp - p;
    float d2 = dot(L, L);
    L *= inversesqrt(d2);
    float nl = max(dot(N, L), 0.0);
    if (nl <= 0.0) return vec3(0);
    vec3 H = normalize(L + V);
    float nv = max(dot(N, V), 1e-4), nh = max(dot(N, H), 0.0), vh = max(dot(V, H), 0.0);
    float a = max(m.rough * m.rough, 0.002);
    vec3 f0 = mix(vec3(0.04), m.base, m.metal);
    vec3 F = F_Schlick(f0, vh);
    vec3 spec = D_GGX(nh, a) * V_Smith(nv, nl, a) * F;
    vec3 diff = (1.0 - m.metal) * m.base / PI * (1.0 - F);
    return (diff + spec) * I * nl / d2;
}

void main() {
    if (i.mat == MAT_GLASS) discard;   // D0: the glass comes with its own pass (D4)
    M m = material(i.mat);
    vec3 N = normalize(i.n);
    vec3 T = normalize(i.t.xyz - N * dot(N, i.t.xyz));
    vec3 B = cross(N, T) * (i.t.w < 0.0 ? -1.0 : 1.0);
    // the feature's normal detail, band-limited at this sample's footprint
    if (i.feat != FEAT_NONE && misc.w > 0.0) {
        float d = max(distance(i.p, camPos.xyz), 0.02);
        Surf s;
        s.p = i.p;
        s.uv = i.uv;
        s.fp = i.fp;
        s.feat = i.feat;
        s.size = 2.0 * camPos.w * d / viewport.y;
        vec3 gw;
        featureHeight(s, T, B, gw);
        gw *= i.mask * misc.w;
        N = normalize(N - (gw - N * dot(N, gw)));
    }
    vec3 V = normalize(camPos.xyz - i.p);
    if (dot(N, V) < 0.0) N = normalize(N + V * (-dot(N, V) + 0.01));
    vec3 c = m.emit;
    // D0 stand-in light: a warm bulb overhead, a red lamp aft, a faint cool fill from the open side
    c += lamp(i.p, N, V, m, vec3(-1.25, 2.55, -0.2), vec3(2.2, 1.6, 0.95) * 1.2);
    c += lamp(i.p, N, V, m, vec3(-1.6, 2.1, 1.3), vec3(0.9, 0.04, 0.02) * 0.9);
    c += lamp(i.p, N, V, m, vec3(1.5, 1.4, -0.6), vec3(0.18, 0.2, 0.24) * 1.5);
    float sky = 0.5 + 0.5 * N.y;
    vec3 f0 = mix(vec3(0.04), m.base, m.metal);
    c += (mix(vec3(0.010, 0.010, 0.012), vec3(0.03, 0.025, 0.02), sky)) * ((1.0 - m.metal) * m.base + m.metal * f0 * 0.6);
    if (misc.z == 1.0) c = N * 0.5 + 0.5;
    if (misc.z == 2.0) c = vec3(fract(i.uv * 10.0), 0.0);
    if (misc.z == 3.0) c = gl_FrontFacing ? vec3(0.3, 0.32, 0.3) * (0.4 + 0.6 * abs(N.y)) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(c, 1.0);
}
