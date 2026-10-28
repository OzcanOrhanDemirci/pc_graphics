// Scene 12 "Ateş": the gas solver (ates_gas.h). One step: the sources and the reactions in each cell (the heat, the
// expansion it causes, the soot, the light it radiates away), the velocity carried by itself, the buoyancy, the
// projection (multigrid V-cycles on the pressure, open sides and top, the ground and the solids closed), the scalars
// carried by the new velocity.
#include "scenes/ates/ates_gas.h"
#include "scenes/ates/ates_shared.h"

#include <cuda_fp16.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define GAS_CHECK(x)                                                                                    \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

namespace {

#define kSigma 5.670374e-8f

struct Dims {
    int nx, ny, nz;
    float h;
};

__device__ __forceinline__ int cid(const Dims& d, int i, int j, int k) { return i + d.nx * (j + d.ny * k); }
__device__ __forceinline__ int uid(const Dims& d, int i, int j, int k) { return i + (d.nx + 1) * (j + d.ny * k); }
__device__ __forceinline__ int vid(const Dims& d, int i, int j, int k) { return i + d.nx * (j + (d.ny + 1) * k); }
__device__ __forceinline__ int wid(const Dims& d, int i, int j, int k) { return i + d.nx * (j + d.ny * k); }

// trilinear over an array of n = (ax, ay, az) values at integer coordinates, clamped at its ends
__device__ __forceinline__ float trilin(const float* __restrict__ a, int ax, int ay, int az, float x, float y, float z) {
    x = fminf(fmaxf(x, 0.0f), float(ax - 1));
    y = fminf(fmaxf(y, 0.0f), float(ay - 1));
    z = fminf(fmaxf(z, 0.0f), float(az - 1));
    const int i0 = min(int(x), ax - 2 >= 0 ? ax - 2 : 0), j0 = min(int(y), ay - 2 >= 0 ? ay - 2 : 0), k0 = min(int(z), az - 2 >= 0 ? az - 2 : 0);
    const float fx = x - i0, fy = y - j0, fz = z - k0;
    const int sx = ax > 1 ? 1 : 0, sy = ax * (ay > 1 ? 1 : 0), sz = ax * ay * (az > 1 ? 1 : 0);
    const int b = i0 + ax * (j0 + ay * k0);
    const float c00 = __ldg(a + b) * (1 - fx) + __ldg(a + b + sx) * fx, c10 = __ldg(a + b + sy) * (1 - fx) + __ldg(a + b + sy + sx) * fx;
    const float c01 = __ldg(a + b + sz) * (1 - fx) + __ldg(a + b + sz + sx) * fx, c11 = __ldg(a + b + sz + sy) * (1 - fx) + __ldg(a + b + sz + sy + sx) * fx;
    return (c00 * (1 - fy) + c10 * fy) * (1 - fz) + (c01 * (1 - fy) + c11 * fy) * fz;
}

// the velocity at a point in cell units (cell (i, j, k)'s centre at i + 0.5 ...)
struct Vel {
    const float *u, *v, *w;
};
__device__ __forceinline__ float3 velAt(const Dims& d, const Vel& V, float3 p) {
    return make_float3(trilin(V.u, d.nx + 1, d.ny, d.nz, p.x, p.y - 0.5f, p.z - 0.5f), trilin(V.v, d.nx, d.ny + 1, d.nz, p.x - 0.5f, p.y, p.z - 0.5f),
                       trilin(V.w, d.nx, d.ny, d.nz + 1, p.x - 0.5f, p.y - 0.5f, p.z));
}

// a scalar at a point in cell units: outside the sides and the top the night's air comes in (amb), under the floor the
// first layer
__device__ __forceinline__ float scalarAt(const Dims& d, const float* __restrict__ a, float amb, float3 p, float& lo, float& hi) {
    const float x = p.x - 0.5f, y = fminf(fmaxf(p.y - 0.5f, 0.0f), float(d.ny)), z = p.z - 0.5f;   // (above the top: the air)
    const int i0 = int(floorf(x)), j0 = min(int(floorf(y)), d.ny - 1), k0 = int(floorf(z));
    const float fx = x - i0, fy = y - j0, fz = z - k0;
    float c[8];
    lo = 1e30f;
    hi = -1e30f;
#pragma unroll
    for (int n = 0; n < 8; ++n) {
        const int i = i0 + (n & 1), j = min(j0 + ((n >> 1) & 1), d.ny), k = k0 + (n >> 2);
        const bool in = i >= 0 && i < d.nx && k >= 0 && k < d.nz && j < d.ny;
        c[n] = in ? __ldg(a + cid(d, i, j, k)) : amb;
        lo = fminf(lo, c[n]);
        hi = fmaxf(hi, c[n]);
    }
    const float x0 = (c[0] * (1 - fx) + c[1] * fx) * (1 - fy) + (c[2] * (1 - fx) + c[3] * fx) * fy;
    const float x1 = (c[4] * (1 - fx) + c[5] * fx) * (1 - fy) + (c[6] * (1 - fx) + c[7] * fx) * fy;
    return x0 * (1 - fz) + x1 * fz;
}

struct Scalars {
    float *T, *F, *C, *O, *S, *M;
};
struct CScalars {
    const float *T, *F, *C, *O, *S, *M;
};

__device__ __forceinline__ float3 backtrace(const Dims& d, const Vel& V, float3 p, float dt) {
    const float s = dt / d.h;
    const float3 v0 = velAt(d, V, p);
    const float3 mid = p - v0 * (0.5f * s);
    return p - velAt(d, V, mid) * s;
}

// ---------------------------------------------------------------------------------------------------------------
// the cells: sources and reactions

struct CellArgs {
    Dims d;
    float3 origin;
    AGasConst k;
    Scalars s;
    Vel V;
    AGasSources src;
    const unsigned char* solid;
    const float* Tprev;   // the temperatures before this step (the neighbours' flame)
    float* div;       // out: the expansion (1/s)
    float* q;         // out: heat release (W/m^3)
    float* rad;       // out: the soot's radiation (W/m^3)
    float* nut;       // out: the sub-grid eddy viscosity (Smagorinsky: (Cs h)^2 |S|, m^2/s)
    float* red;       // reductions: [0] hrr, [1] rad, [2] fuel in, [3] max T (as int bits)
    AGasBurner burner;
    float dt;
};

__device__ __forceinline__ float strainAt(const Dims& d, const Vel& V, int i, int j, int k) {
    // the cell-centred velocity's gradient by central differences of the faces' averages (one-sided at the ends)
    auto uc = [&](int a, int b, int c) {
        a = min(max(a, 0), d.nx - 1);
        b = min(max(b, 0), d.ny - 1);
        c = min(max(c, 0), d.nz - 1);
        return make_float3(0.5f * (V.u[uid(d, a, b, c)] + V.u[uid(d, a + 1, b, c)]), 0.5f * (V.v[vid(d, a, b, c)] + V.v[vid(d, a, b + 1, c)]),
                           0.5f * (V.w[wid(d, a, b, c)] + V.w[wid(d, a, b, c + 1)]));
    };
    const float ih = 1.0f / d.h;
    const float dudx = (V.u[uid(d, i + 1, j, k)] - V.u[uid(d, i, j, k)]) * ih;
    const float dvdy = (V.v[vid(d, i, j + 1, k)] - V.v[vid(d, i, j, k)]) * ih;
    const float dwdz = (V.w[wid(d, i, j, k + 1)] - V.w[wid(d, i, j, k)]) * ih;
    const float3 xp = uc(i + 1, j, k), xm = uc(i - 1, j, k), yp = uc(i, j + 1, k), ym = uc(i, j - 1, k), zp = uc(i, j, k + 1), zm = uc(i, j, k - 1);
    const float hh = 0.5f * ih;
    const float dudy = (yp.x - ym.x) * hh, dudz = (zp.x - zm.x) * hh, dvdx = (xp.y - xm.y) * hh, dvdz = (zp.y - zm.y) * hh, dwdx = (xp.z - xm.z) * hh,
                dwdy = (yp.z - ym.z) * hh;
    const float sxy = 0.5f * (dudy + dvdx), sxz = 0.5f * (dudz + dwdx), syz = 0.5f * (dvdz + dwdy);
    return sqrtf(2.0f * (dudx * dudx + dvdy * dvdy + dwdz * dwdz) + 4.0f * (sxy * sxy + sxz * sxz + syz * syz));
}

__global__ void kCells(CellArgs a) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, kk = blockIdx.z;
    const Dims& d = a.d;
    float hrr = 0.0f, radW = 0.0f, fuelIn = 0.0f, tmax = 0.0f;
    if (i < d.nx && j < d.ny && kk < d.nz) {
        const int c = cid(d, i, j, kk);
        const AGasConst& K = a.k;
        if (a.solid && a.solid[c]) {
            a.div[c] = 0.0f;
            a.q[c] = 0.0f;
            a.rad[c] = 0.0f;
            a.nut[c] = 0.0f;
            a.s.T[c] = fmaxf(a.s.T[c], K.T0);
        } else {
            float T = a.s.T[c], F = a.s.F[c], C = a.s.C[c], O = a.s.O[c], S = a.s.S[c], M = a.s.M[c];
            float T00 = T;   // the temperature the expansion is measured from
            const float V = d.h * d.h * d.h, dt = a.dt;
            float rho = K.rhoT / T, m = rho * V;
            float divMass = 0.0f;
            // what the wood (and the burner) put in: mass at its own temperature, heat, the oxygen the embers take
            float mF = a.src.fuel[c], mC = a.src.co[c], mO = a.src.o2[c], qW = a.src.heat[c], mT = a.src.massT[c];
            const bool boundary = mF > 0.0f;   // the wood's: in the boundary layer over a pyrolysing surface
            if (a.burner.radius > 0.0f && a.burner.power > 0.0f) {
                const float3 p = a.origin + make_float3((i + 0.5f) * d.h, (j + 0.5f) * d.h, (kk + 0.5f) * d.h);
                const float3 r = p - a.burner.centre;
                bool inside;
                float vol;
                if (a.burner.point) {
                    inside = dot(r, r) <= a.burner.radius * a.burner.radius;
                    vol = 4.18879f * a.burner.radius * a.burner.radius * a.burner.radius;
                } else {
                    inside = j == 0 && r.x * r.x + r.z * r.z <= a.burner.radius * a.burner.radius;
                    vol = 3.14159f * a.burner.radius * a.burner.radius * d.h;
                }
                if (inside) {
                    const float share = fminf(V / fmaxf(vol, V), 1.0f);
                    const float rate = a.burner.power / K.hcF * share;
                    mF += rate;
                    mT += rate * a.burner.temp;
                }
            }
            const float mIn = mF + mC;
            if (mIn > 0.0f || mO > 0.0f || qW != 0.0f) {
                const float mNew = m + mIn * dt;
                F = (F * m + mF * dt) / mNew;
                C = (C * m + mC * dt) / mNew;
                O = fmaxf(O * m - mO * dt, 0.0f) / mNew;
                S = S * m / mNew;
                M = M * m / mNew;
                const float Tin = mIn > 0.0f ? mT / mIn : T;
                // mixing at constant pressure keeps the volume: the injected gas brings its own (at its own
                // temperature), the mix needs no more; the heat the surfaces give or take does expand or shrink it
                T = fmaxf((m * T + mIn * dt * Tin) / mNew, 200.0f);
                T00 = T;
                T = fmaxf(T + qW * dt / (mNew * K.cp), 200.0f);
                fuelIn = mF;
                divMass = mIn * (Tin / K.rhoT) / V;
                rho = K.rhoT / T;
                m = rho * V;
            }
            // mixing-limited burning (eddy dissipation): the buoyant time of a cell, or the resolved strain's
            const float strain = strainAt(d, a.V, i, j, kk);
            float tau = fminf(sqrtf(2.0f * d.h / K.g), K.mixC / fmaxf(strain, 1e-3f));
            // over a pyrolysing surface the cell is the boundary layer, laminar: its volatiles meet the air across it
            // by molecular diffusion (D of the hot mixture ~ 2.2e-5 (T/298)^1.75 m^2/s), most of them leave it unburnt
            // and burn above and around the log, where they make their soot, as a wood fire's yellow flames stand on
            // its logs (mixed at the buoyant time, the cell burnt them all at the surface: a blue sheet over each log)
            if (boundary && K.wallMix > 0.0f) tau = fmaxf(tau, K.wallMix * d.h * d.h / (2.2e-5f * powf(fmaxf(T, 288.0f) / 298.0f, 1.75f)));
            a.nut[c] = sqr(K.smag * d.h) * strain;
            const float mixFrac = 1.0f - __expf(-dt / tau);
            const float dil = fmaxf(1.0f - F - C - O - S - M, 0.0f);
            float heat = 0.0f;   // J per kg of the cell's gas
            // the heat capacity grows with the temperature (air and its products: about 1005 J/kg K at 300 K, 1195 at
            // 1200 K, 1260 at 2000 K)
            const float cp = 1005.0f + 300.0f * (1.0f - __expf(-fmaxf(T - 300.0f, 0.0f) / 900.0f));
            // the extinction test (FDS): the reactants with the diluent that comes with their oxygen must be able to
            // heat themselves to the critical flame temperature
            auto burnable = [&](float hc, float s) {
                const float need = (1.0f + s + dil * s / fmaxf(O, 1e-6f)) * cp * (K.Tcft - T);
                return hc >= need;
            };
            // ignition: the cell is hot enough, or a neighbour burns (the flame front, thinner than a cell, crosses
            // into it: its laminar speed ~0.4 m/s takes a cell in about two steps)
            float Tnb = T;
            if (a.Tprev) {
                const float* P = a.Tprev;
                if (i > 0) Tnb = fmaxf(Tnb, P[c - 1]);
                if (i < d.nx - 1) Tnb = fmaxf(Tnb, P[c + 1]);
                if (j > 0) Tnb = fmaxf(Tnb, P[c - d.nx]);
                if (j < d.ny - 1) Tnb = fmaxf(Tnb, P[c + d.nx]);
                if (kk > 0) Tnb = fmaxf(Tnb, P[c - d.nx * d.ny]);
                if (kk < d.nz - 1) Tnb = fmaxf(Tnb, P[c + d.nx * d.ny]);
            }
            float Tig = fmaxf(T, a.Tprev ? Tnb - K.nbMargin : T);
            // a glowing ember lights what flows over it (its boundary layer is hotter than the cell's mean)
            if (a.src.ignite && a.src.ignite[c] >= K.TignSurf) Tig = fmaxf(Tig, K.TignF + 1.0f);
            // a step lets a cell grow by at most maxExpand (its heat beyond waits for the next): the expansion the
            // projection and the advection carry stays small per step, as their accuracy needs (a whole stoichiometric
            // burn in one step grew a cell eightfold, and the advection made a fifth of its fuel twice over)
            // nor past the flame's own ceiling: a cell holds unmixed volatiles and air side by side (the eddy model's
            // picture), and burning them at its mean temperature heated a hot boundary-layer cell to 3700 K; burnt
            // where they mix, they reach the adiabatic temperature at most (Tad)
            const float hMax = fminf((K.maxExpand - 1.0f) * T, fmaxf(K.Tad - T, 0.0f)) * cp;
            auto room = [&](float want) { return want > 0.0f ? fminf(1.0f, fmaxf(hMax - heat, 0.0f) / want) : 0.0f; };
            if (Tig > K.TignF) {
                const float avail = fminf(F, O / K.sF);
                if (avail > 1e-7f && burnable(K.hcF, K.sF)) {
                    float dF = avail * mixFrac;
                    dF *= room(dF * K.hcF);
                    F -= dF;
                    O -= K.sF * dF;
                    heat += dF * K.hcF;
                }
            }
            if (Tig > K.TignC) {
                const float avail = fminf(C, O / K.sC);
                if (avail > 1e-7f && burnable(K.hcC, K.sC)) {
                    float dC = avail * mixFrac;
                    dC *= room(dC * K.hcC);
                    C -= dC;
                    O -= K.sC * dC;
                    heat += dC * K.hcC;
                }
            }
            O = fmaxf(O, 0.0f);
            // soot: formed from the volatiles where they are hot (the fuel side), burnt by oxygen (Lee: kg/m^2/s per
            // atm of O2, times the soot's surface)
            if (T > 1100.0f && F > 1e-6f) {
                const float r = K.sootA * __expf(-K.sootTa / T);
                const float dS = F * (1.0f - __expf(-r * dt));
                F -= dS;
                S += dS * K.sootYield;
            }
            if (S > 0.0f && O > 1e-5f && T > 900.0f) {
                const float pO2 = O * (28.97f / 32.0f) * 0.9f;   // atm (mole fraction; 0.9: the hot mixture's lighter)
                const float wLee = 1.085e5f * pO2 * rsqrtf(T) * __expf(-19778.0f / T);
                const float dS = S * (1.0f - __expf(-K.sootArea * wLee * dt));
                // the soot is the volatiles' carbon gone ahead: it burns with their oxygen and heat per kg (the
                // energy stays the fuel's, whichever way it burns)
                const float sS = K.sF / K.sootYield, hS = K.hcF / K.sootYield;
                float dSo = fminf(dS, O / sS);
                dSo *= room(dSo * hS);
                S -= dSo;
                O -= sS * dSo;
                heat += dSo * hS;
            }
            // the tar that did not burn condenses as the gas cools
            if (T < K.Tcond && F > 1e-6f) {
                const float dM = K.tarShare * F * (1.0f - __expf(-dt / K.tauCond));
                F -= dM;
                M += dM;
            }
            float Tn = T + heat / cp;
            // the soot's radiation (optically thin): 4 kappa_P sigma (T^4 - T0^4), kappa_P = 1862 fv T (1/m)
            const float fv = rho * S / K.rhoSoot;
            const float kP = 1862.0f * fv * Tn;
            const float T4 = Tn * Tn * Tn * Tn, T04 = K.T0 * K.T0 * K.T0 * K.T0;
            float P = 4.0f * kP * kSigma * fmaxf(T4 - T04, 0.0f);   // W/m^3
            // never more than the cell's own heat over T0 in a step
            const float Pmax = 0.7f * rho * cp * fmaxf(Tn - K.T0, 0.0f) / dt;
            P = fminf(P, Pmax);
            Tn -= P * dt / (rho * cp);
            a.s.T[c] = fminf(Tn, K.Tad);
            a.s.F[c] = fmaxf(F, 0.0f);
            a.s.C[c] = fmaxf(C, 0.0f);
            a.s.O[c] = O;
            a.s.S[c] = fmaxf(S, 0.0f);
            a.s.M[c] = fmaxf(M, 0.0f);
            const float qv = heat * rho / dt;   // W/m^3
            a.q[c] = qv;
            a.rad[c] = P;
            // the expansion: the gas at constant pressure grows with its temperature, and the injected mass takes room
            a.div[c] = __logf(Tn / T00) / dt + divMass;
            hrr = qv * V;
            radW = P * V;
            tmax = Tn;
        }
    }
    // a warp's sums, one atomic each
    for (int o = 16; o > 0; o >>= 1) {
        hrr += __shfl_down_sync(0xffffffffu, hrr, o);
        radW += __shfl_down_sync(0xffffffffu, radW, o);
        fuelIn += __shfl_down_sync(0xffffffffu, fuelIn, o);
        tmax = fmaxf(tmax, __shfl_down_sync(0xffffffffu, tmax, o));
    }
    if ((threadIdx.x + threadIdx.y * blockDim.x) % 32 == 0) {
        atomicAdd(&a.red[0], hrr);
        atomicAdd(&a.red[1], radW);
        atomicAdd(&a.red[2], fuelIn);
        atomicMax(reinterpret_cast<int*>(&a.red[3]), __float_as_int(tmax));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// advection (MacCormack with the extrema clamp, Selle et al. 2008)

struct AdvS {
    Dims d;
    Vel V;
    CScalars cur;
    Scalars hat;
    CScalars hatC;
    Scalars out;
    AGasConst k;
    float dt;
    const unsigned char* solid;
    float mc;       // MacCormack's correction (1), or plain semi-Lagrangian (0)
};

// The scalars are mixed by volume as what a volume holds: the density (R = rho / rho0 = T0 / T at constant pressure)
// and each species' partial density (R Y). Averaging T and Y themselves would mix a hot and a cold parcel as if they
// weighed the same: a cell of 2000 K gas and one of 300 K mix to 522 K, not 1150 K, and the hot cell's fuel is a
// sixth of the cold one's per volume. (The linear mix made heat from nothing: a 20 kW burner released 31 kW.)
__device__ __forceinline__ void sample6(const Dims& d, const float* const* arr, bool conserved, float T0, float Yo2, float3 p, float out[6],
                                        float lo[6], float hi[6]) {
    const float x = p.x - 0.5f, y = fminf(fmaxf(p.y - 0.5f, 0.0f), float(d.ny)), z = p.z - 0.5f;
    const int i0 = int(floorf(x)), j0 = min(int(floorf(y)), d.ny - 1), k0 = int(floorf(z));
    const float fx = x - i0, fy = y - j0, fz = z - k0;
#pragma unroll
    for (int f = 0; f < 6; ++f) {
        out[f] = 0.0f;
        lo[f] = 1e30f;
        hi[f] = -1e30f;
    }
#pragma unroll
    for (int n = 0; n < 8; ++n) {
        const int i = i0 + (n & 1), j = min(j0 + ((n >> 1) & 1), d.ny), k = k0 + (n >> 2);
        const bool in = i >= 0 && i < d.nx && k >= 0 && k < d.nz && j < d.ny;
        const float wgt = ((n & 1) ? fx : 1 - fx) * (((n >> 1) & 1) ? fy : 1 - fy) * ((n >> 2) ? fz : 1 - fz);
        float v[6];
        if (!in) {
            // the night's air: density ratio 1, its oxygen
            v[0] = 1.0f;
            v[1] = v[2] = v[4] = v[5] = 0.0f;
            v[3] = Yo2;
        } else {
            const int c = cid(d, i, j, k);
            if (conserved) {
                const float R = T0 / __ldg(arr[0] + c);
                v[0] = R;
#pragma unroll
                for (int f = 1; f < 6; ++f) v[f] = R * __ldg(arr[f] + c);
            } else {
#pragma unroll
                for (int f = 0; f < 6; ++f) v[f] = __ldg(arr[f] + c);
            }
        }
#pragma unroll
        for (int f = 0; f < 6; ++f) {
            out[f] += wgt * v[f];
            lo[f] = fminf(lo[f], v[f]);
            hi[f] = fmaxf(hi[f], v[f]);
        }
    }
}

// pass 1: the hats, as density and partial densities (R, R F, R C, R O, R S, R M)
__global__ void kAdvectS1(AdvS a) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    const Dims& d = a.d;
    if (i >= d.nx || j >= d.ny || k >= d.nz) return;
    const int c = cid(d, i, j, k);
    const float3 p = make_float3(i + 0.5f, j + 0.5f, k + 0.5f);
    const float3 q = backtrace(d, a.V, p, a.dt);
    const float* cur[6] = {a.cur.T, a.cur.F, a.cur.C, a.cur.O, a.cur.S, a.cur.M};
    float v[6], lo[6], hi[6];
    sample6(d, cur, true, a.k.T0, a.k.Yo2, q, v, lo, hi);
    a.hat.T[c] = v[0];
    a.hat.F[c] = v[1];
    a.hat.C[c] = v[2];
    a.hat.O[c] = v[3];
    a.hat.S[c] = v[4];
    a.hat.M[c] = v[5];
}

// pass 2: MacCormack's correction in the same variables, clamped to the source's extrema, back to T and Y
__global__ void kAdvectS2(AdvS a) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    const Dims& d = a.d;
    if (i >= d.nx || j >= d.ny || k >= d.nz) return;
    const int c = cid(d, i, j, k);
    const float3 p = make_float3(i + 0.5f, j + 0.5f, k + 0.5f);
    const float3 q = backtrace(d, a.V, p, a.dt);
    const float3 fwd = backtrace(d, a.V, p, -a.dt);
    const float* cur[6] = {a.cur.T, a.cur.F, a.cur.C, a.cur.O, a.cur.S, a.cur.M};
    const float* hat[6] = {a.hatC.T, a.hatC.F, a.hatC.C, a.hatC.O, a.hatC.S, a.hatC.M};
    float src[6], lo[6], hi[6], back[6], l2[6], h2[6];
    sample6(d, cur, true, a.k.T0, a.k.Yo2, q, src, lo, hi);
    sample6(d, hat, false, a.k.T0, a.k.Yo2, fwd, back, l2, h2);
    const float Rc = a.k.T0 / a.cur.T[c];
    float here[6];
    here[0] = Rc;
    for (int f = 1; f < 6; ++f) here[f] = Rc * cur[f][c];
    float res[6];
    for (int f = 0; f < 6; ++f) {
        const float h0 = hat[f][c];
        const float v = h0 + a.mc * 0.5f * (here[f] - back[f]);
        res[f] = (v < lo[f] || v > hi[f]) ? h0 : v;
    }
    const float R = fmaxf(res[0], 1e-3f);
    a.out.T[c] = a.k.T0 / R;
    a.out.F[c] = fmaxf(res[1] / R, 0.0f);
    a.out.C[c] = fmaxf(res[2] / R, 0.0f);
    a.out.O[c] = fmaxf(res[3] / R, 0.0f);
    a.out.S[c] = fmaxf(res[4] / R, 0.0f);
    a.out.M[c] = fmaxf(res[5] / R, 0.0f);
}

// the velocity's components at their faces: component 0 u, 1 v, 2 w
struct AdvV {
    Dims d;
    Vel V;          // the velocity carrying (the step's start)
    const float* cur;
    const float* hat;
    float* outp;
    int comp;
    float dt;
};

__device__ __forceinline__ void faceDims(const Dims& d, int comp, int& ax, int& ay, int& az, float3& off) {
    ax = d.nx + (comp == 0);
    ay = d.ny + (comp == 1);
    az = d.nz + (comp == 2);
    off = make_float3(comp == 0 ? 0.0f : 0.5f, comp == 1 ? 0.0f : 0.5f, comp == 2 ? 0.0f : 0.5f);
}
__device__ __forceinline__ float faceAt(const float* a, int ax, int ay, int az, float3 off, float3 p, float& lo, float& hi) {
    // beyond the open sides and the top the night's air is still (no wind is carried in from outside: the inflow is
    // the projection's, drawn by the fire); under the floor the first layer
    const float x = p.x - off.x, y = fmaxf(p.y - off.y, 0.0f), z = p.z - off.z;
    const int i0 = int(floorf(x)), j0 = int(floorf(y)), k0 = int(floorf(z));
    const float fx = x - i0, fy = y - j0, fz = z - k0;
    float c[8];
    lo = 1e30f;
    hi = -1e30f;
#pragma unroll
    for (int n = 0; n < 8; ++n) {
        const int i = i0 + (n & 1), j = j0 + ((n >> 1) & 1), k = k0 + (n >> 2);
        const bool in = i >= 0 && i < ax && k >= 0 && k < az && j < ay;
        c[n] = in ? __ldg(a + i + ax * (j + ay * k)) : 0.0f;
        lo = fminf(lo, c[n]);
        hi = fmaxf(hi, c[n]);
    }
    const float x0 = (c[0] * (1 - fx) + c[1] * fx) * (1 - fy) + (c[2] * (1 - fx) + c[3] * fx) * fy;
    const float x1 = (c[4] * (1 - fx) + c[5] * fx) * (1 - fy) + (c[6] * (1 - fx) + c[7] * fx) * fy;
    return x0 * (1 - fz) + x1 * fz;
}

__global__ void kAdvectV1(AdvV a) {
    int ax, ay, az;
    float3 off;
    faceDims(a.d, a.comp, ax, ay, az, off);
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= ax || j >= ay || k >= az) return;
    const float3 p = make_float3(i + off.x, j + off.y, k + off.z);
    const float3 q = backtrace(a.d, a.V, p, a.dt);
    float lo, hi;
    a.outp[i + ax * (j + ay * k)] = faceAt(a.cur, ax, ay, az, off, q, lo, hi);
}
__global__ void kAdvectV2(AdvV a) {
    int ax, ay, az;
    float3 off;
    faceDims(a.d, a.comp, ax, ay, az, off);
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= ax || j >= ay || k >= az) return;
    const int c = i + ax * (j + ay * k);
    const float3 p = make_float3(i + off.x, j + off.y, k + off.z);
    const float3 q = backtrace(a.d, a.V, p, a.dt);
    const float3 fw = backtrace(a.d, a.V, p, -a.dt);
    float lo, hi, l2, h2;
    faceAt(a.cur, ax, ay, az, off, q, lo, hi);
    const float back = faceAt(a.hat, ax, ay, az, off, fw, l2, h2);
    const float f = a.hat[c] + 0.5f * (a.cur[c] - back);
    a.outp[c] = (f < lo || f > hi) ? a.hat[c] : f;
}

// ---------------------------------------------------------------------------------------------------------------
// forces: buoyancy, the jet, vorticity confinement

struct ForceArgs {
    Dims d;
    float3 origin;
    float *u, *v, *w;
    const float* T;
    const float* omega;   // the vorticity at the cells (x, y, z, |w|) as float4, or null
    AGasConst k;
    AGasJet jet;
    float dt;
};

__device__ __forceinline__ float3 jetTarget(const AGasJet& J, float3 p, float& weight) {
    // a round free jet: the centreline speed falls as 6.2 d / s past its core (Pope), it spreads at 0.11 of its length
    weight = 0.0f;
    if (!J.on || J.speed <= 0.0f) return make_float3(0, 0, 0);
    const float3 r = p - J.origin;
    const float s = dot(r, J.dir);
    if (s <= 0.0f || s > J.length) return make_float3(0, 0, 0);
    const float3 perp = r - J.dir * s;
    const float b = J.radius + 0.11f * s;
    const float rr = dot(perp, perp) / (b * b);
    if (rr > 4.0f) return make_float3(0, 0, 0);
    const float uc = J.speed * fminf(1.0f, 6.2f * 2.0f * J.radius / fmaxf(s, 1e-3f));
    weight = __expf(-rr) * (1.0f - smoothstepf(0.7f * J.length, J.length, s));
    return J.dir * (uc * __expf(-rr));
}

__global__ void kForces(ForceArgs a) {
    const Dims& d = a.d;
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    const float relax = 1.0f - __expf(-a.dt / 0.03f);
    // v faces: buoyancy (the gas's lightness against the night's air) and the jet
    if (i < d.nx && j <= d.ny && k < d.nz) {
        const int f = vid(d, i, j, k);
        if (j == 0) a.v[f] = 0.0f;
        else {
            const float Tb = a.T[cid(d, i, j - 1, k)], Tt = j < d.ny ? a.T[cid(d, i, j, k)] : Tb;
            const float Tf = 0.5f * (Tb + Tt);
            // a parcel lighter than the air around it rises at g (rho0 - rho) / rho = g (T / T0 - 1): McCaffrey's
            // continuous-flame speeds (6.84 sqrt(z)) are this acceleration at flame temperatures
            const float lift = a.k.buoyLocal > 0.0f ? (Tf / a.k.T0 - 1.0f) : (1.0f - a.k.T0 / Tf);
            float vv = a.v[f] + a.dt * a.k.g * lift * a.k.buoy;
            const float3 p = a.origin + make_float3((i + 0.5f) * d.h, j * d.h, (k + 0.5f) * d.h);
            float wgt;
            const float3 t = jetTarget(a.jet, p, wgt);
            if (wgt > 0.0f) vv += (t.y - vv) * relax * wgt;
            a.v[f] = vv;
        }
    }
    if (i <= d.nx && j < d.ny && k < d.nz) {
        const int f = uid(d, i, j, k);
        const float3 p = a.origin + make_float3(i * d.h, (j + 0.5f) * d.h, (k + 0.5f) * d.h);
        float wgt;
        const float3 t = jetTarget(a.jet, p, wgt);
        if (wgt > 0.0f) a.u[f] += (t.x - a.u[f]) * relax * wgt;
    }
    if (i < d.nx && j < d.ny && k <= d.nz) {
        const int f = wid(d, i, j, k);
        const float3 p = a.origin + make_float3((i + 0.5f) * d.h, (j + 0.5f) * d.h, k * d.h);
        float wgt;
        const float3 t = jetTarget(a.jet, p, wgt);
        if (wgt > 0.0f) a.w[f] += (t.z - a.w[f]) * relax * wgt;
    }
}

// the jet is the night's air: it brings oxygen and its own coolness where it blows
__global__ void kJetAir(Dims d, float3 origin, Scalars s, AGasJet jet, AGasConst k, float dt) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, kk = blockIdx.z;
    if (i >= d.nx || j >= d.ny || kk >= d.nz) return;
    const float3 p = origin + make_float3((i + 0.5f) * d.h, (j + 0.5f) * d.h, (kk + 0.5f) * d.h);
    float wgt;
    jetTarget(jet, p, wgt);
    if (wgt <= 0.0f) return;
    const float3 r = p - jet.origin;
    const float sAlong = dot(r, jet.dir);
    // the core carries the mouth's air; further on it is mixed with what it took in on the way (entrainment)
    const float core = wgt * (1.0f - smoothstepf(0.0f, 0.25f * jet.length, sAlong)) * (1.0f - __expf(-dt / 0.03f));
    if (core <= 0.0f) return;
    const int c = cid(d, i, j, kk);
    s.O[c] += (k.Yo2 - s.O[c]) * core;
    s.T[c] += (k.T0 - s.T[c]) * core;
}

__global__ void kVorticity(Dims d, const float* u, const float* v, const float* w, float4* om) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= d.nx || j >= d.ny || k >= d.nz) return;
    auto uc = [&](int a, int b, int c) {
        a = min(max(a, 0), d.nx - 1);
        b = min(max(b, 0), d.ny - 1);
        c = min(max(c, 0), d.nz - 1);
        return make_float3(0.5f * (u[uid(d, a, b, c)] + u[uid(d, a + 1, b, c)]), 0.5f * (v[vid(d, a, b, c)] + v[vid(d, a, b + 1, c)]),
                           0.5f * (w[wid(d, a, b, c)] + w[wid(d, a, b, c + 1)]));
    };
    const float hh = 0.5f / d.h;
    const float3 xp = uc(i + 1, j, k), xm = uc(i - 1, j, k), yp = uc(i, j + 1, k), ym = uc(i, j - 1, k), zp = uc(i, j, k + 1), zm = uc(i, j, k - 1);
    const float wx = ((yp.z - ym.z) - (zp.y - zm.y)) * hh, wy = ((zp.x - zm.x) - (xp.z - xm.z)) * hh, wz = ((xp.y - xm.y) - (yp.x - ym.x)) * hh;
    om[cid(d, i, j, k)] = make_float4(wx, wy, wz, sqrtf(wx * wx + wy * wy + wz * wz));
}

// vorticity confinement (Fedkiw et al. 2001): eps h (N x omega), N the unit gradient of |omega|, onto the faces
__global__ void kConfine(Dims d, const float4* om, float* u, float* v, float* w, float eps, float dt) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= d.nx || j >= d.ny || k >= d.nz) return;
    auto mag = [&](int a, int b, int c) {
        a = min(max(a, 0), d.nx - 1);
        b = min(max(b, 0), d.ny - 1);
        c = min(max(c, 0), d.nz - 1);
        return om[cid(d, a, b, c)].w;
    };
    float3 g = make_float3(mag(i + 1, j, k) - mag(i - 1, j, k), mag(i, j + 1, k) - mag(i, j - 1, k), mag(i, j, k + 1) - mag(i, j, k - 1));
    const float gl = length(g);
    if (gl < 1e-6f) return;
    g = g * (1.0f / gl);
    const float4 o = om[cid(d, i, j, k)];
    const float3 f = cross(g, make_float3(o.x, o.y, o.z)) * (eps * d.h * dt * 0.5f);
    // half to each of the cell's two faces per axis (the neighbours add theirs)
    atomicAdd(&u[uid(d, i, j, k)], f.x);
    atomicAdd(&u[uid(d, i + 1, j, k)], f.x);
    if (j > 0) atomicAdd(&v[vid(d, i, j, k)], f.y);
    atomicAdd(&v[vid(d, i, j + 1, k)], f.y);
    atomicAdd(&w[wid(d, i, j, k)], f.z);
    atomicAdd(&w[wid(d, i, j, k + 1)], f.z);
}

// ---------------------------------------------------------------------------------------------------------------
// the sub-grid's mixing (Smagorinsky, as FDS: Cs 0.2, turbulent Schmidt and Prandtl numbers 0.5): what the grid's
// eddies cannot carry, the eddy diffusivity does, explicitly (its number kept under the explicit limit)

__global__ void kDiffuseS(Dims d, CScalars s, Scalars o, const float* nut, const unsigned char* solid, AGasConst k, float dt) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, kk = blockIdx.z;
    if (i >= d.nx || j >= d.ny || kk >= d.nz) return;
    const int c = cid(d, i, j, kk);
    const float lim = 0.15f * d.h * d.h / dt;   // (the explicit limit is h^2 / 6 dt)
    const float Dc = fminf(nut[c] / k.Sct, lim);
    const float* in[6] = {s.T, s.F, s.C, s.O, s.S, s.M};
    float* out[6] = {o.T, o.F, o.C, o.O, o.S, o.M};
    const float amb[6] = {k.T0, 0.0f, 0.0f, k.Yo2, 0.0f, 0.0f};
    float acc[6] = {0, 0, 0, 0, 0, 0};
    const bool sc = solid && solid[c];
    if (!sc) {
        const int di[6] = {-1, 1, 0, 0, 0, 0}, dj[6] = {0, 0, -1, 1, 0, 0}, dk[6] = {0, 0, 0, 0, -1, 1};
        for (int n = 0; n < 6; ++n) {
            const int a = i + di[n], b = j + dj[n], e = kk + dk[n];
            if (b < 0) continue;   // the floor: no flux
            const bool out = a < 0 || a >= d.nx || e < 0 || e >= d.nz || b >= d.ny;
            int nb = 0;
            float Dn = Dc;
            if (!out) {
                nb = cid(d, a, b, e);
                if (solid && solid[nb]) continue;
                Dn = fminf(nut[nb] / k.Sct, lim);
            }
            const float Df = 0.5f * (Dc + Dn);
            for (int f = 0; f < 6; ++f) acc[f] += Df * ((out ? amb[f] : in[f][nb]) - in[f][c]);
        }
    }
    const float g = dt / (d.h * d.h);
    for (int f = 0; f < 6; ++f) out[f][c] = fmaxf(in[f][c] + g * acc[f], f == 0 ? 200.0f : 0.0f);
}

// the eddy viscosity on a velocity component's faces (the cells' values averaged onto them)
__global__ void kDiffuseV(Dims d, const float* cur, float* outp, int comp, const float* nut, float dt) {
    int ax = d.nx + (comp == 0), ay = d.ny + (comp == 1), az = d.nz + (comp == 2);
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= ax || j >= ay || k >= az) return;
    const int f = i + ax * (j + ay * k);
    // the face's two cells
    const int i0 = comp == 0 ? max(i - 1, 0) : i, j0 = comp == 1 ? max(j - 1, 0) : j, k0 = comp == 2 ? max(k - 1, 0) : k;
    const int i1 = comp == 0 ? min(i, d.nx - 1) : i, j1 = comp == 1 ? min(j, d.ny - 1) : j, k1 = comp == 2 ? min(k, d.nz - 1) : k;
    const float lim = 0.15f * d.h * d.h / dt;
    const float nu = fminf(0.5f * (nut[cid(d, i0, j0, k0)] + nut[cid(d, i1, j1, k1)]), lim);
    float acc = 0.0f;
    const float c0 = cur[f];
    auto at = [&](int a, int b, int e) -> float {
        if (b < 0) return c0;   // the floor: free slip
        if (a < 0 || a >= ax || e < 0 || e >= az || b >= ay) return 0.0f;   // the still air beyond
        return cur[a + ax * (b + ay * e)];
    };
    acc += at(i - 1, j, k) + at(i + 1, j, k) + at(i, j - 1, k) + at(i, j + 1, k) + at(i, j, k - 1) + at(i, j, k + 1) - 6.0f * c0;
    outp[f] = c0 + nu * dt / (d.h * d.h) * acc;
}

// ---------------------------------------------------------------------------------------------------------------
// the projection: div u* - S = lap(phi), u = u* - grad(phi); phi on cells, the open sides and top at zero on their
// faces (the ghost cell is -phi), the floor and the solids closed

__global__ void kDivergence(Dims d, const float* u, const float* v, const float* w, const float* S, const unsigned char* solid, float* rhs) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= d.nx || j >= d.ny || k >= d.nz) return;
    const int c = cid(d, i, j, k);
    if (solid && solid[c]) {
        rhs[c] = 0.0f;
        return;
    }
    const float div = (u[uid(d, i + 1, j, k)] - u[uid(d, i, j, k)] + v[vid(d, i, j + 1, k)] - v[vid(d, i, j, k)] + w[wid(d, i, j, k + 1)] - w[wid(d, i, j, k)]) / d.h;
    rhs[c] = -d.h * d.h * (div - S[c]);
}

// The density varies sevenfold between the night's air and the flame: the pressure acts on each face through 1/rho
// there. With phi = dt p / rho0 and beta = rho0 / rho_face (the harmonic mean of T / T0 of the face's two cells):
// sum over faces beta (phi_c - phi_n) = b, and u -= beta grad(phi). A constant-density projection would push the light
// gas as hard as the heavy: the plume came out either too slow (buoyancy over rho0) or a hot, unmixed chimney (over
// rho). Face coefficients per level: x (nx+1) ny nz, y nx (ny+1) nz, z nx ny (nz+1); 0 on closed faces (the floor, a
// solid).
struct Coef {
    float *bx, *by, *bz;
};

__global__ void kCoef(Dims d, const float* T, const unsigned char* solid, float T0, Coef cf) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    auto Tc = [&](int a, int b, int c) {
        if (a < 0 || a >= d.nx || c < 0 || c >= d.nz || b >= d.ny) return T0;   // the air beyond
        return T[cid(d, a, b, c)];
    };
    auto sol = [&](int a, int b, int c) {
        if (a < 0 || a >= d.nx || c < 0 || c >= d.nz || b >= d.ny || b < 0) return false;
        return solid && solid[cid(d, a, b, c)] != 0;
    };
    auto beta = [&](float t1, float t2) { return 2.0f / (T0 * (1.0f / t1 + 1.0f / t2)); };
    if (i <= d.nx && j < d.ny && k < d.nz)
        cf.bx[uid(d, i, j, k)] = (sol(i - 1, j, k) || sol(i, j, k)) ? 0.0f : beta(Tc(i - 1, j, k), Tc(i, j, k));
    if (i < d.nx && j <= d.ny && k < d.nz)
        cf.by[vid(d, i, j, k)] = (j == 0 || sol(i, j - 1, k) || sol(i, j, k)) ? 0.0f : beta(Tc(i, j - 1, k), Tc(i, j, k));
    if (i < d.nx && j < d.ny && k <= d.nz)
        cf.bz[wid(d, i, j, k)] = (sol(i, j, k - 1) || sol(i, j, k)) ? 0.0f : beta(Tc(i, j, k - 1), Tc(i, j, k));
}

// a coarse face's coefficient: the mean of the four fine faces it covers
__global__ void kCoarsenCoef(int cx, int cy, int cz, Coef c, int fx, int fy, int fz, Coef f) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i <= cx && j < cy && k < cz) {
        float s = 0.0f;
        for (int n = 0; n < 4; ++n) s += f.bx[(2 * i) + (fx + 1) * ((2 * j + (n & 1)) + fy * (2 * k + (n >> 1)))];
        c.bx[i + (cx + 1) * (j + cy * k)] = 0.25f * s;
    }
    if (i < cx && j <= cy && k < cz) {
        float s = 0.0f;
        for (int n = 0; n < 4; ++n) s += f.by[(2 * i + (n & 1)) + fx * ((2 * j) + (fy + 1) * (2 * k + (n >> 1)))];
        c.by[i + cx * (j + (cy + 1) * k)] = 0.25f * s;
    }
    if (i < cx && j < cy && k <= cz) {
        float s = 0.0f;
        for (int n = 0; n < 4; ++n) s += f.bz[(2 * i + (n & 1)) + fx * ((2 * j + (n >> 1)) + fy * (2 * k))];
        c.bz[i + cx * (j + cy * k)] = 0.25f * s;
    }
}

// the operator at a cell: A phi = sum over faces beta (phi_c - phi_n), open faces with the ghost -phi_c
__device__ __forceinline__ void stencil(int nx, int ny, int nz, const Coef& cf, const float* phi, int i, int j, int k, float& sum, float& diag) {
    sum = 0.0f;
    diag = 0.0f;
    const float bW = cf.bx[i + (nx + 1) * (j + ny * k)], bE = cf.bx[i + 1 + (nx + 1) * (j + ny * k)];
    const float bS = cf.by[i + nx * (j + (ny + 1) * k)], bN = cf.by[i + nx * (j + 1 + (ny + 1) * k)];
    const float bB = cf.bz[i + nx * (j + ny * k)], bF = cf.bz[i + nx * (j + ny * (k + 1))];
    const int c = i + nx * (j + ny * k);
    if (i > 0) { sum += bW * phi[c - 1]; diag += bW; } else diag += 2.0f * bW;
    if (i < nx - 1) { sum += bE * phi[c + 1]; diag += bE; } else diag += 2.0f * bE;
    if (j > 0) { sum += bS * phi[c - nx]; diag += bS; }   // (the floor's coefficient is 0)
    if (j < ny - 1) { sum += bN * phi[c + nx]; diag += bN; } else diag += 2.0f * bN;
    if (k > 0) { sum += bB * phi[c - nx * ny]; diag += bB; } else diag += 2.0f * bB;
    if (k < nz - 1) { sum += bF * phi[c + nx * ny]; diag += bF; } else diag += 2.0f * bF;
}

// one colour of a red-black Gauss-Seidel sweep
__global__ void kSmooth(int nx, int ny, int nz, float* phi, const float* b, Coef cf, int colour) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= nx || j >= ny || k >= nz || ((i + j + k) & 1) != colour) return;
    const int c = i + nx * (j + ny * k);
    float sum, diag;
    stencil(nx, ny, nz, cf, phi, i, j, k, sum, diag);
    phi[c] = diag > 1e-12f ? (b[c] + sum) / diag : 0.0f;
}

__global__ void kResidual(int nx, int ny, int nz, const float* phi, const float* b, Coef cf, float* r, float* red) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    float rr = 0.0f, bb2 = 0.0f;
    if (i < nx && j < ny && k < nz) {
        const int c = i + nx * (j + ny * k);
        float sum, diag;
        stencil(nx, ny, nz, cf, phi, i, j, k, sum, diag);
        r[c] = diag > 1e-12f ? b[c] - (diag * phi[c] - sum) : 0.0f;
        rr = r[c] * r[c];
        bb2 = b[c] * b[c];
    }
    if (red) {
        for (int o = 16; o > 0; o >>= 1) {
            rr += __shfl_down_sync(0xffffffffu, rr, o);
            bb2 += __shfl_down_sync(0xffffffffu, bb2, o);
        }
        if ((threadIdx.x + threadIdx.y * blockDim.x) % 32 == 0) {
            atomicAdd(&red[4], rr);
            atomicAdd(&red[5], bb2);
        }
    }
}

// fine residual to the coarse right-hand side: b_c = (sum of the 8 children's residuals) / 2 (h doubles)
__global__ void kRestrict(int cx, int cy, int cz, const float* r, int fx, int fy, float* bc, float* phic) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= cx || j >= cy || k >= cz) return;
    float s = 0.0f;
    for (int n = 0; n < 8; ++n) s += r[(2 * i + (n & 1)) + fx * ((2 * j + ((n >> 1) & 1)) + fy * (2 * k + (n >> 2)))];
    const int c = i + cx * (j + cy * k);
    bc[c] = 0.5f * s;
    phic[c] = 0.0f;
}

// the coarse correction back, trilinear between the coarse cells (cell-centred: the fine cell at coarse i/2 - 1/4)
__global__ void kProlong(int fx, int fy, int fz, float* phif, const unsigned char* mskf, int cx, int cy, int cz, const float* ec) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= fx || j >= fy || k >= fz) return;
    const int c = i + fx * (j + fy * k);
    if (mskf && mskf[c]) return;
    const float x = 0.5f * i - 0.25f, y = 0.5f * j - 0.25f, z = 0.5f * k - 0.25f;
    const int i0 = int(floorf(x)), j0 = int(floorf(y)), k0 = int(floorf(z));
    const float ax = x - i0, ay = y - j0, az = z - k0;
    float s = 0.0f;
    for (int n = 0; n < 8; ++n) {
        int a = i0 + (n & 1), b = j0 + ((n >> 1) & 1), cc = k0 + (n >> 2);
        const float wgt = ((n & 1) ? ax : 1 - ax) * (((n >> 1) & 1) ? ay : 1 - ay) * ((n >> 2) ? az : 1 - az);
        float sign = 1.0f;
        // a ghost beyond an open side is minus its cell; under the floor it is the cell itself
        if (a < 0) { a = 0; sign = -sign; }
        if (a >= cx) { a = cx - 1; sign = -sign; }
        if (cc < 0) { cc = 0; sign = -sign; }
        if (cc >= cz) { cc = cz - 1; sign = -sign; }
        if (b >= cy) { b = cy - 1; sign = -sign; }
        if (b < 0) b = 0;
        s += wgt * sign * ec[a + cx * (b + cy * cc)];
    }
    phif[c] += s;
}

__global__ void kCoarsenMask(int cx, int cy, int cz, const unsigned char* mf, int fx, int fy, unsigned char* mc) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    if (i >= cx || j >= cy || k >= cz) return;
    int all = 1;
    for (int n = 0; n < 8; ++n) all &= mf[(2 * i + (n & 1)) + fx * ((2 * j + ((n >> 1) & 1)) + fy * (2 * k + (n >> 2)))] ? 1 : 0;
    mc[i + cx * (j + cy * k)] = (unsigned char)all;
}

__global__ void kProject(Dims d, float* u, float* v, float* w, const float* phi, Coef cf) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, k = blockIdx.z;
    auto P = [&](int a, int b, int c) { return phi[cid(d, a, b, c)]; };
    const float ih = 1.0f / d.h;
    if (i <= d.nx && j < d.ny && k < d.nz) {
        const int f = uid(d, i, j, k);
        const float bt = cf.bx[f];
        if (bt == 0.0f) u[f] = 0.0f;
        else if (i == 0) u[f] -= bt * (P(0, j, k) * 2.0f) * ih;
        else if (i == d.nx) u[f] -= bt * (-P(d.nx - 1, j, k) * 2.0f) * ih;
        else u[f] -= bt * (P(i, j, k) - P(i - 1, j, k)) * ih;
    }
    if (i < d.nx && j <= d.ny && k < d.nz) {
        const int f = vid(d, i, j, k);
        const float bt = cf.by[f];
        if (bt == 0.0f) v[f] = 0.0f;
        else if (j == d.ny) v[f] -= bt * (-P(i, d.ny - 1, k) * 2.0f) * ih;
        else v[f] -= bt * (P(i, j, k) - P(i, j - 1, k)) * ih;
    }
    if (i < d.nx && j < d.ny && k <= d.nz) {
        const int f = wid(d, i, j, k);
        const float bt = cf.bz[f];
        if (bt == 0.0f) w[f] = 0.0f;
        else if (k == 0) w[f] -= bt * (P(i, j, 0) * 2.0f) * ih;
        else if (k == d.nz) w[f] -= bt * (-P(i, j, d.nz - 1) * 2.0f) * ih;
        else w[f] -= bt * (P(i, j, k) - P(i, j, k - 1)) * ih;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// what the drawing reads

__global__ void kPack(Dims d, CScalars s, const float* q, AGasConst k, cudaSurfaceObject_t out, unsigned int* bricks, int bnx, int bny) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, kk = blockIdx.z;
    if (i >= d.nx || j >= d.ny || kk >= d.nz) return;
    const int c = cid(d, i, j, kk);
    const float T = s.T[c], rho = k.rhoT / T;
    const float fv = rho * s.S[c] / k.rhoSoot;
    // the heat release over the cell and its neighbours (the flame front is thinner than a cell: one cell's value
    // drawn trilinearly shows the grid as blocks)
    float qs = 0.0f, wsum = 0.0f;
    for (int dk = -1; dk <= 1; ++dk)
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                const int a = i + di, b = j + dj, e = kk + dk;
                if (a < 0 || b < 0 || e < 0 || a >= d.nx || b >= d.ny || e >= d.nz) continue;
                const float wgt = (di == 0 ? 2.0f : 1.0f) * (dj == 0 ? 2.0f : 1.0f) * (dk == 0 ? 2.0f : 1.0f);
                qs += wgt * q[cid(d, a, b, e)];
                wsum += wgt;
            }
    const float4 v = make_float4(T, fv * 1e6f, rho * s.M[c] * 1e3f, qs / wsum * 1e-6f);
    surf3Dwrite(make_ushort4(__half_as_ushort(__float2half_rn(v.x)), __half_as_ushort(__float2half_rn(v.y)), __half_as_ushort(__float2half_rn(v.z)),
                             __half_as_ushort(__float2half_rn(v.w))),
                out, i * int(sizeof(ushort4)), j, kk);
    // the cell's mark for the march (the same thresholds it draws by: soot over 1e-5, a front over 0.01 MW/m^3, the
    // haze over 330 K), on every brick whose samples read it (a cell beside a brick's side is read from inside it)
    const unsigned int lvl = (v.y > 1e-5f || v.w > 0.01f) ? 2u : v.x > 330.0f ? 1u : 0u;
    if (lvl == 0u) return;
    const int x0 = max(i - 1, 0) / ATES_BRICK, x1 = min(i + 1, d.nx - 1) / ATES_BRICK, y0 = max(j - 1, 0) / ATES_BRICK, y1 = min(j + 1, d.ny - 1) / ATES_BRICK,
              z0 = max(kk - 1, 0) / ATES_BRICK, z1 = min(kk + 1, d.nz - 1) / ATES_BRICK;
    for (int bz = z0; bz <= z1; ++bz)
        for (int by = y0; by <= y1; ++by)
            for (int bx = x0; bx <= x1; ++bx) {
                unsigned int* m = &bricks[bx + bnx * (by + bny * bz)];
                if (*m < lvl) atomicMax(m, lvl);
            }
}

// the soot's light for the surfaces: its luminous intensity summed over blocks (cd, RGB), from the same emission table
// the eye's march reads (ates_spectra.h), so what lights the stones is what the flame shows
__global__ void kLight(Dims d, CScalars s, AGasConst k, const float3* table, int div, int lx, int ly, int lz, float3* light) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x, j = blockIdx.y * blockDim.y + threadIdx.y, kk = blockIdx.z;
    if (i >= lx || j >= ly || kk >= lz) return;
    float3 sum = make_float3(0, 0, 0);
    for (int c = 0; c < div; ++c)
        for (int b = 0; b < div; ++b)
            for (int a = 0; a < div; ++a) {
                const int x = i * div + a, y = j * div + b, z = kk * div + c;
                if (x >= d.nx || y >= d.ny || z >= d.nz) continue;
                const int cc = cid(d, x, y, z);
                const float T = s.T[cc], Ss = s.S[cc];
                if (T < 700.0f || Ss <= 0.0f) continue;
                const float fvE6 = (k.rhoT / T) * Ss / k.rhoSoot * 1e6f;
                const float tx = fminf(fmaxf((T - ATES_TABLE_T0) / ATES_TABLE_DT, 0.0f), float(ATES_TABLE_N - 1) - 0.001f);
                const int ti = int(tx);
                const float tf = tx - ti;
                sum += (table[ti] * (1.0f - tf) + table[ti + 1] * tf) * fvE6;
            }
    light[i + lx * (j + ly * kk)] = sum * (d.h * d.h * d.h);
}

__global__ void kFill(float* a, size_t n, float v) {
    const size_t i = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i < n) a[i] = v;
}

dim3 grid3(int x, int y, int z, dim3 b) { return dim3((x + b.x - 1) / b.x, (y + b.y - 1) / b.y, z); }

}   // namespace

// ---------------------------------------------------------------------------------------------------------------

float* AtesGas::alloc(size_t n) {
    float* p = nullptr;
    GAS_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(float)));
    GAS_CHECK(cudaMemset(p, 0, n * sizeof(float)));
    owned_.push_back(p);
    bytes += n * sizeof(float);
    return p;
}

bool AtesGas::init(int nx_, int ny_, int nz_, float h_, float3 origin_, cudaStream_t stream) {
    release();
    nx = nx_;
    ny = ny_;
    nz = nz_;
    h = h_;
    origin = origin_;
    st_ = stream;
    const size_t n = size_t(nx) * ny * nz;
    const size_t nu = size_t(nx + 1) * ny * nz, nv = size_t(nx) * (ny + 1) * nz, nw = size_t(nx) * ny * (nz + 1);
    T = alloc(n), F = alloc(n), C = alloc(n), O = alloc(n), S = alloc(n), M = alloc(n);
    T2_ = alloc(n), F2_ = alloc(n), C2_ = alloc(n), O2_ = alloc(n), S2_ = alloc(n), M2_ = alloc(n);
    T3_ = alloc(n), F3_ = alloc(n), C3_ = alloc(n), O3_ = alloc(n), S3_ = alloc(n), M3_ = alloc(n);
    omega_ = reinterpret_cast<float4*>(alloc(4 * n));
    nut_ = alloc(n);
    u = alloc(nu), u2_ = alloc(nu), u3_ = alloc(nu);
    v = alloc(nv), v2_ = alloc(nv), v3_ = alloc(nv);
    w = alloc(nw), w2_ = alloc(nw), w3_ = alloc(nw);
    q = alloc(n);
    rad = alloc(n);
    div_ = alloc(n);
    src.fuel = alloc(n);
    src.co = alloc(n);
    src.o2 = alloc(n);
    src.heat = alloc(n);
    src.massT = alloc(n);
    src.ignite = alloc(n);
    GAS_CHECK(cudaMalloc(reinterpret_cast<void**>(&solid), n));
    GAS_CHECK(cudaMemset(solid, 0, n));
    owned_.push_back(solid);
    bytes += n;
    // the multigrid's levels: halving while every side stays even and at least 4
    dims_.clear();
    phi_.clear();
    rhs_.clear();
    res_.clear();
    msk_.clear();
    bx_.clear();
    by_.clear();
    bz_.clear();
    int3 dl = make_int3(nx, ny, nz);
    for (;;) {
        dims_.push_back(dl);
        const size_t m = size_t(dl.x) * dl.y * dl.z;
        phi_.push_back(alloc(m));
        rhs_.push_back(alloc(m));
        res_.push_back(alloc(m));
        bx_.push_back(alloc(size_t(dl.x + 1) * dl.y * dl.z));
        by_.push_back(alloc(size_t(dl.x) * (dl.y + 1) * dl.z));
        bz_.push_back(alloc(size_t(dl.x) * dl.y * (dl.z + 1)));
        if (dims_.size() == 1) msk_.push_back(solid);
        else {
            unsigned char* mk = nullptr;
            GAS_CHECK(cudaMalloc(reinterpret_cast<void**>(&mk), m));
            GAS_CHECK(cudaMemset(mk, 0, m));
            owned_.push_back(mk);
            msk_.push_back(mk);
        }
        if (dl.x % 2 || dl.y % 2 || dl.z % 2 || dl.x / 2 < 4 || dl.y / 2 < 4 || dl.z / 2 < 4) break;
        dl = make_int3(dl.x / 2, dl.y / 2, dl.z / 2);
    }
    levels = int(dims_.size());
    red_ = alloc(8);
    lnx = (nx + lightDiv - 1) / lightDiv;
    lny = (ny + lightDiv - 1) / lightDiv;
    lnz = (nz + lightDiv - 1) / lightDiv;
    light_ = reinterpret_cast<float3*>(alloc(3 * size_t(lnx) * lny * lnz));
    // the drawing's texture (half: the march is bound by its reads; T keeps 0.5-2 K, the rest is scaled to unit range)
    bnx = (nx + ATES_BRICK - 1) / ATES_BRICK;
    bny = (ny + ATES_BRICK - 1) / ATES_BRICK;
    bnz = (nz + ATES_BRICK - 1) / ATES_BRICK;
    bricks_ = reinterpret_cast<unsigned int*>(alloc(size_t(bnx) * bny * bnz));
    const cudaChannelFormatDesc fd = cudaCreateChannelDescHalf4();
    GAS_CHECK(cudaMalloc3DArray(&arr_, &fd, make_cudaExtent(nx, ny, nz), cudaArraySurfaceLoadStore));
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr_;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = td.addressMode[2] = cudaAddressModeBorder;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 0;
    GAS_CHECK(cudaCreateTextureObject(&tex_, &rd, &td, nullptr));
    GAS_CHECK(cudaCreateSurfaceObject(&surf_, &rd));
    bytes += n * 4 * sizeof(unsigned short);
    GAS_CHECK(cudaEventCreate(&e0_));
    GAS_CHECK(cudaEventCreate(&e1_));
    reset();
    return true;
}

void AtesGas::reset() {
    const size_t n = size_t(nx) * ny * nz;
    const int bs = 256;
    const int nb = int((n + bs - 1) / bs);
    kFill<<<nb, bs, 0, st_>>>(T, n, k.T0);
    kFill<<<nb, bs, 0, st_>>>(O, n, k.Yo2);
    for (float* p : {F, C, S, M, q, rad, div_}) GAS_CHECK(cudaMemsetAsync(p, 0, n * sizeof(float), st_));
    GAS_CHECK(cudaMemsetAsync(u, 0, size_t(nx + 1) * ny * nz * sizeof(float), st_));
    GAS_CHECK(cudaMemsetAsync(v, 0, size_t(nx) * (ny + 1) * nz * sizeof(float), st_));
    GAS_CHECK(cudaMemsetAsync(w, 0, size_t(nx) * ny * (nz + 1) * sizeof(float), st_));
    for (float* p : phi_) (void)p;
    for (size_t l = 0; l < phi_.size(); ++l) GAS_CHECK(cudaMemsetAsync(phi_[l], 0, size_t(dims_[l].x) * dims_[l].y * dims_[l].z * sizeof(float), st_));
    for (float* p : {src.fuel, src.co, src.o2, src.heat, src.massT, src.ignite}) GAS_CHECK(cudaMemsetAsync(p, 0, n * sizeof(float), st_));
    pack(nullptr);
}

void AtesGas::smooth(int l, int iters) {
    const int3 dl = dims_[l];
    const dim3 b(32, 4), g = grid3(dl.x, dl.y, dl.z, b);
    const Coef cf{bx_[l], by_[l], bz_[l]};
    for (int it = 0; it < iters; ++it)
        for (int c = 0; c < 2; ++c) kSmooth<<<g, b, 0, st_>>>(dl.x, dl.y, dl.z, phi_[l], rhs_[l], cf, c);
}

void AtesGas::vcycle(int l) {
    const int3 dl = dims_[l];
    const dim3 b(32, 4), g = grid3(dl.x, dl.y, dl.z, b);
    if (l == levels - 1) {
        smooth(l, 24);
        return;
    }
    smooth(l, 2);
    kResidual<<<g, b, 0, st_>>>(dl.x, dl.y, dl.z, phi_[l], rhs_[l], Coef{bx_[l], by_[l], bz_[l]}, res_[l], nullptr);
    const int3 dc = dims_[l + 1];
    kRestrict<<<grid3(dc.x, dc.y, dc.z, b), b, 0, st_>>>(dc.x, dc.y, dc.z, res_[l], dl.x, dl.y, rhs_[l + 1], phi_[l + 1]);
    vcycle(l + 1);
    kProlong<<<g, b, 0, st_>>>(dl.x, dl.y, dl.z, phi_[l], msk_[l], dc.x, dc.y, dc.z, phi_[l + 1]);
    smooth(l, 2);
}

void AtesGas::project(float dt) {
    (void)dt;
    const Dims d{nx, ny, nz, h};
    const dim3 b(32, 4), g = grid3(nx, ny, nz, b);
    const dim3 gf = grid3(nx + 1, ny + 1, nz + 1, b);
    kCoef<<<gf, b, 0, st_>>>(d, T, solid, k.T0, Coef{bx_[0], by_[0], bz_[0]});
    for (int l = 1; l < levels; ++l) {
        const int3 dc = dims_[l], df = dims_[l - 1];
        kCoarsenMask<<<grid3(dc.x, dc.y, dc.z, b), b, 0, st_>>>(dc.x, dc.y, dc.z, msk_[l - 1], df.x, df.y, msk_[l]);
        kCoarsenCoef<<<grid3(dc.x + 1, dc.y + 1, dc.z + 1, b), b, 0, st_>>>(dc.x, dc.y, dc.z, Coef{bx_[l], by_[l], bz_[l]}, df.x, df.y, df.z,
                                                                             Coef{bx_[l - 1], by_[l - 1], bz_[l - 1]});
    }
    kDivergence<<<g, b, 0, st_>>>(d, u, v, w, div_, solid, rhs_[0]);
    // warm start: last step's phi (the pressure changes slowly)
    for (int c = 0; c < vcycles; ++c) vcycle(0);
    GAS_CHECK(cudaMemsetAsync(red_ + 4, 0, 2 * sizeof(float), st_));
    kResidual<<<g, b, 0, st_>>>(nx, ny, nz, phi_[0], rhs_[0], Coef{bx_[0], by_[0], bz_[0]}, res_[0], red_);
    kProject<<<gf, b, 0, st_>>>(d, u, v, w, phi_[0], Coef{bx_[0], by_[0], bz_[0]});
}

void AtesGas::step(float dt) {
    const Dims d{nx, ny, nz, h};
    const dim3 b(32, 4), g = grid3(nx, ny, nz, b);
    GAS_CHECK(cudaEventRecord(e0_, st_));
    if (profile && !pe_[0])
        for (cudaEvent_t& e : pe_) GAS_CHECK(cudaEventCreate(&e));
    auto mark = [&](int s) {
        if (profile) GAS_CHECK(cudaEventRecord(pe_[s], st_));
    };
    mark(0);
    GAS_CHECK(cudaMemsetAsync(red_, 0, 4 * sizeof(float), st_));
    // 1 the cells: what came in, what burnt
    CellArgs ca{};
    ca.d = d;
    ca.origin = origin;
    ca.k = k;
    ca.s = Scalars{T, F, C, O, S, M};
    ca.V = Vel{u, v, w};
    ca.src = src;
    ca.solid = solid;
    GAS_CHECK(cudaMemcpyAsync(T2_, T, size_t(nx) * ny * nz * sizeof(float), cudaMemcpyDeviceToDevice, st_));
    ca.Tprev = T2_;
    ca.div = div_;
    ca.q = q;
    ca.rad = rad;
    ca.nut = nut_;
    ca.red = red_;
    ca.burner = burner;
    ca.dt = dt;
    kCells<<<g, b, 0, st_>>>(ca);
    if (jet.on) kJetAir<<<g, b, 0, st_>>>(d, origin, Scalars{T, F, C, O, S, M}, jet, k, dt);
    mark(1);
    // 2 the velocity carried by itself
    for (int comp = 0; comp < 3; ++comp) {
        float* cur = comp == 0 ? u : comp == 1 ? v : w;
        float* hat = comp == 0 ? u2_ : comp == 1 ? v2_ : w2_;
        float* out = comp == 0 ? u3_ : comp == 1 ? v3_ : w3_;
        AdvV av{d, Vel{u, v, w}, cur, hat, hat, comp, dt};
        const dim3 gv = grid3(nx + (comp == 0), ny + (comp == 1), nz + (comp == 2), b);
        kAdvectV1<<<gv, b, 0, st_>>>(av);
        av.outp = out;
        kAdvectV2<<<gv, b, 0, st_>>>(av);
    }
    std::swap(u, u3_);
    std::swap(v, v3_);
    std::swap(w, w3_);
    if (k.smag > 0.0f && k.smagMomentum > 0.0f)
        for (int comp = 0; comp < 3; ++comp) {
            float*& cur = comp == 0 ? u : comp == 1 ? v : w;
            float*& out = comp == 0 ? u3_ : comp == 1 ? v3_ : w3_;
            kDiffuseV<<<grid3(nx + (comp == 0), ny + (comp == 1), nz + (comp == 2), b), b, 0, st_>>>(d, cur, out, comp, nut_, dt);
            std::swap(cur, out);
        }
    mark(2);
    // 3 forces
    ForceArgs fa{d, origin, u, v, w, T, nullptr, k, jet, dt};
    kForces<<<grid3(nx + 1, ny + 1, nz + 1, b), b, 0, st_>>>(fa);
    if (k.vortEps > 0.0f) {
        kVorticity<<<g, b, 0, st_>>>(d, u, v, w, omega_);
        kConfine<<<g, b, 0, st_>>>(d, omega_, u, v, w, k.vortEps, dt);
    }
    mark(3);
    // 4 the projection
    project(dt);
    mark(4);
    // 5 the scalars carried by the new velocity
    AdvS as{};
    as.d = d;
    as.V = Vel{u, v, w};
    as.cur = CScalars{T, F, C, O, S, M};
    as.hat = Scalars{T2_, F2_, C2_, O2_, S2_, M2_};
    as.hatC = CScalars{T2_, F2_, C2_, O2_, S2_, M2_};
    as.k = k;
    as.dt = dt;
    as.solid = solid;
    as.mc = k.maccormack;
    kAdvectS1<<<g, b, 0, st_>>>(as);
    // (a third set: the correction reads the old values' and the hats' neighbours)
    as.out = Scalars{T3_, F3_, C3_, O3_, S3_, M3_};
    kAdvectS2<<<g, b, 0, st_>>>(as);
    std::swap(T, T3_);
    std::swap(F, F3_);
    std::swap(C, C3_);
    std::swap(O, O3_);
    std::swap(S, S3_);
    std::swap(M, M3_);
    mark(5);
    if (k.smag > 0.0f) {
        kDiffuseS<<<g, b, 0, st_>>>(d, CScalars{T, F, C, O, S, M}, Scalars{T3_, F3_, C3_, O3_, S3_, M3_}, nut_, solid, k, dt);
        std::swap(T, T3_);
        std::swap(F, F3_);
        std::swap(C, C3_);
        std::swap(O, O3_);
        std::swap(S, S3_);
        std::swap(M, M3_);
    }
    mark(6);
    GAS_CHECK(cudaEventRecord(e1_, st_));
    // the step's sums (read back once: the tools and the panel); the maximum was taken on the float's bits (positive)
    float r[6];
    GAS_CHECK(cudaMemcpyAsync(r, red_, sizeof(r), cudaMemcpyDeviceToHost, st_));
    GAS_CHECK(cudaStreamSynchronize(st_));
    stats.hrr = r[0];
    stats.rad = r[1];
    stats.fuelIn = r[2];
    stats.maxT = r[3];
    stats.residual = r[5] > 0.0f ? std::sqrt(r[4] / r[5]) : 0.0f;
    cudaEventElapsedTime(&stats.stepMs, e0_, e1_);
    if (profile)
        for (int s = 0; s < 6; ++s) {
            float ms = 0.0f;
            cudaEventElapsedTime(&ms, pe_[s], pe_[s + 1]);
            sectionMs[s] += ms;
        }
    ++stats.steps;
}

void AtesGas::clearSources() {
    const size_t n = size_t(nx) * ny * nz;
    for (float* p : {src.fuel, src.co, src.o2, src.heat, src.massT, src.ignite}) GAS_CHECK(cudaMemsetAsync(p, 0, n * sizeof(float), st_));
}

void AtesGas::pack(const float3* sootEmit) {
    const Dims d{nx, ny, nz, h};
    const dim3 b(32, 4);
    GAS_CHECK(cudaMemsetAsync(bricks_, 0, size_t(bnx) * bny * bnz * sizeof(unsigned int), st_));
    kPack<<<grid3(nx, ny, nz, b), b, 0, st_>>>(d, CScalars{T, F, C, O, S, M}, q, k, surf_, bricks_, bnx, bny);
    if (sootEmit)
        kLight<<<grid3(lnx, lny, lnz, dim3(8, 8)), dim3(8, 8), 0, st_>>>(d, CScalars{T, F, C, O, S, M}, k, sootEmit, lightDiv, lnx, lny, lnz, light_);
}

std::vector<float> AtesGas::download(int field) const {
    const size_t n = size_t(nx) * ny * nz;
    std::vector<float> h(n);
    const float* src_ = field == 0 ? T : field == 1 ? F : field == 2 ? C : field == 3 ? O : field == 4 ? S : field == 5 ? M : field == 6 ? q
                      : field == 7 ? rad : field == 8 ? div_ : phi_[0];
    GAS_CHECK(cudaStreamSynchronize(st_));
    GAS_CHECK(cudaMemcpy(h.data(), src_, n * sizeof(float), cudaMemcpyDeviceToHost));
    return h;
}

std::vector<float> AtesGas::downloadV() const {
    std::vector<float> vf(size_t(nx) * (ny + 1) * nz), out(size_t(nx) * ny * nz);
    GAS_CHECK(cudaStreamSynchronize(st_));
    GAS_CHECK(cudaMemcpy(vf.data(), v, vf.size() * sizeof(float), cudaMemcpyDeviceToHost));
    for (int k2 = 0; k2 < nz; ++k2)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                out[i + size_t(nx) * (j + size_t(ny) * k2)] = 0.5f * (vf[i + size_t(nx) * (j + size_t(ny + 1) * k2)] + vf[i + size_t(nx) * (j + 1 + size_t(ny + 1) * k2)]);
    return out;
}

void AtesGas::release() {
    if (st_) cudaStreamSynchronize(st_);
    if (tex_) cudaDestroyTextureObject(tex_);
    if (surf_) cudaDestroySurfaceObject(surf_);
    if (arr_) cudaFreeArray(arr_);
    tex_ = 0;
    surf_ = 0;
    arr_ = nullptr;
    for (void* p : owned_) cudaFree(p);
    owned_.clear();
    phi_.clear();
    rhs_.clear();
    res_.clear();
    msk_.clear();
    dims_.clear();
    if (e0_) cudaEventDestroy(e0_);
    if (e1_) cudaEventDestroy(e1_);
    e0_ = e1_ = nullptr;
    for (cudaEvent_t& e : pe_) {
        if (e) cudaEventDestroy(e);
        e = nullptr;
    }
    T = F = C = O = S = M = nullptr;
    u = v = w = nullptr;
    solid = nullptr;
    bytes = 0;
}
