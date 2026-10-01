// Display transform pieces shared by the OptiX tonemap programs and the CUDA kernels (one definition).
#pragma once
#include "vec.h"

HD float3 agxContrast(float3 x) {
    const float3 x2 = x * x, x4 = x2 * x2;
    return x4 * x2 * 15.5f - x4 * x * 40.14f + x4 * 31.96f - x2 * x * 6.868f + x2 * 0.4298f + x * 0.1191f - make_float3(0.00232f, 0.00232f, 0.00232f);
}
// AgX (Troy Sobotka; minimal fit by Benjamin Wrensch). Input linear Rec.709, output display-linear.
HD float3 agx(float3 c) {
    float3 v = make_float3(0.842479062253094f * c.x + 0.0784335999999992f * c.y + 0.0792237451477643f * c.z,
                           0.0423282422610123f * c.x + 0.878468636469772f * c.y + 0.0791661274605434f * c.z,
                           0.0423756549057051f * c.x + 0.0784336f * c.y + 0.879142973793104f * c.z);
    const float minEv = -12.47393f, maxEv = 4.026069f;
    v = make_float3(clampf(log2f(fmaxf(v.x, 1e-10f)), minEv, maxEv), clampf(log2f(fmaxf(v.y, 1e-10f)), minEv, maxEv),
                    clampf(log2f(fmaxf(v.z, 1e-10f)), minEv, maxEv));
    v = (v - make_float3(minEv, minEv, minEv)) * (1.0f / (maxEv - minEv));
    v = agxContrast(v);
    const float3 o = make_float3(1.19687900512017f * v.x - 0.0980208811401368f * v.y - 0.0990297440797205f * v.z,
                                 -0.0528968517574562f * v.x + 1.15190312990417f * v.y - 0.0989611768448433f * v.z,
                                 -0.0529716355144438f * v.x - 0.0980434501171241f * v.y + 1.15107367264116f * v.z);
    return make_float3(powf(fmaxf(o.x, 0.0f), 2.2f), powf(fmaxf(o.y, 0.0f), 2.2f), powf(fmaxf(o.z, 0.0f), 2.2f));
}
HD float srgbOetf(float x) {
    x = saturate(x);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}
// Out-of-gamut spectral colours: pull towards the luminance until no channel is negative.
HD float3 gamutCompress(float3 rgb, float Y) {
    const float mn = fminf(rgb.x, fminf(rgb.y, rgb.z));
    if (mn >= 0.0f || Y <= 0.0f) return fmaxf3(rgb, make_float3(0, 0, 0));
    const float t = Y / (Y - mn);
    return make_float3(Y, Y, Y) + (rgb - make_float3(Y, Y, Y)) * t;
}
HD float luminance709(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
