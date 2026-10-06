#version 460 core
// Scene 8 · the resolved HDR picture to display: exposure, AgX, the display's encoding and a fixed dither (the same
// pattern every frame: no grain, no shimmer, only the 8-bit steps broken).
in vec2 vUV;
layout(binding = 0) uniform sampler2D hdr;
uniform float exposure;   // multiplier
layout(location = 0) out vec4 outColor;

vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 agx(vec3 c) {
    mat3 inm = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336,
                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    mat3 outm = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417,
                     -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    vec3 v = inm * c;
    v = clamp(log2(max(v, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    v = outm * v;
    return pow(max(v, vec3(0)), vec3(2.2));
}
vec3 srgb(vec3 x) {
    x = clamp(x, 0.0, 1.0);
    return mix(12.92 * x, 1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), x));
}
uint hash2(uvec2 p) {
    p = p * uvec2(1597334673u, 3812015801u);
    uint n = (p.x ^ p.y) * 1597334673u;
    n ^= n >> 16;
    n *= 2246822519u;
    n ^= n >> 13;
    return n;
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(hdr, px, 0).rgb * exposure;
    vec3 d = srgb(agx(c));
    // triangular dither, fixed per pixel
    uint h = hash2(uvec2(px));
    float r1 = float(h & 65535u) / 65535.0, r2 = float(h >> 16) / 65535.0;
    d += (r1 + r2 - 1.0) / 255.0;
    outColor = vec4(d, 1.0);
}
