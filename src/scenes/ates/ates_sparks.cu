// Scene 12 "Ateş": the sparks' kernels (ates_sparks.h).
#include "scenes/ates/ates_shared.h"
#include "scenes/ates/ates_sparks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define SPARK_CHECK(x)                                                                                  \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

namespace {

#define SIGMA 5.670374e-8f
#define SEED_CAP 4096u
#define BURST_CAP 1024u

struct SGas {
    int nx, ny, nz;
    float h;
    float3 origin;
    const float *T, *O, *u, *v, *w;
    const unsigned char* solid;
    float T0, Yo2;
    const float* plume;
};

// trilinear over an sx * sy * sz array at index coordinates (x, y, z), clamped to its ends
__device__ __forceinline__ float tri(const float* f, int sx, int sy, int sz, float x, float y, float z) {
    x = fminf(fmaxf(x, 0.0f), float(sx - 1) - 1e-3f);
    y = fminf(fmaxf(y, 0.0f), float(sy - 1) - 1e-3f);
    z = fminf(fmaxf(z, 0.0f), float(sz - 1) - 1e-3f);
    const int i = int(x), j = int(y), k = int(z);
    const float a = x - i, b = y - j, c = z - k;
    const int i1 = min(i + 1, sx - 1), j1 = min(j + 1, sy - 1), k1 = min(k + 1, sz - 1);
    auto at = [&](int ii, int jj, int kk) { return f[ii + sx * (jj + sy * kk)]; };
    const float c00 = at(i, j, k) * (1 - a) + at(i1, j, k) * a, c10 = at(i, j1, k) * (1 - a) + at(i1, j1, k) * a;
    const float c01 = at(i, j, k1) * (1 - a) + at(i1, j, k1) * a, c11 = at(i, j1, k1) * (1 - a) + at(i1, j1, k1) * a;
    return (c00 * (1 - b) + c10 * b) * (1 - c) + (c01 * (1 - b) + c11 * b) * c;
}

// the gas at a point: velocity, temperature, oxygen; above the grid the plume that leaves its top, beside it still air.
// Returns false inside a solid
__device__ bool gasAt(const SGas& g, float3 p, float3& U, float& T, float& O) {
    const float3 c = (p - g.origin) * (1.0f / g.h);
    const bool inXZ = c.x >= 0.0f && c.z >= 0.0f && c.x < g.nx && c.z < g.nz;
    if (inXZ && c.y >= 0.0f && c.y < g.ny) {
        const int ci = min(int(c.x), g.nx - 1), cj = min(int(c.y), g.ny - 1), ck = min(int(c.z), g.nz - 1);
        if (g.solid && g.solid[ci + g.nx * (cj + g.ny * ck)]) return false;
        U = make_float3(tri(g.u, g.nx + 1, g.ny, g.nz, c.x, c.y - 0.5f, c.z - 0.5f), tri(g.v, g.nx, g.ny + 1, g.nz, c.x - 0.5f, c.y, c.z - 0.5f),
                        tri(g.w, g.nx, g.ny, g.nz + 1, c.x - 0.5f, c.y - 0.5f, c.z));
        T = tri(g.T, g.nx, g.ny, g.nz, c.x - 0.5f, c.y - 0.5f, c.z - 0.5f);
        O = tri(g.O, g.nx, g.ny, g.nz, c.x - 0.5f, c.y - 0.5f, c.z - 0.5f);
        return true;
    }
    U = make_float3(0, 0, 0);
    T = g.T0;
    O = g.Yo2;
    if (c.y >= g.ny && g.plume[0] > 0.0f) {
        // a buoyant plume from a point source: its centreline speed falls as z^-1/3, its excess temperature as z^-5/3,
        // its width grows as z (z from the floor, the fire's virtual origin near it)
        const float top = g.origin.y + g.ny * g.h, z = fmaxf(p.y, top);
        const float s = z / top;
        const float b = sqrtf(g.plume[3]) * s;
        const float dx = p.x - g.plume[1], dz = p.z - g.plume[2];
        const float f = __expf(-(dx * dx + dz * dz) / fmaxf(b * b, 1e-6f));
        U.y = g.plume[4] * powf(s, -1.0f / 3.0f) * f;
        T = g.T0 + g.plume[5] * powf(s, -5.0f / 3.0f) * f;
    }
    return true;
}

__device__ __forceinline__ bool solidCell(const SGas& g, int i, int j, int k) {
    if (!g.solid || i < 0 || j < 0 || k < 0 || i >= g.nx || j >= g.ny || k >= g.nz) return false;
    return g.solid[i + g.nx * (j + g.ny * k)] != 0;
}

// the way out of a solid at a point: toward its free neighbours (the 26 around its cell), up when it has none
__device__ float3 solidNormal(const SGas& g, float3 p) {
    const float3 c = (p - g.origin) * (1.0f / g.h);
    const int ci = int(floorf(c.x)), cj = int(floorf(c.y)), ck = int(floorf(c.z));
    float3 n = make_float3(0, 0, 0);
    for (int dk = -1; dk <= 1; ++dk)
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di)
                if (!solidCell(g, ci + di, cj + dj, ck + dk)) n = n + make_float3(float(di), float(dj), float(dk));
    const float l = length(n);
    return l > 1e-3f ? n * (1.0f / l) : make_float3(0, 1, 0);
}

__device__ __forceinline__ bool inSolid(const SGas& g, float3 p) {
    const float3 c = (p - g.origin) * (1.0f / g.h);
    return solidCell(g, int(floorf(c.x)), int(floorf(c.y)), int(floorf(c.z)));
}

// what leaves the grid's top: weighted by the buoyant flux (upward speed times excess temperature)
__global__ void kPlume(SGas g, float* out) {
    __shared__ float s[6][1024];
    float a[6] = {0, 0, 0, 0, 0, 0};
    const int j = g.ny - 1;
    for (int c = threadIdx.x; c < g.nx * g.nz; c += blockDim.x) {
        const int i = c % g.nx, k = c / g.nx;
        const float vv = 0.5f * (g.v[i + g.nx * (j + (g.ny + 1) * k)] + g.v[i + g.nx * (j + 1 + (g.ny + 1) * k)]);
        const float dT = g.T[i + g.nx * (j + g.ny * k)] - g.T0;
        if (vv <= 0.0f || dT <= 0.0f) continue;
        const float wgt = vv * dT;
        const float x = g.origin.x + (i + 0.5f) * g.h, z = g.origin.z + (k + 0.5f) * g.h;
        a[0] += wgt;
        a[1] += wgt * x;
        a[2] += wgt * z;
        a[3] += wgt * (x * x + z * z);
        a[4] += wgt * vv;
        a[5] += wgt * dT;
    }
    for (int q = 0; q < 6; ++q) s[q][threadIdx.x] = a[q];
    __syncthreads();
    for (int o = blockDim.x / 2; o > 0; o >>= 1) {
        if (int(threadIdx.x) < o)
            for (int q = 0; q < 6; ++q) s[q][threadIdx.x] += s[q][threadIdx.x + o];
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        const float W = s[0][0];
        if (W <= 0.0f) {
            out[0] = 0.0f;
            return;
        }
        const float cx = s[1][0] / W, cz = s[2][0] / W;
        out[0] = W;
        out[1] = cx;
        out[2] = cz;
        out[3] = fmaxf(s[3][0] / W - cx * cx - cz * cz, g.h * g.h);
        out[4] = s[4][0] / W;
        out[5] = s[5][0] / W;
    }
}

// the host's bursts joined to the wood's seeds
__global__ void kAppend(const ASparkSeed* in, unsigned int n, ASparkSeed* seeds, unsigned int* counters) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const unsigned int at = atomicAdd(&counters[0], 1u);
    if (at < SEED_CAP) seeds[at] = in[i];
}

// the seeds into the ring (the oldest slots, whatever is in them: a spark lives seconds, the ring holds thousands)
__global__ void kBirth(const ASparkSeed* seeds, const unsigned int* counters, unsigned int cap, float4* pos, float4* prev, float4* vel, float* thick) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int n = min(counters[0], SEED_CAP);
    if (i >= n) return;
    const unsigned int slot = (counters[1] + i) % cap;
    const ASparkSeed s = seeds[i];
    pos[slot] = make_float4(s.p.x, s.p.y, s.p.z, s.d);
    prev[slot] = make_float4(s.p.x, s.p.y, s.p.z, s.T);
    vel[slot] = make_float4(s.v.x, s.v.y, s.v.z, s.T);
    thick[slot] = s.t;
}
__global__ void kAdvanceRing(unsigned int* counters, unsigned int cap) {
    const unsigned int n = min(counters[0], SEED_CAP);
    counters[1] = (counters[1] + n) % cap;
    counters[3] += n;
    counters[0] = 0;
    counters[2] = 0;
}

// one spark over dt: carried, burnt, heated and cooled (sub-steps of a few milliseconds: its drag and its heat follow
// the gas in tens of milliseconds, the gas changes along its path). A spark is a flake of char, d across and t thick
// (a crust split off the char's cells): its weight is its thickness's, its drag its breadth's, so a thin one rides the
// plume up as real sparks do and a thick one falls back
__global__ void kSpark(SGas g, unsigned int cap, float4* pos, float4* prev, float4* vel, float* thick, float dt, float rhoP, float eps, float kinA,
                       float groundH, unsigned int* dbg) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= cap) return;
    float4 P = pos[i];
    if (P.w < 0.0f) P.w = 0.0f;   // died in the last step (drawn then): free now
    if (P.w <= 0.0f) {
        pos[i] = P;
        return;
    }
    float4 V = vel[i];
    prev[i] = make_float4(P.x, P.y, P.z, V.w);
    float3 x = make_float3(P.x, P.y, P.z), v = make_float3(V.x, V.y, V.z);
    float d = P.w, t = thick[i], Tp = V.w;
    const int n = max(1, int(ceilf(dt / 0.004f)));
    const float h = dt / n;
    const float cp = 1200.0f;   // J/(kg K), char
    bool dead = false;
    for (int s = 0; s < n && !dead; ++s) {
        float3 ug;
        float Tg, Og;
        // born in a log's cell (its cells are coarser than its surface) or caught by a log settling: out on the free
        // side, a cell at a time
        bool inGas = gasAt(g, x, ug, Tg, Og);
        for (int k = 0; k < 3 && !inGas; ++k) {
            x = x + solidNormal(g, x) * g.h;
            inGas = gasAt(g, x, ug, Tg, Og);
        }
        if (!inGas) {
            dead = true;   // buried in a log or a stone
            if (dbg) atomicAdd(&dbg[0], 1u);
            break;
        }
        const float Tf = 0.5f * (Tp + Tg);
        const float mu = 1.8e-5f * powf(Tf / 293.0f, 0.7f), rhoF = 353.0f / Tf;
        const float3 rel = ug - v;
        const float Re = rhoF * length(rel) * d / mu;
        // drag and gravity, exact for the step's gas: a disc tumbling, its mean projected area a quarter of its
        // surface (pi d^2 / 8) with a sphere's drag coefficient (Schiller-Naumann): F = 1.5 pi mu d U (1 + 0.15 Re^0.687)
        const float tau = rhoP * d * t / (6.0f * mu * (1.0f + 0.15f * powf(Re, 0.687f)));
        const float e = __expf(-h / tau);
        const float3 a = ug + make_float3(0.0f, -9.81f * tau, 0.0f);
        const bool landed = x.y <= 0.0031f;
        if (!landed) {
            const float3 x0 = x;
            x = x + a * h + (v - a) * (tau * (1.0f - e));
            v = a + (v - a) * e;
            // it struck a log or a stone: back where it was, bounced off the face (a third of its speed into the face
            // comes back, rubbing takes some of its glide), so it skips over the bed or comes to rest on a log
            if (inSolid(g, x)) {
                const float3 nf = solidNormal(g, x);
                x = x0;
                const float vn = dot(v, nf);
                if (vn < 0.0f) v = (v - nf * vn) * 0.6f - nf * (0.3f * vn);
            }
        }
        // on the ground it lies where it fell, the ground under it drawing its heat (a contact conductance)
        if (x.y < 0.003f) {
            x.y = 0.003f;
            v = make_float3(0, 0, 0);
        }
        const float hGround = landed ? 0.5f * groundH : 0.0f;
        // the oxygen reaching its surface: the kinetic rate and the film's diffusion in series (Lewis number ~1)
        const float D = 2.2e-5f * powf(Tf / 300.0f, 1.75f), kAir = 0.026f * powf(Tf / 300.0f, 0.8f);
        const float sq = sqrtf(Re);
        const float hm = (2.0f + 0.6f * sq * 0.91f) * D / d;
        const float hc = (2.0f + 0.6f * sq * 0.89f) * kAir / d;
        // (a fragment is porous char: its inner surface burns too, its apparent rate a hundred times the log's; it
        // lights at ~700 K, as fine charcoal does)
        const float ks = kinA * __expf(-19000.0f / Tp);
        const float keff = 1.0f / (1.0f / fmaxf(ks, 1e-9f) + 1.0f / hm);
        const float rCO = 2500.0f * __expf(-6240.0f / Tp), fCO = rCO / (1.0f + rCO);
        const float sC = 2.667f * (1.0f - fCO) + 1.333f * fCO;
        const float mC = rhoF * Og * keff / sC;                       // kg of carbon per m^2 per s
        const float q = mC * ((1.0f - fCO) * 32.8e6f + fCO * 9.2e6f);  // W/m^2 at the surface (the CO burns away from it)
        // its heat, implicit in the loss (the convection and the radiation linearized)
        const float C = rhoP * cp * t * 0.5f;   // J/(m^2 K): its mass over its two faces
        const float T3 = Tp * Tp * Tp;
        Tp = (C / h * Tp + q + hc * Tg + hGround * 290.0f + eps * SIGMA * (3.0f * T3 * Tp + 300.0f * 300.0f * 300.0f * 300.0f)) /
             (C / h + hc + hGround + 4.0f * eps * SIGMA * T3);
        d -= 2.0f * mC / rhoP * h;
        t -= 2.0f * mC / rhoP * h;
        // burnt away, gone dark in gas too cool to light it again (under ~650 K it no longer shows nor burns), far off
        if (t < 1e-5f || (Tp < 650.0f && Tg < 900.0f) || x.y > 12.0f || x.x * x.x + x.z * x.z > 64.0f) {
            dead = true;
            if (dbg) atomicAdd(&dbg[t < 1e-5f ? 1 : Tp < 650.0f ? (landed ? 3 : 2) : 4], 1u);
        }
    }
    pos[i] = make_float4(x.x, x.y, x.z, dead ? -fmaxf(d, 1e-6f) : d);
    vel[i] = make_float4(v.x, v.y, v.z, Tp);
    thick[i] = t;
}

__device__ __forceinline__ float3 greyAt(const float3* t, float T) {
    const float tx = fminf(fmaxf((T - ATES_TABLE_T0) / ATES_TABLE_DT, 0.0f), float(ATES_TABLE_N - 1) - 0.001f);
    const int ti = int(tx);
    const float tf = tx - ti;
    return t[ti] * (1.0f - tf) + t[ti + 1] * tf;
}

// the path a spark took in the step, as the eye integrates it over the frame: its energy (luminance times the solid
// angle it fills, in pixels) spread evenly along the path, each sample splatted bilinearly, hidden where the surfaces'
// depth is nearer
__global__ void kDraw(unsigned int cap, const float4* pos, const float4* prev, const float4* vel, float4* hdr, const float* depth, int W, int H, float3 eye,
                      float3 U, float3 V, float3 Wd, float pixelAngle, const float3* grey, float eps, unsigned int* counters) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= cap) return;
    const float4 P = pos[i];
    if (P.w == 0.0f) return;
    const float d = fabsf(P.w);
    const float4 A = prev[i];
    const float TB = vel[i].w;
    atomicAdd(&counters[2], 1u);
    const float iu = 1.0f / dot(U, U), iv = 1.0f / dot(V, V);
    auto project = [&](float3 q, float2& px) {
        const float3 r = q - eye;
        const float z = dot(r, Wd);
        if (z < 0.05f) return false;
        px = make_float2((dot(r, U) * iu / z + 1.0f) * 0.5f * W, (1.0f - dot(r, V) * iv / z) * 0.5f * H);
        return true;
    };
    const float3 a = make_float3(A.x, A.y, A.z), b = make_float3(P.x, P.y, P.z);
    float2 pa, pb;
    if (!project(a, pa) || !project(b, pb)) return;
    const float len = sqrtf((pb.x - pa.x) * (pb.x - pa.x) + (pb.y - pa.y) * (pb.y - pa.y));
    const int n = min(max(int(ceilf(len * 1.5f)), 1), 512);
    const float pix2 = pixelAngle * pixelAngle;
    for (int k = 0; k < n; ++k) {
        const float s = (k + 0.5f) / n;
        const float3 q = a + (b - a) * s;
        const float T = A.w + (TB - A.w) * s;
        if (T < 700.0f) continue;
        const float x = pa.x + (pb.x - pa.x) * s - 0.5f, y = pa.y + (pb.y - pa.y) * s - 0.5f;
        if (x < -1.0f || y < -1.0f || x > W || y > H) continue;
        const float r = length(q - eye);
        const int cx = min(max(int(x + 0.5f), 0), W - 1), cy = min(max(int(y + 0.5f), 0), H - 1);
        if (r > depth[cy * W + cx] + 0.01f) continue;
        const float omega = 0.3926991f * d * d / (r * r);   // its mean projected area, pi d^2 / 8
        const float3 e = greyAt(grey, T) * (eps * omega / pix2 / n);
        const int x0 = int(floorf(x)), y0 = int(floorf(y));
        const float fx = x - x0, fy = y - y0;
        for (int jj = 0; jj < 2; ++jj)
            for (int ii = 0; ii < 2; ++ii) {
                const int X = x0 + ii, Y = y0 + jj;
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                const float w = (ii ? fx : 1.0f - fx) * (jj ? fy : 1.0f - fy);
                float4* o = &hdr[Y * W + X];
                atomicAdd(&o->x, e.x * w);
                atomicAdd(&o->y, e.y * w);
                atomicAdd(&o->z, e.z * w);
            }
    }
}

SGas sgas(const AtesGas& gas, const float* plume) {
    SGas g{};
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
    g.solid = gas.solid;
    g.T0 = gas.k.T0;
    g.Yo2 = gas.k.Yo2;
    g.plume = plume;
    return g;
}

}   // namespace

bool AtesSparks::init(unsigned int capacity, cudaStream_t st) {
    release();
    st_ = st;
    cap = capacity;
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&pos_), cap * sizeof(float4)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&prev_), cap * sizeof(float4)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&vel_), cap * sizeof(float4)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&thick_), cap * sizeof(float)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&seeds_), (SEED_CAP + BURST_CAP) * sizeof(ASparkSeed)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&counters_), 9 * sizeof(unsigned int)));
    SPARK_CHECK(cudaMalloc(reinterpret_cast<void**>(&plume_), 8 * sizeof(float)));
    SPARK_CHECK(cudaEventCreate(&e0_));
    SPARK_CHECK(cudaEventCreate(&e1_));
    SPARK_CHECK(cudaEventCreate(&e2_));
    SPARK_CHECK(cudaEventCreate(&e3_));
    clear();
    return true;
}

void AtesSparks::clear() {
    if (!pos_) return;
    SPARK_CHECK(cudaMemsetAsync(pos_, 0, cap * sizeof(float4), st_));
    SPARK_CHECK(cudaMemsetAsync(counters_, 0, 9 * sizeof(unsigned int), st_));
    SPARK_CHECK(cudaMemsetAsync(plume_, 0, 8 * sizeof(float), st_));
    stats = ASparkStats{};
}

ASparkSink AtesSparks::sink(float popPerKg, float flakePerKg) {
    ASparkSink s;
    s.seeds = seeds_;
    s.count = counters_;
    s.cap = SEED_CAP;
    s.popPerKg = popPerKg;
    s.flakePerKg = flakePerKg;
    s.frame = frame_;
    return s;
}

void AtesSparks::burst(const std::vector<ASparkSeed>& seeds) {
    if (!seeds_ || seeds.empty()) return;
    const unsigned int n = std::min<unsigned int>(unsigned(seeds.size()), BURST_CAP);
    ASparkSeed* stage = seeds_ + SEED_CAP;
    SPARK_CHECK(cudaMemcpyAsync(stage, seeds.data(), n * sizeof(ASparkSeed), cudaMemcpyHostToDevice, st_));
    kAppend<<<(n + 127) / 128, 128, 0, st_>>>(stage, n, seeds_, counters_);
}

void AtesSparks::step(float dt, const AtesGas& gas) {
    if (!pos_) return;
    SPARK_CHECK(cudaEventRecord(e0_, st_));
    const SGas g = sgas(gas, plume_);
    kPlume<<<1, 1024, 0, st_>>>(g, plume_);
    kBirth<<<(SEED_CAP + 127) / 128, 128, 0, st_>>>(seeds_, counters_, cap, pos_, prev_, vel_, thick_);
    kAdvanceRing<<<1, 1, 0, st_>>>(counters_, cap);
    kSpark<<<(cap + 127) / 128, 128, 0, st_>>>(g, cap, pos_, prev_, vel_, thick_, dt, rhoChar, emissivity, kinA, groundH, counters_ + 4);
    SPARK_CHECK(cudaEventRecord(e1_, st_));
    ++frame_;
}

void AtesSparks::draw(float4* hdr, const float* depth, int width, int height, float3 eye, float3 U, float3 V, float3 W, float pixelAngle, const float3* grey,
                      cudaStream_t st) {
    if (!pos_) return;
    SPARK_CHECK(cudaEventRecord(e2_, st));
    kDraw<<<(cap + 127) / 128, 128, 0, st>>>(cap, pos_, prev_, vel_, hdr, depth, width, height, eye, U, V, W, pixelAngle, grey, emissivity, counters_);
    SPARK_CHECK(cudaEventRecord(e3_, st));
    timed_ = true;
}

void AtesSparks::readStats() {
    if (!counters_) return;
    unsigned int c[9];
    SPARK_CHECK(cudaMemcpy(c, counters_, sizeof(c), cudaMemcpyDeviceToHost));
    stats.alive = c[2];
    stats.born = c[3];
    for (int k = 0; k < 5; ++k) stats.deaths[k] = c[4 + k];
    float a = 0.0f, b = 0.0f;
    if (timed_ && cudaEventElapsedTime(&a, e0_, e1_) == cudaSuccess && cudaEventElapsedTime(&b, e2_, e3_) == cudaSuccess) stats.ms = a + b;
}

void AtesSparks::snapshot(std::vector<float4>& pos, std::vector<float4>& vel) const {
    pos.clear();
    vel.clear();
    if (!pos_) return;
    std::vector<float4> P(cap), V(cap);
    SPARK_CHECK(cudaMemcpy(P.data(), pos_, cap * sizeof(float4), cudaMemcpyDeviceToHost));
    SPARK_CHECK(cudaMemcpy(V.data(), vel_, cap * sizeof(float4), cudaMemcpyDeviceToHost));
    for (unsigned int i = 0; i < cap; ++i)
        if (P[i].w > 0.0f) {
            pos.push_back(P[i]);
            vel.push_back(V[i]);
        }
}

void AtesSparks::release() {
    for (void* p : {static_cast<void*>(pos_), static_cast<void*>(prev_), static_cast<void*>(vel_), static_cast<void*>(thick_), static_cast<void*>(seeds_), static_cast<void*>(counters_),
                    static_cast<void*>(plume_)})
        if (p) cudaFree(p);
    pos_ = prev_ = vel_ = nullptr;
    thick_ = nullptr;
    seeds_ = nullptr;
    counters_ = nullptr;
    plume_ = nullptr;
    for (cudaEvent_t e : {e0_, e1_, e2_, e3_})
        if (e) cudaEventDestroy(e);
    e0_ = e1_ = e2_ = e3_ = nullptr;
    cap = 0;
    timed_ = false;
}
