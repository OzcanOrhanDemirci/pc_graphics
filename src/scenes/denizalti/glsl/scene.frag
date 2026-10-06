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
layout(binding = 10) uniform sampler3D froxels;   // the haze along the view (froxel.comp): in-scatter so far, transmittance

// irradiance from L1 harmonics, one direction for all three channels (the luminance's): a weak channel's own direction
// is mostly noise and would tint a bumpy surface in specks. Each channel keeps its own mean and its own share of the
// directional part along that direction; the response is the linear one (gentle, never below zero).
vec3 irradianceL1(vec3 L0, vec3 r, vec3 g, vec3 b, vec3 n) {
    vec3 R0 = PI * 0.282095 * L0;
    vec3 l1 = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    float ll = length(l1);
    vec3 dir = ll > 1e-9 ? l1 / ll : n;
    // each channel's directional amount along the shared direction
    vec3 R1 = (2.0 * PI / 3.0) * 0.488603 * vec3(dot(r, dir), dot(g, dir), dot(b, dir));
    vec3 ratio = clamp(R1 / max(R0, vec3(1e-8)), vec3(0.0), vec3(0.95));
    return max(R0 * (1.0 + ratio * dot(dir, n)), vec3(0.0));
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
    else if (m == MAT_EMISSIVE) { r.base = vec3(0.8); r.emit = vec3(1.0, 0.72, 0.42) * (40.0 / 0.758); }
    else if (m == MAT_CREAM) { r.base = vec3(0.72, 0.68, 0.56); r.rough = 0.38; }
    else if (m == MAT_GREY) { r.base = vec3(0.40, 0.42, 0.42); r.rough = 0.42; }
    else if (m == MAT_DECK_GREEN) { r.base = vec3(0.10, 0.20, 0.13); r.rough = 0.45; }
    else if (m == MAT_VINYL) { r.base = vec3(0.08, 0.38, 0.40); r.rough = 0.35; }
    else if (m == MAT_ALU) { r.base = vec3(0.91, 0.92, 0.92); r.rough = 0.40; r.metal = 1.0; }
    else if (m == MAT_WRINKLE) { r.base = vec3(0.03); r.rough = 0.70; }
    else if (m == MAT_RED_GLASS) { r.base = vec3(0.5, 0.02, 0.01); r.rough = 0.1; r.emit = vec3(1.0, 0.02, 0.006) * (0.55 / 0.2126); }
    else if (m == MAT_TAPE) { r.base = vec3(0.42, 0.04, 0.03); r.rough = 0.75; }
    else if (m == MAT_BLACK_PLATE) { r.base = vec3(0.03); r.rough = 0.3; }
    else if (m == MAT_LAMP_SHADE) { r.base = vec3(0.05, 0.11, 0.07); r.rough = 0.3; }
    else if (m == MAT_WOOD) { r.base = vec3(0.36, 0.22, 0.12); r.rough = 0.4; }
    else if (m == MAT_PAPER) { r.base = vec3(0.80, 0.78, 0.70); r.rough = 0.8; }
    else if (m == MAT_INDICATOR_RED) { r.base = vec3(0.3, 0.02, 0.02); r.rough = 0.15; r.emit = vec3(1.0, 0.03, 0.02) * 0.22; }
    else if (m == MAT_INDICATOR_GREEN) { r.base = vec3(0.02, 0.3, 0.05); r.rough = 0.15; r.emit = vec3(0.02, 1.0, 0.18) * 0.22; }
    return r;
}

bool painted(uint m) {
    return m == MAT_CREAM || m == MAT_PAINT_PIPE || m == MAT_PAINT_HULL || m == MAT_GREY || m == MAT_DECK_GREEN || m == MAT_RED ||
           m == MAT_PAINT_DARK || m == MAT_DECK;
}

// A boat in commission, kept up but lived in (Ozcan's 1-a): paint chipped on the edges hands and boots knock (down to
// the red primer, at the worst to bare steel), grime gathered in the corners, a little dust on what faces up, brass
// tarnished where it is not handled, the deck's paint worn along the walkway. Deterministic: world position, the
// baked occlusion, the edge weight the kit wrote; every threshold softened by the sample's footprint (no aliasing).
void weather(inout M m, uint mat, vec3 p, inout vec3 N, float edge, float ao, float size) {
    float fw = max(size * 40.0, 0.02);   // the width of a threshold in noise units at this footprint
    vec3 d;
    if (painted(mat)) {
        // chips: where the edge is exposed and the noise says so
        float n1 = fbm(p, 0.018, 3, 0.55, size, d);
        float n2 = fbm(p + 11.3, 0.0045, 2, 0.5, size, d);
        // chips need an edge: a face alone never chips (else specks everywhere, read as grain)
        float worn = edge * 1.3 + n1 * 0.7 + n2 * 0.25 - 1.05 - (1.0 - smoothstep(0.2, 0.55, edge)) * 2.0;
        if (mat == MAT_DECK_GREEN || mat == MAT_DECK) {
            // the walkway: boots scuff the paint along the centre line and in front of the stations
            float walk = 1.0 - smoothstep(0.35, 0.9, abs(p.x)) + 0.5 * (1.0 - smoothstep(0.2, 0.6, abs(abs(p.x) - 1.3)));
            float scuff = clamp(walk * (0.6 + n1), 0.0, 1.0);
            m.base = mix(m.base, m.base * 1.35 + vec3(0.02), scuff * 0.5);
            m.rough = mix(m.rough, 0.6, scuff * 0.6);
        }
        float primer = smoothstep(-fw, fw, worn);
        float steel = smoothstep(0.18 - fw, 0.18 + fw, worn);
        m.base = mix(m.base, vec3(0.30, 0.11, 0.06), primer);
        m.rough = mix(m.rough, 0.62, primer);
        m.base = mix(m.base, vec3(0.50, 0.50, 0.50), steel);
        m.metal = mix(m.metal, 1.0, steel);
        m.rough = mix(m.rough, 0.42, steel);
        // the paint film's edge: a step where it chips (the normal tilts across it)
        // grime: in the corners the paint darkens and dulls, a little streaked downwards
        float gn = fbm(p * vec3(1.0, 0.35, 1.0), 0.06, 3, 0.5, size, d);
        float grime = clamp((1.0 - ao) * 1.6 + gn * 0.5 - 0.25, 0.0, 1.0);
        m.base *= mix(1.0, 0.62, grime);
        m.rough = mix(m.rough, min(m.rough + 0.2, 0.85), grime);
        // smudges: the gloss varies where hands go
        float sm = fbm(p + 4.1, 0.09, 2, 0.5, size, d);
        m.rough = clamp(m.rough + 0.08 * sm, 0.05, 0.95);
    }
    if (mat == MAT_BRASS) {
        float t = fbm(p, 0.03, 3, 0.55, size, d);
        float tarnish = clamp(0.45 + t * 0.9 - edge * 0.6 - (1.0 - ao) * -0.5, 0.0, 1.0);
        m.base = mix(m.base, m.base * vec3(0.55, 0.48, 0.36), tarnish);
        m.rough = mix(m.rough, 0.45, tarnish);
    }
    if (mat == MAT_STEEL || mat == MAT_CHROME) {
        float t = fbm(p, 0.006, 2, 0.5, size, d);
        m.rough = clamp(m.rough + 0.08 * t + (1.0 - ao) * 0.15, 0.03, 0.9);
    }
    // dust on what faces up, away from hands (a damp boat: little of it)
    float up = smoothstep(0.55, 0.95, N.y);
    if (up > 0.0 && mat != MAT_DECK_GREEN && mat != MAT_EMISSIVE && mat != MAT_RED_GLASS) {
        float dn = fbm(p + 7.7, 0.02, 2, 0.5, size, d);
        float dust = up * clamp(0.35 + dn, 0.0, 1.0) * 0.35;
        m.base = mix(m.base, vec3(0.42, 0.40, 0.36), dust);
        m.rough = mix(m.rough, 0.8, dust);
        m.metal = mix(m.metal, 0.0, dust);
    }
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
    float ao = texture(lmL0, i.uv2).a;
    if (i.feat == FEAT_CORK) m.rough = max(m.rough, 0.6);   // enamel over cork dries satin (no glitter on the granules)
    // the printed faces: a dial's or a plate's colour from the atlas (filtered: anisotropic, mipmapped)
    if (i.feat == FEAT_DIAL || i.feat == FEAT_LABEL) {
        int id = int(i.fp.x + 0.5);
        vec4 dc = texture(decalTex, decalUV(id, i.uv, i.fp.y));
        float style = decInfo[id].w;
        if (i.feat == FEAT_DIAL) {
            m.base = dc.rgb;
            m.rough = mix(0.4, 0.5, dc.a);
            m.metal = 0.0;
        } else if (style < 0.5) {         // black plate, white filled engraving
            m.base = dc.rgb;
            m.rough = mix(0.28, 0.5, dc.a);
            m.metal = 0.0;
        } else if (style < 1.5) {         // brass, black filled engraving
            m.base = mix(vec3(0.87, 0.72, 0.42), dc.rgb, dc.a);
            m.metal = 1.0 - dc.a;
            m.rough = mix(0.22, 0.55, dc.a);
        } else {                          // a stencil: paint on paint
            m.base = mix(m.base, dc.rgb, dc.a);
            m.rough = mix(m.rough, 0.45, dc.a);
        }
    }
    {
        float d = max(distance(i.p, camPos.xyz), 0.02);
        weather(m, i.mat, i.p, N, i.edge, ao, 2.0 * camPos.w * d / viewport.y);
    }
    vec3 c = m.emit;
    // ---- the baked light: L1 spherical harmonics of what arrives here (amount and direction), read at the detail normal
    vec3 c0 = texture(lmL0, i.uv2).rgb;
    vec3 sr = texture(lmR, i.uv2).xyz, sg = texture(lmG, i.uv2).xyz, sb = texture(lmB, i.uv2).xyz;
    vec4 vis = texture(lmVis, i.uv2);
    vec3 E = irradianceL1(c0, sr, sg, sb, N);
    vec3 f0 = mix(vec3(0.04), m.base, m.metal);
    float nv = max(dot(N, V), 1e-4);
    float a = max(m.rough * m.rough, 0.002);
    // the environment's reflection (until the probes): the radiance the harmonics hold along the mirror direction,
    // with the split-sum's analytic fit
    vec3 R = reflect(-V, N);
    vec3 Lr = max(vec3(0.282095 * c0.r + 0.488603 * dot(sr, R), 0.282095 * c0.g + 0.488603 * dot(sg, R), 0.282095 * c0.b + 0.488603 * dot(sb, R)), vec3(0));
    vec3 envF = envBRDF(f0, m.rough, nv);
    c += (1.0 - m.metal) * m.base * (1.0 - envF) * E / PI;
    float specOcc = clamp(pow(nv + ao, exp2(-16.0 * m.rough - 1.0)) - 1.0 + ao, 0.0, 1.0);   // Lagarde
    c += Lr * envF * specOcc;
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
    // the haze between the eye and this sample
    if (haze.w > 0.5) {
        float dv = max(dot(i.p - camPos.xyz, camFwd.xyz), haze.x);
        float tz = log(dv / haze.x) / log(haze.y / haze.x) * haze.z;
        vec3 uvw = vec3(gl_FragCoord.xy / viewport.xy * viewport.z * 0.0 + gl_FragCoord.xy / (viewport.xy * viewport.z), (tz - 0.5) / haze.z);
        vec4 fr = texture(froxels, clamp(uvw, vec3(0), vec3(1)));
        c = c * fr.a + fr.rgb;
    }
    if (misc.z == 1.0) c = N * 0.5 + 0.5;
    if (misc.z == 2.0) c = vec3(fract(i.uv * 10.0), 0.0);
    if (misc.z == 3.0) c = gl_FrontFacing ? vec3(0.3, 0.32, 0.3) * (0.4 + 0.6 * abs(N.y)) : vec3(1.0, 0.0, 1.0);
    outColor = vec4(c, 1.0);
}
