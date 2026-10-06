#version 460 core
// Scene 8 · the base mesh's vertices pass through in world space (the world never moves); tessellation does the rest.
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec3 aDN;
layout(location = 3) in vec4 aT;
layout(location = 4) in vec2 aUV;
layout(location = 5) in vec2 aUV2;
layout(location = 6) in uint aMat;
layout(location = 7) in uint aFeat;
layout(location = 8) in vec2 aFP;
layout(location = 9) in float aEdge;
layout(location = 10) in float aMask;

out V {
    vec3 p, n, dn;
    vec4 t;
    vec2 uv, uv2, fp;
    flat uint mat, feat;
    float edge, mask;
} o;

void main() {
    o.p = aP;
    o.n = aN;
    o.dn = aDN;
    o.t = aT;
    o.uv = aUV;
    o.uv2 = aUV2;
    o.fp = aFP;
    o.mat = aMat;
    o.feat = aFeat;
    o.edge = aEdge;
    o.mask = aMask;
}
