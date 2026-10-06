#version 460 core
// Scene 8 · how finely each triangle is cut: an edge's factor is its length over the target size at its distance, in
// samples. Computed from the edge's two ends only (both triangles sharing it compute the same number: no cracks).
#include "common.glsl"
layout(vertices = 3) out;

in V {
    vec3 p, n, dn;
    vec4 t;
    vec2 uv, uv2, fp;
    flat uint mat, feat;
    float edge, mask;
} i[];
out V {
    vec3 p, n, dn;
    vec4 t;
    vec2 uv, uv2, fp;
    flat uint mat, feat;
    float edge, mask;
} o[];

// the size of one sample at distance d (metres)
float sampleSize(float d) { return 2.0 * camPos.w * d / viewport.y; }

float edgeFactor(vec3 a, vec3 b) {
    vec3 m = 0.5 * (a + b);
    float d = max(distance(m, camPos.xyz), 0.02);
    float len = distance(a, b);
    // the target: target.x samples per triangle edge at this distance
    float f = len / (target.x * featureTessScale(i[0].feat, i[0].fp) * sampleSize(d));
    return clamp(f, 1.0, target.y);
}

bool debugCull() { return misc.z != 3.0; }   // the crack view draws back faces (magenta)

bool outside(vec4 c[3], int axis, float sgn) {
    for (int k = 0; k < 3; ++k)
        if (sgn * c[k][axis] <= c[k].w) return false;
    return true;
}

void main() {
    o[gl_InvocationID].p = i[gl_InvocationID].p;
    o[gl_InvocationID].n = i[gl_InvocationID].n;
    o[gl_InvocationID].dn = i[gl_InvocationID].dn;
    o[gl_InvocationID].t = i[gl_InvocationID].t;
    o[gl_InvocationID].uv = i[gl_InvocationID].uv;
    o[gl_InvocationID].uv2 = i[gl_InvocationID].uv2;
    o[gl_InvocationID].fp = i[gl_InvocationID].fp;
    o[gl_InvocationID].mat = i[gl_InvocationID].mat;
    o[gl_InvocationID].feat = i[gl_InvocationID].feat;
    o[gl_InvocationID].edge = i[gl_InvocationID].edge;
    o[gl_InvocationID].mask = i[gl_InvocationID].mask;
    if (gl_InvocationID == 0) {
        // frustum: a patch wholly beyond one plane (with a margin for displacement and Phong bulge) is dropped
        vec4 c[3];
        float margin = 0.01;
        for (int k = 0; k < 3; ++k) c[k] = viewProj * vec4(i[k].p, 1.0);
        bool cull = false;
        for (int ax = 0; ax < 2 && !cull; ++ax) {
            bool pos = true, neg = true;
            for (int k = 0; k < 3; ++k) {
                float w = c[k].w + margin * 4.0;
                if (c[k][ax] <= w) pos = false;
                if (-c[k][ax] <= w) neg = false;
            }
            cull = pos || neg;
        }
        // behind the eye entirely
        if (c[0].w < -margin && c[1].w < -margin && c[2].w < -margin) cull = true;
        // facing away: every corner's (smooth) normal points away from the eye by a margin (the bulge and the
        // displacement stay within a few millimetres)
        bool away = true;
        for (int k = 0; k < 3 && away; ++k) {
            vec3 toEye = camPos.xyz - i[k].p;
            if (dot(i[k].dn, toEye) > -0.004 * length(toEye) - 0.004) away = false;
        }
        if (away && debugCull()) cull = true;
        if (cull) {
            gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = gl_TessLevelOuter[2] = 0.0;
            gl_TessLevelInner[0] = 0.0;
        } else {
            // outer level k is the edge opposite vertex k
            float e0 = edgeFactor(i[1].p, i[2].p);
            float e1 = edgeFactor(i[2].p, i[0].p);
            float e2 = edgeFactor(i[0].p, i[1].p);
            gl_TessLevelOuter[0] = e0;
            gl_TessLevelOuter[1] = e1;
            gl_TessLevelOuter[2] = e2;
            gl_TessLevelInner[0] = max(e0, max(e1, e2));
        }
    }
}
