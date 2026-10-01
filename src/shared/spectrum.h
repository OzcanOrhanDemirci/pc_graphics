// Spectral building blocks, shared by host (tests, table building) and device.
#pragma once
#include "vec.h"

// pbrt-v4: wavelengths sampled with a pdf that follows the eye's sensitivity, 360-830 nm.
HD float sampleVisibleWavelength(float u) {
    return 538.0f - 138.888889f * atanhf(0.85691062f - 1.82750197f * u);
}
HD float visibleWavelengthPdf(float lambda) {
    if (lambda < LAMBDA_MIN || lambda > LAMBDA_MAX) return 0.0f;
    const float c = coshf(0.0072f * (lambda - 538.0f));
    return 0.0039398042f / (c * c);
}

// CIE 1931 2-degree colour matching functions, multi-lobe Gaussian fit (Wyman, Sloan, Shirley 2013).
HD float cmfLobe(float x, float mu, float s1, float s2) {
    const float t = (x - mu) / (x < mu ? s1 : s2);
    return expf(-0.5f * t * t);
}
HD float3 cieXYZ(float l) {
    const float x = 1.056f * cmfLobe(l, 599.8f, 37.9f, 31.0f) + 0.362f * cmfLobe(l, 442.0f, 16.0f, 26.7f)
                  - 0.065f * cmfLobe(l, 501.1f, 20.4f, 26.2f);
    const float y = 0.821f * cmfLobe(l, 568.8f, 46.9f, 40.5f) + 0.286f * cmfLobe(l, 530.9f, 16.3f, 31.1f);
    const float z = 1.217f * cmfLobe(l, 437.0f, 11.8f, 36.0f) + 0.681f * cmfLobe(l, 459.0f, 26.0f, 13.8f);
    return make_float3(x, y, z);
}
// Integral of y-bar over wavelength for this fit (nm). Dividing by it makes a flat spectrum of 1 give Y = 1.
#define CIE_Y_INTEGRAL 106.92f

HD float3 xyzToLinearSRGB(float3 c) {
    return make_float3(3.2404542f * c.x - 1.5371385f * c.y - 0.4985314f * c.z,
                       -0.9692660f * c.x + 1.8760108f * c.y + 0.0415560f * c.z,
                       0.0556434f * c.x - 0.2040259f * c.y + 1.0572252f * c.z);
}
HD float3 linearSRGBToXYZ(float3 c) {
    return make_float3(0.4124564f * c.x + 0.3575761f * c.y + 0.1804375f * c.z,
                       0.2126729f * c.x + 0.7151522f * c.y + 0.0721750f * c.z,
                       0.0193339f * c.x + 0.1191920f * c.y + 0.9503041f * c.z);
}

// Reflectance spectra: sigmoid of a quadratic in normalised wavelength (Jakob & Hanika 2019).
// Smooth, bounded to [0,1], three coefficients fitted on the host from an sRGB albedo.
HD float sigmoidSpectrum(float3 c, float lambda) {
    const float t = (lambda - LAMBDA_MIN) / (LAMBDA_MAX - LAMBDA_MIN);
    const float x = (c.x * t + c.y) * t + c.z;
    if (isinf(x)) return x > 0.0f ? 1.0f : 0.0f;
    return 0.5f + x / (2.0f * sqrtf(1.0f + x * x));
}
HD float4 sigmoidSpectrum4(float3 c, float4 l) {
    return make_float4(sigmoidSpectrum(c, l.x), sigmoidSpectrum(c, l.y), sigmoidSpectrum(c, l.z), sigmoidSpectrum(c, l.w));
}

// Planck's law, spectral radiance in W / (m^2 sr nm).
HD float blackbody(float lambdaNm, float T) {
    const double l = lambdaNm * 1e-9;
    const double c1 = 1.1910429723971884e-16;  // 2 h c^2
    const double c2 = 1.4387768775039337e-2;   // h c / k
    return float(c1 / (l * l * l * l * l) / (exp(c2 / (l * T)) - 1.0) * 1e-9);
}

// Refractive indices.
HD float sellmeier(float lambdaNm, float b1, float b2, float b3, float c1, float c2, float c3) {
    const float l2 = lambdaNm * lambdaNm * 1e-6f;  // um^2
    return sqrtf(1.0f + b1 * l2 / (l2 - c1) + b2 * l2 / (l2 - c2) + b3 * l2 / (l2 - c3));
}
HD float iorOf(int model, float lambda) {
    switch (model) {
    case IOR_WATER: {  // empirical fit to pure water at ~20 C (Quan & Fry form), 400-700 nm within 1e-3
        const float l = lambda;
        return 1.31279f + 15.762f / l - 4382.0f / (l * l) + 1.1455e6f / (l * l * l);
    }
    case IOR_BK7: return sellmeier(lambda, 1.03961212f, 0.231792344f, 1.01046945f, 0.00600069867f, 0.0200179144f, 103.560653f);
    case IOR_SF11: return sellmeier(lambda, 1.73759695f, 0.313747346f, 1.89878101f, 0.013188707f, 0.0623068142f, 155.23629f);
    default: return 1.000277f;
    }
}
// Water disperses too, but weakly (dn ~ 0.01 across the visible, ~0.5 deg): it refracts along the hero
// wavelength only, keeping all four wavelengths alive instead of paying chromatic noise on every pool ray.
HD bool iorIsDispersive(int model) { return model == IOR_BK7 || model == IOR_SF11; }

// Unpolarised Fresnel reflectance of a smooth dielectric interface. eta = n_transmitted / n_incident.
HD float fresnelDielectric(float cosI, float eta) {
    cosI = clampf(cosI, 0.0f, 1.0f);
    const float sin2T = (1.0f - cosI * cosI) / (eta * eta);
    if (sin2T >= 1.0f) return 1.0f;
    const float cosT = sqrtf(1.0f - sin2T);
    const float rs = (cosI - eta * cosT) / (cosI + eta * cosT);
    const float rp = (eta * cosI - cosT) / (eta * cosI + cosT);
    return 0.5f * (rs * rs + rp * rp);
}

// Thin film in air (soap film): Airy reflectance, both polarisations, no absorption.
// d in nm, film index nf, cosI the cosine in air.
HD float thinFilmReflectance(float lambda, float d, float nf, float cosI) {
    cosI = clampf(cosI, 0.0f, 1.0f);
    const float sinT2 = (1.0f - cosI * cosI) / (nf * nf);
    const float cosT = sqrtf(fmaxf(0.0f, 1.0f - sinT2));
    const float rs = (cosI - nf * cosT) / (cosI + nf * cosT);
    const float rp = (nf * cosI - cosT) / (nf * cosI + cosT);
    const float delta = 4.0f * M_PIf * nf * d * cosT / lambda;
    const float c = cosf(delta);
    const float rs2 = rs * rs, rp2 = rp * rp;
    const float Rs = 2.0f * rs2 * (1.0f - c) / (1.0f + rs2 * rs2 - 2.0f * rs2 * c);
    const float Rp = 2.0f * rp2 * (1.0f - c) / (1.0f + rp2 * rp2 - 2.0f * rp2 * c);
    return 0.5f * (Rs + Rp);
}
