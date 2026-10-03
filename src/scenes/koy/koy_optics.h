// Scene 5 "Koy": the water's and the daylight's optics, on the host, at 5 nm (docs/koy/PLAN.md §5.2-5.3).
//
// Everything that depends on wavelength is computed here once and baked into tables of CIE XYZ (koy_params.h, "the baked
// tables"): pure water's absorption (Pope & Fry 1997) and scattering, the particles of a Jerlov water type (Solonenko &
// Mobley 2015), the sun and the sky (ASTM G173), the eye (CIE 1931). The diffuse light field of the water column (sky light
// through Snell's window, the sun's light scattered many times, the sand's light from below) is a Monte Carlo simulation of
// a plane-parallel column, also here (the "oven": deterministic, a fixed seed). The frame only reads tables.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_params.h"

#include <string>
#include <vector>

struct KoyOptics {
    static constexpr int NL = 81;              // 380 .. 780 nm every 5 nm
    static constexpr float L0 = 380.0f, DL = 5.0f;
    static float lambdaOf(int i) { return L0 + DL * i; }

    // ---- inputs (build fills them from the data and the settings)
    float aw[NL], aph[NL], acdom[NL], a[NL];    // absorption: water, phytoplankton, CDOM, total (1/m)
    float bw[NL], bp[NL], b[NL], c[NL];         // scattering: water, particles, total; attenuation
    float sunPerp[NL];                          // direct sun in air on a plane facing it, W m^-2 nm^-1
    float skyHoriz[NL];                         // diffuse sky on a horizontal plane in air
    float sunWaterPerp[NL];                     // the refracted direct sun just below a flat surface, on a plane facing it
    float cmf[3][NL];                           // CIE 1931 2 degree x, y, z bar
    float basis[3][NL];                         // three smooth reflectance spectra (partition of unity)
    float sand[NL];                             // the sea floor's reflectance (for the light field's bottom)
    float rim[NL];                              // the cove's sunlit rim seen through the window: radiance
    float skyBlue[NL], skyWhite[NL];            // the sky's two spectral shapes, normalised to unit Y

    // ---- derived
    float waterN = 1.333f;
    float sunElevDeg = 70.0f, sunAzimDeg = 180.0f;
    float3 sunAir{}, sunWater{};
    float sunCosW = 1.0f;
    float sunRadiusAir = 0.00465f;
    float Bp = 0.018f, ffN = 1.10f, ffMu = 3.5f, ffNu = 0.0f, ffD180 = 0.0f;
    float ffScale = 1.0f, ffTruncated = 0.0f;   // delta-M: 1 / (1 - share inside the sun-disc cone), that share
    float chl = 0.05f;
    std::string jerlov = "IA";
    float bottomDepth = 8.0f;                   // the floor of the light field being built
    std::vector<float> bottoms;                 // every floor depth a field is built for (ascending; light.bottoms)
    float basisFix[3][3] = {};
    float3 xyzToRgb[3] = {};
    float skyY = 0.0f, perez[5] = {}, perezNorm = 1.0f, coastSin = 0.2f;
    float whiteY = 0.0f;                        // Y of daylight on a horizontal plane above the water (exposure reference)

    // ---- the light field of the water column: L[lambda][depth plane][mu bin] (W m^-2 sr^-1 nm^-1)
    int photons = 400000;
    std::vector<float> field;

    // ---- tables (host copies; upload makes the textures)
    std::vector<float4> pathTab;                // PT_W x PT_ROWS
    std::vector<float4> ambSurf[3];             // AS_T x AS_N x AS_Z
    std::vector<float4> ambPath;                // AP_T x AP_MU x AP_Z
    std::vector<float4> reflPath;               // RP_T x RP_MU x RP_TS
    struct FieldTables {                        // the three above for each of bottoms (the members: the deepest one's)
        std::vector<float4> ambSurf[3], ambPath, reflPath;
    };
    std::vector<FieldTables> fieldTables;
    double msBuild = 0.0, msField = 0.0;

    void build(const Config& cfg);              // everything above
    void upload(KoyParams& p);                  // textures and constants into the launch parameters
    void release();
    std::string summary() const;

    // helpers shared with the tests
    float phaseFF(float cosTheta) const;
    double phaseIntegral() const;               // of phaseFF over the sphere (1 once normalised)
    static float phaseWater(float cosTheta);
    float fresnelWaterToAir(float cosI) const;  // unpolarised, 1 beyond the critical angle
    float fresnelAirToWater(float cosI) const;
    float3 xyzOf(const float* spectrum) const;  // sum cmf * s * dlambda
    double kdDown(int li, float z0, float z1) const;   // diffuse attenuation of downwelling irradiance in the light field

private:
    void spectra(const Config& cfg);
    void lightField();
    void tables();
    std::vector<unsigned long long> textures_;
    std::vector<void*> arrays_;
};
