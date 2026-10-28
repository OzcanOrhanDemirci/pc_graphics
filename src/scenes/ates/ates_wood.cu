// Scene 12 "Ateş": the wood's kernels (ates_wood.h).
#include "scenes/ates/ates_renderer.h"
#include "scenes/ates/ates_wood.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define WOOD_CHECK(x)                                                                                   \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

namespace {

#define SIGMA 5.670374e-8f
#define RGAS 8.314f
#define RAD_DIV 8   // the radiation's blocks: 8 gas cells a side

// the layers' outer radii (fractions of r0): a log's are finer at the surface (a millimetre where the char front
// moves), a twig's even
__host__ __device__ __forceinline__ float layerOuter(int kind, int i) {
    if (kind == 0) return 1.0f - float(i) / WOOD_LAYERS;
    const float g = 1.25f;
    // sum_{j<i} g^j / sum_{j<N} g^j
    const float num = powf(g, float(i)) - 1.0f, den = powf(g, float(WOOD_LAYERS)) - 1.0f;
    return 1.0f - num / den;
}

struct GasView {
    int nx, ny, nz;
    float h;
    float3 origin;
    const float *T, *O, *u, *v, *w;
    AGasSources src;
    float T0, Yo2, rhoT;
};

struct LogDev {
    float r0, len;
    int nTheta, nZ, kind;
    float pose[12];
};

__device__ __forceinline__ float3 xp(const float m[12], float3 p) {
    return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
}
__device__ __forceinline__ float3 xv(const float m[12], float3 v) {
    return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z);
}

// a patch's place and normal in the world, at its surface radius r
__device__ __forceinline__ void patchPlace(const LogDev& L, int local, float r, float3& p, float3& n) {
    const int it = local % L.nTheta, iz = local / L.nTheta;
    const float th = (it + 0.5f) * 6.2831853f / L.nTheta;
    const float z = -0.5f * L.len + (iz + 0.5f) * L.len / L.nZ;
    const float3 nl = make_float3(0.0f, cosf(th), sinf(th));
    p = xp(L.pose, make_float3(z, 0, 0) + nl * r);
    n = normalize(xv(L.pose, nl));
}

// the cell next to a point (the gas the surface touches), or -1 outside the grid
__device__ __forceinline__ int gasCell(const GasView& g, float3 p) {
    const float3 c = (p - g.origin) * (1.0f / g.h);
    const int i = int(floorf(c.x)), j = int(floorf(c.y)), k = int(floorf(c.z));
    if (i < 0 || j < 0 || k < 0 || i >= g.nx || j >= g.ny || k >= g.nz) return -1;
    return i + g.nx * (j + g.ny * k);
}

__device__ __forceinline__ float speedAt(const GasView& g, int c) {
    const int i = c % g.nx, j = (c / g.nx) % g.ny, k = c / (g.nx * g.ny);
    const float uu = 0.5f * (g.u[i + (g.nx + 1) * (j + g.ny * k)] + g.u[i + 1 + (g.nx + 1) * (j + g.ny * k)]);
    const float vv = 0.5f * (g.v[i + g.nx * (j + (g.ny + 1) * k)] + g.v[i + g.nx * (j + 1 + (g.ny + 1) * k)]);
    const float ww = 0.5f * (g.w[i + g.nx * (j + g.ny * k)] + g.w[i + g.nx * (j + g.ny * (k + 1))]);
    return sqrtf(uu * uu + vv * vv + ww * ww);
}

// ---------------------------------------------------------------------------------------------------------------
// the radiation's blocks: the soot's emission (W) and the hot surfaces' (W), summed over RAD_DIV^3 cells

__global__ void kRadGas(GasView g, const float* rad, int bx, int by, int bz, float* blocks) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= bx || j >= by || k >= bz) return;
    float s = 0.0f;
    for (int c = 0; c < RAD_DIV; ++c)
        for (int b = 0; b < RAD_DIV; ++b)
            for (int a = 0; a < RAD_DIV; ++a) {
                const int x = i * RAD_DIV + a, y = j * RAD_DIV + b, z = k * RAD_DIV + c;
                if (x < g.nx && y < g.ny && z < g.nz) s += rad[x + g.nx * (y + g.ny * z)];
            }
    blocks[2 * (i + bx * (j + by * k))] = s * g.h * g.h * g.h;
    blocks[2 * (i + bx * (j + by * k)) + 1] = 0.0f;
}

__global__ void kRadSurf(GasView g, const LogDev* logs, const int* patchLog, const unsigned int* firstPatch, const float* surfT, const float* radNow,
                         unsigned int n, float eps, int bx, int by, int bz, float* blocks, float3* light, int lx, int ly, int lz, float lightCell,
                         const float3* grey) {
    const unsigned int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= n) return;
    const int li = patchLog[p];
    if (li < 0) return;
    const LogDev& L = logs[li];
    const float Ts = surfT[p];
    if (Ts < 600.0f) return;
    const float r = radNow[p];
    float3 pos, nrm;
    patchPlace(L, int(p - firstPatch[li]), r, pos, nrm);
    const float A = 6.2831853f * r / L.nTheta * L.len / L.nZ;
    const float3 c = (pos - g.origin) * (1.0f / (g.h * RAD_DIV));
    const int i = int(floorf(c.x)), j = int(floorf(c.y)), k = int(floorf(c.z));
    if (blocks && i >= 0 && j >= 0 && k >= 0 && i < bx && j < by && k < bz) atomicAdd(&blocks[2 * (i + bx * (j + by * k)) + 1], eps * SIGMA * Ts * Ts * Ts * Ts * A);
    // the ember's light for the surfaces around it (cd, RGB): its luminance times its area, over the half sphere it
    // faces (a Lambertian emitter's mean intensity is L A / 2 over all directions)
    if (light && Ts > 700.0f) {
        const float tx = fminf(fmaxf((Ts - ATES_TABLE_T0) / ATES_TABLE_DT, 0.0f), float(ATES_TABLE_N - 1) - 0.001f);
        const int ti = int(tx);
        const float tf = tx - ti;
        const float3 Le = (grey[ti] * (1.0f - tf) + grey[ti + 1] * tf) * eps;
        const float3 lc = (pos + nrm * 0.01f - g.origin) * (1.0f / lightCell);
        const int a = int(floorf(lc.x)), b = int(floorf(lc.y)), cc = int(floorf(lc.z));
        if (a >= 0 && b >= 0 && cc >= 0 && a < lx && b < ly && cc < lz) {
            float3* dst = &light[a + lx * (b + ly * cc)];
            const float3 I = Le * (0.5f * A);
            atomicAdd(&dst->x, I.x);
            atomicAdd(&dst->y, I.y);
            atomicAdd(&dst->z, I.z);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// one patch's step

struct StepArgs {
    GasView g;
    const LogDev* logs;
    const int* patchLog;
    const unsigned int* firstPatch;
    float *T, *wood, *charM, *water;
    int* outer;
    float *surfT, *charD, *ash, *massOut, *radOut;
    const float* blocks;
    int bx, by, bz;
    AWoodConst k;
    float dtW, timeScale;
    unsigned int n;
    int nLogs;
    float bedT, bedR;   // the ash bed under the pile: its temperature (K) and reach (m)
    float* red;   // [0] pyrolysis kg/s, [1] char kg/s, [2] mass kg, [3] hottest (bits), [4] ember area
};

__global__ void kWoodStep(StepArgs a) {
    const unsigned int p = blockIdx.x * blockDim.x + threadIdx.x;
    float pyroRate = 0.0f, charRate = 0.0f, massLeft = 0.0f, hottest = 0.0f, emberA = 0.0f, hotSum = 0.0f, hotN = 0.0f;
    if (p < a.n && a.patchLog[p] >= 0) {
        const LogDev& L = a.logs[a.patchLog[p]];
        const AWoodConst& K = a.k;
        const int kind = L.kind;
        const float r0 = L.r0;
        const float wedge = 6.2831853f / L.nTheta, Lz = L.len / L.nZ;
        int out = a.outer[p];
        // the layers' geometry
        float ro[WOOD_LAYERS + 1];
        for (int i = 0; i <= WOOD_LAYERS; ++i) ro[i] = r0 * (i < WOOD_LAYERS ? layerOuter(kind, i) : 0.0f);
        float* Tl = a.T + size_t(p) * WOOD_LAYERS;
        float* Wl = a.wood + size_t(p) * WOOD_LAYERS;
        float* Cl = a.charM + size_t(p) * WOOD_LAYERS;
        float* Hl = a.water + size_t(p) * WOOD_LAYERS;
        // the surface now: the outer layer's outer radius, drawn in by what of it is gone
        auto solidFrac = [&](int i) { return fminf(Wl[i] / K.rhoWood + Cl[i] / (0.25f * K.rhoWood), 1.0f); };
        float rs = out < WOOD_LAYERS ? ro[out + 1] + (ro[out] - ro[out + 1]) * fmaxf(solidFrac(out), 0.0f) : 0.0f;
        if (out >= WOOD_LAYERS || rs < 1e-4f) {
            a.surfT[p] = a.g.T0;
            a.radOut[p] = 0.0f;
            a.massOut[p] = 0.0f;
        } else {
            float3 pos, nrm;
            patchPlace(L, int(p - a.firstPatch[a.patchLog[p]]), rs, pos, nrm);
            const float As = wedge * rs * Lz;
            // the gas it touches: a cell outward from the surface
            const int cg = gasCell(a.g, pos + nrm * (0.75f * a.g.h));
            const float Tg = cg >= 0 ? a.g.T[cg] : a.g.T0;
            const float Og = cg >= 0 ? a.g.O[cg] : a.g.Yo2;
            const float spd = cg >= 0 ? speedAt(a.g, cg) : 0.0f;
            // convection: a cylinder in cross-flow (Churchill-Bernstein's leading terms, Pr 0.7), never less than
            // free convection's
            const float Tf = 0.5f * (Tg + Tl[out]);
            const float nu = 1.5e-5f * powf(Tf / 300.0f, 1.7f), kAir = 0.026f * powf(Tf / 300.0f, 0.8f);
            const float D = 2.0f * rs;
            const float Re = spd * D / nu;
            const float Nu = fmaxf(0.3f + 0.53f * sqrtf(Re), 0.48f * powf(fmaxf(9.81f * fabsf(Tl[out] - Tg) / Tf * D * D * D / (nu * nu) * 0.7f, 1.0f), 0.25f));
            const float hc = Nu * kAir / D;
            const float rhoG = a.g.rhoT / Tg;
            const float hm = hc / (rhoG * 1100.0f);   // the mass transfer coefficient (Lewis number 1)
            // the radiation reaching it: the gas's and the other embers' blocks (their own block's surfaces left out:
            // they hold this patch itself), the night's
            // the radiation reaching it. From the flame: the gas's blocks as point sources. From the other wood: rays
            // over its half sphere (cosine-weighted, a fixed set turned per patch) against the logs' cylinders, each hit
            // the radiance of the patch it meets (its own log is convex: never met); downward rays meet the ground,
            // under the pile the ash bed (as hot as the embers above it, a share), beyond it the night's ground
            float qin = 0.0f;
            {
                const float bs = a.g.h * RAD_DIV;
                const float soft = 0.25f * bs * bs;
                for (int bk = 0; bk < a.bz; ++bk)
                    for (int bj = 0; bj < a.by; ++bj)
                        for (int bi = 0; bi < a.bx; ++bi) {
                            const float P = a.blocks[2 * (bi + a.bx * (bj + a.by * bk))];
                            if (P <= 0.0f) continue;
                            const float3 bc = a.g.origin + make_float3(bi + 0.5f, bj + 0.5f, bk + 0.5f) * bs;
                            const float3 dd = bc - pos;
                            const float d2 = dot(dd, dd);
                            const float cs = dot(dd, nrm) * rsqrtf(fmaxf(d2, 1e-8f));
                            if (cs <= 0.0f) continue;
                            qin += P * cs / (12.566371f * (d2 + soft));
                        }
                float3 tu, tv;
                makeBasis(nrm, tu, tv);
                const float spin = float((p * 2654435761u) >> 8) * (6.2831853f / 16777216.0f);
                const float bedSigma = SIGMA * a.bedT * a.bedT * a.bedT * a.bedT, skySigma = K.ambientIn;
                float Lsum = 0.0f;
                const int NR = 16;
                for (int r = 0; r < NR; ++r) {
                    // Hammersley over the disk, lifted to the hemisphere (cosine-weighted)
                    const float u1 = (r + 0.5f) / NR;
                    unsigned int bits = unsigned(r);
                    bits = (bits << 16u) | (bits >> 16u);
                    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                    const float phi = 6.2831853f * float(bits) * 2.3283064e-10f + spin;
                    const float sr = sqrtf(u1);
                    const float3 d = tu * (sr * cosf(phi)) + tv * (sr * sinf(phi)) + nrm * sqrtf(fmaxf(1.0f - u1, 0.0f));
                    float best = 1e9f, Lhit = -1.0f;
                    for (int l = 0; l < a.nLogs; ++l) {
                        if (l == a.patchLog[p]) continue;
                        const LogDev& M = a.logs[l];
                        const float3 o = pos - make_float3(M.pose[3], M.pose[7], M.pose[11]);
                        // into the log's space (the rotation's transpose)
                        const float ox = M.pose[0] * o.x + M.pose[4] * o.y + M.pose[8] * o.z, oy = M.pose[1] * o.x + M.pose[5] * o.y + M.pose[9] * o.z,
                                    oz = M.pose[2] * o.x + M.pose[6] * o.y + M.pose[10] * o.z;
                        const float dx = M.pose[0] * d.x + M.pose[4] * d.y + M.pose[8] * d.z, dy = M.pose[1] * d.x + M.pose[5] * d.y + M.pose[9] * d.z,
                                    dz = M.pose[2] * d.x + M.pose[6] * d.y + M.pose[10] * d.z;
                        const float A = dy * dy + dz * dz, B = oy * dy + oz * dz, C = oy * oy + oz * oz - M.r0 * M.r0;
                        if (A < 1e-9f) continue;
                        const float disc = B * B - A * C;
                        if (disc < 0.0f) continue;
                        const float t = (-B - sqrtf(disc)) / A;
                        if (t <= 1e-4f || t >= best) continue;
                        const float hx = ox + dx * t;
                        if (fabsf(hx) > 0.5f * M.len) continue;
                        float th = atan2f(oz + dz * t, oy + dy * t);
                        if (th < 0.0f) th += 6.2831853f;
                        const int it = min(int(th / 6.2831853f * M.nTheta), M.nTheta - 1), iz = min(max(int((hx / M.len + 0.5f) * M.nZ), 0), M.nZ - 1);
                        const float Th = a.surfT[a.firstPatch[l] + unsigned(iz * M.nTheta + it)];
                        best = t;
                        Lhit = K.emissivity * SIGMA * Th * Th * Th * Th + (1.0f - K.emissivity) * skySigma;
                    }
                    if (Lhit < 0.0f) {
                        if (d.y < 0.0f) {
                            const float t = -pos.y / d.y;
                            const float gx = pos.x + d.x * t, gz = pos.z + d.z * t;
                            Lhit = (gx * gx + gz * gz < a.bedR * a.bedR) ? fmaxf(bedSigma, skySigma) : skySigma;
                        } else {
                            Lhit = skySigma;
                        }
                    }
                    Lsum += Lhit;
                }
                qin += Lsum / NR;   // (E = pi * mean radiance, and each Lhit is already pi * radiance)
            }
            // the time's own steps (the conduction is implicit; the reactions are integrated exactly over each)
            const float dtW = a.dtW;
            const int nsub = max(1, int(ceilf(dtW / 0.2f)));
            const float dt = dtW / nsub;
            float volat = 0.0f, tarOut = 0.0f, charOx = 0.0f, coOut = 0.0f, o2In = 0.0f, heatGas = 0.0f;
            float ashT = a.ash[p];
            for (int s = 0; s < nsub; ++s) {
                // --- the char at the surface burns with the oxygen that reaches it
                const float Ts = Tl[out];
                float qOx = 0.0f;
                if (Cl[out] > 1e-3f && Og > 1e-4f) {
                    const float ks = K.kinA * __expf(-K.kinTa / Ts);
                    const float keff = 1.0f / (1.0f / fmaxf(ks, 1e-9f) + 1.0f / fmaxf(hm, 1e-6f) + ashT / K.ashDiff);
                    const float rCO = 2500.0f * __expf(-6240.0f / Ts);
                    const float fCO = rCO / (1.0f + rCO);
                    const float sC = 2.667f * (1.0f - fCO) + 1.333f * fCO;
                    float mC = rhoG * Og * keff / sC;   // kg of carbon per m^2 per s
                    const float Vout = 0.5f * wedge * (ro[out] * ro[out] - ro[out + 1] * ro[out + 1]) * Lz;
                    mC = fminf(mC, Cl[out] * Vout / (As * dt));
                    const float hcC = (1.0f - fCO) * 32.8e6f + fCO * 9.2e6f;
                    qOx = mC * hcC;
                    Cl[out] -= mC * As * dt / Vout;
                    // (kg and J over the sub-step: the rates are these over dtW)
                    charOx += mC * As * dt;
                    coOut += mC * fCO * (28.0f / 12.0f) * As * dt;
                    o2In += mC * sC * As * dt;
                    heatGas += (1.0f - K.charHeatShare) * qOx * As * dt;
                    ashT += mC * K.ashInChar / K.rhoAsh * dt;
                }
                // a draught lifts the ash; ash on a side facing down falls (a slow loss everywhere: it settles off)
                ashT = fmaxf(ashT - dt * (fmaxf(spd - K.ashLift, 0.0f) * 2e-4f + 2e-6f + fmaxf(-nrm.y, 0.0f) * 2e-5f), 0.0f);
                // --- conduction with the surface's balance, implicit (Thomas) over the layers left
                float cA[WOOD_LAYERS], cB[WOOD_LAYERS], cC[WOOD_LAYERS], cD[WOOD_LAYERS];
                for (int i = out; i < WOOD_LAYERS; ++i) {
                    const float V = 0.5f * wedge * (ro[i] * ro[i] - ro[i + 1] * ro[i + 1]) * Lz;
                    const float Ti = Tl[i];
                    const float cw = fminf(1500.0f + 2.3f * (Ti - 300.0f), 2600.0f), cc = fminf(700.0f + 1.0f * (Ti - 300.0f), 1600.0f);
                    const float Cap = (Wl[i] * cw + Cl[i] * cc + Hl[i] * 4186.0f + 3000.0f) * V;   // J/K (+ a little: the gas in the pores)
                    float diag = Cap / dt, rhs = Cap / dt * Ti;
                    cA[i] = cC[i] = 0.0f;
                    if (i + 1 < WOOD_LAYERS) {
                        const float rb = ro[i + 1];
                        const float Tm = 0.5f * (Ti + Tl[i + 1]);
                        const float kw = 0.12f + 0.0002f * (Tm - 300.0f), kc = 0.08f + 1.2e-10f * Tm * Tm * Tm;
                        const float wsum = Wl[i] + Cl[i] + Wl[i + 1] + Cl[i + 1] + 1e-6f;
                        const float kf = ((Wl[i] + Wl[i + 1]) * kw + (Cl[i] + Cl[i + 1]) * kc) / wsum + 0.02f;
                        const float dist = 0.5f * (ro[i] - ro[i + 2 <= WOOD_LAYERS ? i + 2 : WOOD_LAYERS]);
                        const float G = kf * wedge * rb * Lz / fmaxf(dist, 1e-5f);
                        diag += G;
                        cC[i] = -G;
                    }
                    if (i > out) {
                        const float G = -cC[i - 1];
                        diag += G;
                        cA[i] = -G;
                    }
                    if (i == out) {
                        // the surface: absorbed radiation, convection, the char's heat, its own emission (linearized)
                        const float T3 = Ti * Ti * Ti;
                        diag += (K.emissivity * SIGMA * 4.0f * T3 + hc) * As;
                        rhs += (K.emissivity * qin + hc * Tg + K.charHeatShare * qOx + 3.0f * K.emissivity * SIGMA * T3 * Ti) * As;
                    }
                    cB[i] = diag;
                    cD[i] = rhs;
                }
                for (int i = out + 1; i < WOOD_LAYERS; ++i) {
                    const float m = cA[i] / cB[i - 1];
                    cB[i] -= m * cC[i - 1];
                    cD[i] -= m * cD[i - 1];
                }
                Tl[WOOD_LAYERS - 1] = cD[WOOD_LAYERS - 1] / cB[WOOD_LAYERS - 1];
                for (int i = WOOD_LAYERS - 2; i >= out; --i) Tl[i] = (cD[i] - cC[i] * Tl[i + 1]) / cB[i];
                // the convective heat the surface gave the gas
                heatGas += hc * (Tl[out] - Tg) * As * dt;
                // --- drying and pyrolysis in each layer
                for (int i = out; i < WOOD_LAYERS; ++i) {
                    const float V = 0.5f * wedge * (ro[i] * ro[i] - ro[i + 1] * ro[i + 1]) * Lz;
                    float Ti = Tl[i];
                    const float cw = fminf(1500.0f + 2.3f * (Ti - 300.0f), 2600.0f), cc = fminf(700.0f + 1.0f * (Ti - 300.0f), 1600.0f);
                    const float rc = Wl[i] * cw + Cl[i] * cc + Hl[i] * 4186.0f + 3000.0f;
                    if (Hl[i] > 0.0f && Ti > 373.0f) {
                        const float E = (Ti - 373.0f) * rc;
                        const float dW = fminf(Hl[i], E / K.latent);
                        Hl[i] -= dW;
                        Ti = 373.0f + (E - dW * K.latent) / rc;
                    }
                    if (Wl[i] > 0.0f && Ti > 450.0f) {
                        const float iRT = 1.0f / (RGAS * Ti);
                        const float kg = 4.38e9f * __expf(-152700.0f * iRT), kt = 1.08e10f * __expf(-148000.0f * iRT), kc = 3.27e6f * __expf(-111700.0f * iRT);
                        const float kt0 = kg + kt + kc;
                        const float dw = Wl[i] * (1.0f - __expf(-kt0 * dt));
                        Wl[i] -= dw;
                        Cl[i] += dw * kc / kt0;
                        const float vol = dw * (kg + kt) / kt0 * V;
                        volat += vol;
                        tarOut += dw * kt / kt0 * V;
                        Ti -= K.hPyro * dw / rc;
                    }
                    Tl[i] = Ti;
                }
                // the outer layer gone (its char burnt): the surface steps in
                if (out < WOOD_LAYERS - 1 && solidFrac(out) < 0.03f && Wl[out] < 1.0f) {
                    const float carry = Tl[out];
                    ++out;
                    Tl[out] = fmaxf(Tl[out], 0.5f * (Tl[out] + carry));
                }
            }
            a.outer[p] = out;
            a.ash[p] = ashT;
            // what went into the gas, at its real rates (the wood's time ran timeScale times faster)
            const float inv = 1.0f / dtW;
            if (cg >= 0) {
                const float Tsurf = Tl[out];
                if (volat > 0.0f) {
                    atomicAdd(&a.g.src.fuel[cg], volat * inv);
                    atomicAdd(&a.g.src.massT[cg], volat * inv * Tsurf);
                }
                if (coOut > 0.0f) {
                    atomicAdd(&a.g.src.co[cg], coOut * inv);
                    atomicAdd(&a.g.src.massT[cg], coOut * inv * Tsurf);
                }
                if (o2In > 0.0f) atomicAdd(&a.g.src.o2[cg], o2In * inv);
                atomicAdd(&a.g.src.heat[cg], heatGas * inv);
                atomicMax(reinterpret_cast<int*>(&a.g.src.ignite[cg]), __float_as_int(Tsurf));
            }
            (void)tarOut;
            pyroRate = volat * inv;
            charRate = charOx * inv;
            // what is left, the surface for the eye and the body
            rs = out < WOOD_LAYERS ? ro[out + 1] + (ro[out] - ro[out + 1]) * fmaxf(solidFrac(out), 0.0f) : 0.0f;
            float mass = 0.0f, cdepth = 0.0f;
            bool inChar = true;
            for (int i = out; i < WOOD_LAYERS; ++i) {
                const float V = 0.5f * wedge * (ro[i] * ro[i] - ro[i + 1] * ro[i + 1]) * Lz;
                mass += (Wl[i] + Cl[i]) * V;
                if (inChar) {
                    if (Wl[i] < 0.5f * K.rhoWood) cdepth += ro[i] - ro[i + 1];
                    else inChar = false;
                }
            }
            a.massOut[p] = mass;
            a.radOut[p] = rs;
            a.charD[p] = cdepth;
            a.surfT[p] = Tl[out];
            massLeft = mass;
            hottest = Tl[out];
            if (Tl[out] > 800.0f) emberA = As;
            if (Tl[out] > 700.0f && pos.y < 0.08f) {
                hotSum = Tl[out];
                hotN = 1.0f;
            }
        }
    }
    for (int o = 16; o > 0; o >>= 1) {
        pyroRate += __shfl_down_sync(0xffffffffu, pyroRate, o);
        charRate += __shfl_down_sync(0xffffffffu, charRate, o);
        massLeft += __shfl_down_sync(0xffffffffu, massLeft, o);
        emberA += __shfl_down_sync(0xffffffffu, emberA, o);
        hotSum += __shfl_down_sync(0xffffffffu, hotSum, o);
        hotN += __shfl_down_sync(0xffffffffu, hotN, o);
        hottest = fmaxf(hottest, __shfl_down_sync(0xffffffffu, hottest, o));
    }
    if (threadIdx.x % 32 == 0) {
        atomicAdd(&a.red[0], pyroRate);
        atomicAdd(&a.red[1], charRate);
        atomicAdd(&a.red[2], massLeft);
        atomicMax(reinterpret_cast<int*>(&a.red[3]), __float_as_int(hottest));
        atomicAdd(&a.red[4], emberA);
        atomicAdd(&a.red[5], hotSum);
        atomicAdd(&a.red[6], hotN);
    }
}

// the logs' cells in the gas: inside a log's surface (its rings' radii), thicker than a cell
__global__ void kSolids(GasView g, const LogDev* logs, const float* ringR, const int* ringFirst, int nLogs, unsigned char* solid) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= g.nx || j >= g.ny || k >= g.nz) return;
    const float3 p = g.origin + make_float3(i + 0.5f, j + 0.5f, k + 0.5f) * g.h;
    unsigned char s = 0;
    for (int l = 0; l < nLogs && !s; ++l) {
        const LogDev& L = logs[l];
        if (L.kind == 0 || L.r0 < 0.6f * g.h) continue;
        // into the log's space (the pose is a rotation and a translation: its transpose undoes it)
        const float3 d = p - make_float3(L.pose[3], L.pose[7], L.pose[11]);
        const float x = L.pose[0] * d.x + L.pose[4] * d.y + L.pose[8] * d.z;
        if (fabsf(x) > 0.5f * L.len) continue;
        const float y = L.pose[1] * d.x + L.pose[5] * d.y + L.pose[9] * d.z, z = L.pose[2] * d.x + L.pose[6] * d.y + L.pose[10] * d.z;
        const int iz = min(max(int((x / L.len + 0.5f) * L.nZ), 0), L.nZ - 1);
        const float r = ringR[ringFirst[l] + iz];
        if (y * y + z * z < r * r) s = 1;
    }
    solid[i + g.nx * (j + g.ny * k)] = s;
}

}   // namespace

// ---------------------------------------------------------------------------------------------------------------

std::array<float, 12> ALog::pose() const {
    const float w = q.x, x = q.y, y = q.z, z = q.w;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), pos.x,
            2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), pos.y,
            2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), pos.z};
}

bool AtesWood::init(unsigned int cap, cudaStream_t st) {
    release();
    st_ = st;
    capacity = cap;
    auto F = [&](size_t n) {
        float* p = nullptr;
        WOOD_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(float)));
        WOOD_CHECK(cudaMemset(p, 0, n * sizeof(float)));
        owned_.push_back(p);
        return p;
    };
    surfT = F(cap);
    charD = F(cap);
    ash = F(cap);
    qrad = F(cap);
    massOut = F(cap);
    radOut = F(cap);
    T = F(size_t(cap) * WOOD_LAYERS);
    wood = F(size_t(cap) * WOOD_LAYERS);
    charM = F(size_t(cap) * WOOD_LAYERS);
    water = F(size_t(cap) * WOOD_LAYERS);
    WOOD_CHECK(cudaMalloc(reinterpret_cast<void**>(&outer), cap * sizeof(int)));
    owned_.push_back(outer);
    WOOD_CHECK(cudaMalloc(reinterpret_cast<void**>(&patchLog_), cap * sizeof(int)));
    WOOD_CHECK(cudaMemset(patchLog_, 0xff, cap * sizeof(int)));
    owned_.push_back(patchLog_);
    WOOD_CHECK(cudaEventCreate(&e0_));
    WOOD_CHECK(cudaEventCreate(&e1_));
    patches = 0;
    logs.clear();
    return true;
}

int AtesWood::add(const ALog& proto, float burnt, float surfaceT) {
    ALog L = proto;
    const unsigned int n = unsigned(L.nTheta * L.nZ);
    if (patches + n > capacity) return -1;
    L.firstPatch = patches;
    L.seed = unsigned(logs.size() * 2654435761u + 12345u);
    L.radius = L.r0;
    L.ringRadius.assign(size_t(L.nZ), L.r0);
    // the layers: dry wood with its water at the night's temperature; a charred skin `burnt` deep, hot at its surface
    std::vector<float> t(size_t(n) * WOOD_LAYERS), w(t.size()), c(t.size()), h(t.size());
    std::vector<int> o(n, 0), pl(n, int(logs.size()));
    std::vector<float> st(n, surfaceT), cd(n, burnt), rr(n, L.r0), mm(n, 0.0f);
    for (unsigned int p = 0; p < n; ++p)
        for (int i = 0; i < WOOD_LAYERS; ++i) {
            const size_t x = size_t(p) * WOOD_LAYERS + i;
            const float depth = L.r0 * (1.0f - layerOuter(L.kind, i));   // the layer's outer face below the surface
            const float depthIn = L.r0 * (1.0f - (i + 1 < WOOD_LAYERS ? layerOuter(L.kind, i + 1) : 0.0f));
            const float mid = 0.5f * (depth + depthIn);
            const bool charred = mid < burnt;
            w[x] = charred ? 0.0f : k.rhoWood;
            c[x] = charred ? 0.25f * k.rhoWood : 0.0f;
            h[x] = charred ? 0.0f : k.rhoWood * k.moisture;
            // a hot skin over a cooler core: the profile of a heated slab (an error function, ~3 char depths deep)
            const float decay = burnt > 0.0f ? std::exp(-mid / std::max(2.0f * burnt, 1e-4f)) : 0.0f;
            t[x] = 288.0f + (surfaceT - 288.0f) * (burnt > 0.0f ? decay : (i == 0 ? 1.0f : 0.0f));
            if (!charred && t[x] > 373.0f) h[x] = 0.0f;
        }
    const size_t off = size_t(patches) * WOOD_LAYERS;
    WOOD_CHECK(cudaMemcpy(T + off, t.data(), t.size() * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(wood + off, w.data(), w.size() * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(charM + off, c.data(), c.size() * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(water + off, h.data(), h.size() * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(outer + patches, o.data(), n * sizeof(int), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(patchLog_ + patches, pl.data(), n * sizeof(int), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(surfT + patches, st.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(charD + patches, cd.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemcpy(radOut + patches, rr.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    WOOD_CHECK(cudaMemset(ash + patches, 0, n * sizeof(float)));
    patches += n;
    logs.push_back(L);
    return int(logs.size()) - 1;
}

namespace {
std::vector<LogDev> logDevs(const std::vector<ALog>& logs) {
    std::vector<LogDev> v(logs.size());
    for (size_t i = 0; i < logs.size(); ++i) {
        const ALog& L = logs[i];
        v[i].r0 = L.r0;
        v[i].len = L.len;
        v[i].nTheta = L.nTheta;
        v[i].nZ = L.nZ;
        v[i].kind = L.kind;
        const auto m = L.pose();
        for (int k = 0; k < 12; ++k) v[i].pose[k] = m[k];
    }
    return v;
}
GasView view(const AtesGas& gas) {
    GasView g{};
    g.nx = gas.nx;
    g.ny = gas.ny;
    g.nz = gas.nz;
    g.h = gas.h;
    g.origin = gas.origin;
    g.T = gas.T;
    g.O = gas.O;
    g.u = gas.u;
    g.v = gas.v;
    g.w = gas.w;
    g.src = gas.src;
    g.T0 = gas.k.T0;
    g.Yo2 = gas.k.Yo2;
    g.rhoT = gas.k.rhoT;
    return g;
}
}   // namespace

void AtesWood::step(float dtWood, float timeScale, AtesGas& gas) {
    if (logs.empty() || patches == 0) return;
    const std::vector<LogDev> ld = logDevs(logs);
    std::vector<unsigned int> first(logs.size());
    for (size_t i = 0; i < logs.size(); ++i) first[i] = logs[i].firstPatch;
    // (small per-frame tables: a fresh allocation is simpler than tracking their size)
    LogDev* dLogs = nullptr;
    unsigned int* dFirst = nullptr;
    float* red = nullptr;
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dLogs), ld.size() * sizeof(LogDev), st_));
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dFirst), first.size() * sizeof(unsigned int), st_));
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&red), 8 * sizeof(float), st_));
    WOOD_CHECK(cudaMemcpyAsync(dLogs, ld.data(), ld.size() * sizeof(LogDev), cudaMemcpyHostToDevice, st_));
    WOOD_CHECK(cudaMemcpyAsync(dFirst, first.data(), first.size() * sizeof(unsigned int), cudaMemcpyHostToDevice, st_));
    WOOD_CHECK(cudaMemsetAsync(red, 0, 8 * sizeof(float), st_));
    const GasView g = view(gas);
    rbx_ = (gas.nx + RAD_DIV - 1) / RAD_DIV;
    rby_ = (gas.ny + RAD_DIV - 1) / RAD_DIV;
    rbz_ = (gas.nz + RAD_DIV - 1) / RAD_DIV;
    if (!radBlocks_) {
        WOOD_CHECK(cudaMalloc(reinterpret_cast<void**>(&radBlocks_), 2 * size_t(rbx_) * rby_ * rbz_ * sizeof(float)));
        owned_.push_back(radBlocks_);
    }
    WOOD_CHECK(cudaEventRecord(e0_, st_));
    kRadGas<<<dim3((rbx_ + 7) / 8, (rby_ + 7) / 8, rbz_), dim3(8, 8), 0, st_>>>(g, gas.rad, rbx_, rby_, rbz_, radBlocks_);
    kRadSurf<<<(patches + 127) / 128, 128, 0, st_>>>(g, dLogs, patchLog_, dFirst, surfT, radOut, patches, k.emissivity, rbx_, rby_, rbz_, radBlocks_, nullptr, 0, 0,
                                                       0, 0.0f, nullptr);
    StepArgs a{};
    a.g = g;
    a.logs = dLogs;
    a.patchLog = patchLog_;
    a.firstPatch = dFirst;
    a.T = T;
    a.wood = wood;
    a.charM = charM;
    a.water = water;
    a.outer = outer;
    a.surfT = surfT;
    a.charD = charD;
    a.ash = ash;
    a.massOut = massOut;
    a.radOut = radOut;
    a.blocks = radBlocks_;
    a.bx = rbx_;
    a.by = rby_;
    a.bz = rbz_;
    a.k = k;
    a.dtW = dtWood;
    a.timeScale = timeScale;
    a.n = patches;
    a.nLogs = int(logs.size());
    a.bedT = bedT;
    a.bedR = bedR;
    a.red = red;
    kWoodStep<<<(patches + 127) / 128, 128, 0, st_>>>(a);
    WOOD_CHECK(cudaEventRecord(e1_, st_));
    float r[8];
    WOOD_CHECK(cudaMemcpyAsync(r, red, sizeof(r), cudaMemcpyDeviceToHost, st_));
    WOOD_CHECK(cudaFreeAsync(dLogs, st_));
    WOOD_CHECK(cudaFreeAsync(dFirst, st_));
    WOOD_CHECK(cudaFreeAsync(red, st_));
    WOOD_CHECK(cudaStreamSynchronize(st_));
    stats.pyrolysis = r[0];
    stats.charBurn = r[1];
    stats.mass = r[2];
    stats.hottest = r[3];
    stats.emberArea = r[4];
    // the ash bed under the pile follows the embers lying low on it (a share of their mean over the night's air: ash
    // between them, cooler than their faces), over a few seconds of the wood's time
    const float hotMean = r[6] > 0.0f ? r[5] / r[6] : 288.0f;
    const float target = 288.0f + 0.75f * (hotMean - 288.0f);
    bedT += (target - bedT) * (1.0f - std::exp(-dtWood / 5.0f));
    cudaEventElapsedTime(&stats.stepMs, e0_, e1_);
}

// the embers' light into the gas's light blocks (after the gas's pack: they light the surfaces too)
void AtesWood::emberLight(const AtesGas& gas, float3* light, int lx, int ly, int lz, float lightCell, const float3* grey) {
    if (logs.empty() || patches == 0 || !light) return;
    const std::vector<LogDev> ld = logDevs(logs);
    std::vector<unsigned int> first(logs.size());
    for (size_t i = 0; i < logs.size(); ++i) first[i] = logs[i].firstPatch;
    LogDev* dLogs = nullptr;
    unsigned int* dFirst = nullptr;
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dLogs), ld.size() * sizeof(LogDev), st_));
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dFirst), first.size() * sizeof(unsigned int), st_));
    WOOD_CHECK(cudaMemcpyAsync(dLogs, ld.data(), ld.size() * sizeof(LogDev), cudaMemcpyHostToDevice, st_));
    WOOD_CHECK(cudaMemcpyAsync(dFirst, first.data(), first.size() * sizeof(unsigned int), cudaMemcpyHostToDevice, st_));
    kRadSurf<<<(patches + 127) / 128, 128, 0, st_>>>(view(gas), dLogs, patchLog_, dFirst, surfT, radOut, patches, k.emissivity, 0, 0, 0, nullptr, light, lx, ly, lz,
                                                       lightCell, grey);
    WOOD_CHECK(cudaFreeAsync(dLogs, st_));
    WOOD_CHECK(cudaFreeAsync(dFirst, st_));
}

void AtesWood::markSolids(AtesGas& gas) {
    if (logs.empty()) return;
    const std::vector<LogDev> ld = logDevs(logs);
    std::vector<float> rings;
    std::vector<int> firsts;
    for (const ALog& L : logs) {
        firsts.push_back(int(rings.size()));
        for (int z = 0; z < L.nZ; ++z) rings.push_back(z < int(L.ringRadius.size()) ? L.ringRadius[size_t(z)] : L.r0);
    }
    LogDev* dLogs = nullptr;
    float* dR = nullptr;
    int* dF = nullptr;
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dLogs), ld.size() * sizeof(LogDev), st_));
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dR), rings.size() * sizeof(float), st_));
    WOOD_CHECK(cudaMallocAsync(reinterpret_cast<void**>(&dF), firsts.size() * sizeof(int), st_));
    WOOD_CHECK(cudaMemcpyAsync(dLogs, ld.data(), ld.size() * sizeof(LogDev), cudaMemcpyHostToDevice, st_));
    WOOD_CHECK(cudaMemcpyAsync(dR, rings.data(), rings.size() * sizeof(float), cudaMemcpyHostToDevice, st_));
    WOOD_CHECK(cudaMemcpyAsync(dF, firsts.data(), firsts.size() * sizeof(int), cudaMemcpyHostToDevice, st_));
    const GasView g = view(gas);
    kSolids<<<dim3((gas.nx + 31) / 32, (gas.ny + 3) / 4, gas.nz), dim3(32, 4), 0, st_>>>(g, dLogs, dR, dF, int(ld.size()), gas.solid);
    WOOD_CHECK(cudaFreeAsync(dLogs, st_));
    WOOD_CHECK(cudaFreeAsync(dR, st_));
    WOOD_CHECK(cudaFreeAsync(dF, st_));
}

void AtesWood::readBack() {
    if (patches == 0) return;
    std::vector<float> r(patches), m(patches);
    WOOD_CHECK(cudaStreamSynchronize(st_));
    WOOD_CHECK(cudaMemcpy(r.data(), radOut, patches * sizeof(float), cudaMemcpyDeviceToHost));
    WOOD_CHECK(cudaMemcpy(m.data(), massOut, patches * sizeof(float), cudaMemcpyDeviceToHost));
    for (ALog& L : logs) {
        double ms = 0.0, rsum = 0.0;
        L.ringRadius.assign(size_t(L.nZ), 0.0f);
        for (int z = 0; z < L.nZ; ++z) {
            double rr = 0.0;
            for (int t = 0; t < L.nTheta; ++t) {
                const unsigned int p = L.firstPatch + unsigned(z * L.nTheta + t);
                rr += r[p];
                ms += m[p];
            }
            L.ringRadius[size_t(z)] = float(rr / L.nTheta);
            rsum += rr / L.nTheta;
        }
        L.mass = float(ms);
        L.radius = float(rsum / L.nZ);
        L.alive = L.mass > 1e-4f;
    }
}

std::vector<ALogView> AtesWood::views() const {
    std::vector<ALogView> v(logs.size());
    for (size_t i = 0; i < logs.size(); ++i) {
        v[i].surfT = surfT;
        v[i].charD = charD;
        v[i].ash = ash;
        v[i].firstPatch = logs[i].firstPatch;
        v[i].nTheta = logs[i].nTheta;
        v[i].nZ = logs[i].nZ;
        v[i].r0 = logs[i].r0;
        v[i].len = logs[i].len;
        v[i].seed = logs[i].seed;
    }
    return v;
}

void AtesWood::release() {
    if (st_) cudaStreamSynchronize(st_);
    for (void* p : owned_) cudaFree(p);
    owned_.clear();
    radBlocks_ = nullptr;
    if (e0_) cudaEventDestroy(e0_);
    if (e1_) cudaEventDestroy(e1_);
    e0_ = e1_ = nullptr;
    surfT = charD = ash = qrad = T = wood = charM = water = massOut = radOut = nullptr;
    outer = nullptr;
    patchLog_ = nullptr;
    patches = 0;
    logs.clear();
}

// the side follows each patch's radius (the vertex the mean of the four patches around it); u is metres around (at the
// first radius), v metres along: the bark's texture keeps its size, the patch is found from them (ates.cu)
void buildLogMesh(const ALog& L, const std::vector<float>& pr, ALogMesh& out) {
    OMeshCPU& s = out.side;
    s = OMeshCPU();
    const int nt = L.nTheta, nz = L.nZ;
    auto rad = [&](int t, int z) {
        t = (t % nt + nt) % nt;
        z = std::clamp(z, 0, nz - 1);
        const size_t p = size_t(L.firstPatch) + size_t(z * nt + t);
        return p < pr.size() ? pr[p] : L.r0;
    };
    for (int z = 0; z <= nz; ++z)
        for (int t = 0; t <= nt; ++t) {
            const float r = 0.25f * (rad(t - 1, z - 1) + rad(t, z - 1) + rad(t - 1, z) + rad(t, z));
            const float th = 6.2831853f * t / nt;
            const float x = -0.5f * L.len + L.len * z / nz;
            s.pos.push_back(make_float3(x, r * std::cos(th), r * std::sin(th)));
            s.nrm.push_back(make_float3(0, std::cos(th), std::sin(th)));
            s.uv.push_back(make_float2(th * L.r0, x));
        }
    for (int z = 0; z < nz; ++z)
        for (int t = 0; t < nt; ++t) {
            const unsigned a = unsigned(z * (nt + 1) + t), b = a + 1, c = a + unsigned(nt + 1), d = c + 1;
            s.idx.push_back(make_uint3(a, c, b));
            s.idx.push_back(make_uint3(b, c, d));
        }
    s.uvScale = 1.0f;
    // the two ends: a fan over the end ring's radii, its uv the side's (the angle around, the end's place along)
    OMeshCPU& e = out.ends;
    e = OMeshCPU();
    for (int side = 0; side < 2; ++side) {
        const int z = side == 0 ? 0 : nz - 1;
        const float x = side == 0 ? -0.5f * L.len : 0.5f * L.len;
        const float sx = side == 0 ? -1.0f : 1.0f;
        const unsigned c0 = unsigned(e.pos.size());
        e.pos.push_back(make_float3(x, 0, 0));
        e.nrm.push_back(make_float3(sx, 0, 0));
        e.uv.push_back(make_float2(0.0f, x - sx * 0.001f));
        for (int t = 0; t <= nt; ++t) {
            const float th = 6.2831853f * t / nt;
            const float r = 0.5f * (rad(t - 1, z) + rad(t, z));
            e.pos.push_back(make_float3(x, r * std::cos(th), r * std::sin(th)));
            e.nrm.push_back(make_float3(sx, 0, 0));
            e.uv.push_back(make_float2(th * L.r0, x - sx * 0.001f));
        }
        for (int t = 0; t < nt; ++t) {
            const unsigned a = c0 + 1 + unsigned(t), b = a + 1;
            if (side == 0) e.idx.push_back(make_uint3(c0, a, b));
            else e.idx.push_back(make_uint3(c0, b, a));
        }
    }
    e.uvScale = 1.0f;
}
