#version 460 core
// Scene 8 · a tessellated point: Phong-tessellated (curved surfaces round out between the base mesh's vertices, along
// the shared displacement normals so neighbours agree), then displaced by the surface's feature.
#include "common.glsl"
layout(triangles, fractional_odd_spacing, ccw) in;

in V {
    vec3 p, n, dn;
    vec4 t;
    vec2 uv, uv2, fp;
    flat uint mat, feat;
    float edge, mask;
} i[];
out F {
    vec3 p, n;
    vec4 t;
    vec2 uv, uv2, fp;
    flat uint mat, feat;
    float edge, size, mask;
} o;

// a fixed order of the three corners by position: two triangles sharing an edge sum its two ends in the same order
// (and the third corner's weight is exactly 0 on it), so a point on the edge comes out bitwise the same from both
bool before(vec3 a, vec3 b) { return a.x < b.x || (a.x == b.x && (a.y < b.y || (a.y == b.y && a.z < b.z))); }

void main() {
    vec3 w = gl_TessCoord;
    int k0 = 0, k1 = 1, k2 = 2;
    if (before(i[k1].p, i[k0].p)) { int t = k0; k0 = k1; k1 = t; }
    if (before(i[k2].p, i[k1].p)) { int t = k1; k1 = k2; k2 = t; }
    if (before(i[k1].p, i[k0].p)) { int t = k0; k0 = k1; k1 = t; }
    precise float w0 = w[k0], w1 = w[k1], w2 = w[k2];
    precise vec3 p = w0 * i[k0].p + w1 * i[k1].p + w2 * i[k2].p;
    // Phong tessellation on the displacement normals (identical at shared positions), in the same order
    precise vec3 q0 = p - dot(p - i[k0].p, i[k0].dn) * i[k0].dn;
    precise vec3 q1 = p - dot(p - i[k1].p, i[k1].dn) * i[k1].dn;
    precise vec3 q2 = p - dot(p - i[k2].p, i[k2].dn) * i[k2].dn;
    precise vec3 ph = w0 * q0 + w1 * q1 + w2 * q2;
    // the shape factor: 0.5 puts an edge's midpoint back on a circular arc through its ends (x = r cos t (1 + a sin^2 t)
    // = r to second order); the usual 0.75 pushed it a half sagitta beyond, a 20-gon disc came out scalloped
    p = p + (ph - p) * 0.5;
    precise vec3 dn = normalize(w0 * i[k0].dn + w1 * i[k1].dn + w2 * i[k2].dn);
    vec2 uv = w.x * i[0].uv + w.y * i[1].uv + w.z * i[2].uv;
    o.n = normalize(w.x * i[0].n + w.y * i[1].n + w.z * i[2].n);
    o.t = w.x * i[0].t + w.y * i[1].t + w.z * i[2].t;
    o.uv = uv;
    o.uv2 = w.x * i[0].uv2 + w.y * i[1].uv2 + w.z * i[2].uv2;
    o.fp = i[0].fp;
    o.mat = i[0].mat;
    o.feat = i[0].feat;
    o.edge = w.x * i[0].edge + w.y * i[1].edge + w.z * i[2].edge;
    o.mask = w.x * i[0].mask + w.y * i[1].mask + w.z * i[2].mask;
    // the sampling size here, a function of the position alone (so a point on a shared edge gets the same height from
    // both triangles: no cracks): the target triangle size at this distance, floored by what the base mesh can reach
    // (its longest edge over the largest factor)
    float sz = max(target.x * featureTessScale(i[0].feat, i[0].fp) * 2.0 * camPos.w * max(distance(p, camPos.xyz), 0.02) / viewport.y, target.z);
    o.size = sz;
    if (target.w > 0.5 && i[0].feat != FEAT_NONE) {
        Surf s;
        s.p = p;
        s.uv = uv;
        s.fp = i[0].fp;
        s.feat = i[0].feat;
        s.size = sz;
        vec3 gw;
        float h = featureHeight(s, vec3(1, 0, 0), vec3(0, 1, 0), gw);
        precise float mask = w0 * i[k0].mask + w1 * i[k1].mask + w2 * i[k2].mask;
        p += dn * (h * mask);
    }
    o.p = p;
    gl_Position = viewProj * vec4(p, 1.0);
}
