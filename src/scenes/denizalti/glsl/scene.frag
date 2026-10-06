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
layout(binding = 1) uniform sampler2D lmL0;
layout(binding = 2) uniform sampler2D lmR;
layout(binding = 3) uniform sampler2D lmG;
layout(binding = 4) uniform sampler2D lmB;
layout(binding = 5) uniform sampler2D lmVis;

// irradiance from one channel's L1 harmonics, the non-linear reconstruction (Geomerics: no negative lobe, a strongly
// directional light stays sharp)
float irradianceL1(float L0, vec3 L1, vec3 n) {
    float R0 = PI * 0.282095 * L0;                    // the mean irradiance
    float R1 = (2.0 * PI / 3.0) * 0.488603 * length(L1);   // its directional part
    if (R0 <= 1e-8) return 0.0;
    float r1 = clamp(R1 / R0, 0.0, 0.999);
    vec3 dir = R1 > 1e-9 ? L1 / length(L1) : n;
    float q = 0.5 * (1.0 + dot(dir, n));
    float p = 1.0 + 2.0 * r1;
    float a = (1.0 - r1) / (1.0 + r1);
    return R0 * (a + (1.0 - a) * (p + 1.0) * pow(max(q, 0.0), p));
}
// the split-sum's environment term, analytic (Karis, mobile)
vec3 envBRDF(vec3 f0, float rough, float nv) {
    const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * AB.x + AB.y;
}

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
    // ---- the baked light: L1 spherical harmonics of what arrives here (amount and direction), read at the detail normal
    vec3 c0 = texture(lmL0, i.uv2).rgb;
    vec3 sr = texture(lmR, i.uv2).xyz, sg = texture(lmG, i.uv2).xyz, sb = texture(lmB, i.uv2).xyz;
    vec4 vis = texture(lmVis, i.uv2);
    vec3 E = vec3(irradianceL1(c0.r, sr, N), irradianceL1(c0.g, sg, N), irradianceL1(c0.b, sb, N));
    vec3 f0 = mix(vec3(0.04), m.base, m.metal);
    float nv = max(dot(N, V), 1e-4);
    float a = max(m.rough * m.rough, 0.002);
    // the environment's reflection (until the probes): the radiance the harmonics hold along the mirror direction,
    // with the split-sum's analytic fit
    vec3 R = reflect(-V, N);
    vec3 Lr = max(vec3(0.282095 * c0.r + 0.488603 * dot(sr, R), 0.282095 * c0.g + 0.488603 * dot(sg, R), 0.282095 * c0.b + 0.488603 * dot(sb, R)), vec3(0));
    vec3 envF = envBRDF(f0, m.rough, nv);
    c += (1.0 - m.metal) * m.base * (1.0 - envF) * E / PI;
    c += Lr * envF;
    // the key lamps' highlights: sphere lights, their shadow baked (visibility per texel)
    for (int k = 0; k < 4; ++k) {
        if (lampL[k].w <= 0.0) continue;
        vec3 Lv = lampPos[k].xyz - i.p;
        float d2 = dot(Lv, Lv), d = sqrt(d2);
        vec3 l = Lv / d;
        float nl = dot(N, l);
        if (nl <= 0.0) continue;
        float r = lampPos[k].w;
        float a2 = clamp(a + r / (2.0 * d), 0.0, 1.0);   // the sphere widens the lobe (Karis)
        float norm = (a / a2) * (a / a2);
        vec3 H = normalize(l + V);
        float nh = max(dot(N, H), 0.0), vh = max(dot(V, H), 0.0);
        vec3 F = F_Schlick(f0, vh);
        vec3 illum = lampL[k].rgb * (PI * r * r / d2) * nl;
        c += D_GGX(nh, a2) * V_Smith(nv, nl, a2) * F * norm * illum * vis[k];
    }
    if (misc.z == 1.0) c = N * 0.5 + 0.5;
    if (misc.z == 2.0) c = vec3(fract(i.uv * 10.0), 0.0);
    if (misc.z == 3.0) c = gl_FrontFacing ? vec3(0.3, 0.32, 0.3) * (0.4 + 0.6 * abs(N.y)) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(c, 1.0);
}
