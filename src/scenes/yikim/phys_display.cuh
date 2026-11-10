// Writes the renderer's snapshot and reduces the live damage counters on the GPU.
#pragma once

// transforms for the renderer: OptixInstance.transform is the first 12 floats (3x4 rows)
// Display: the reinforcing bars. A reinforced fibre owns two thin prisms (6 vertices each). While it holds and its crack
// is wider than minGap, one spans the crack (anchor on a to anchor on b). Once it is gone and the faces are apart, a
// stub sticks out of each face, bent a little (the bars of a failed member, stripped of their concrete). A prism not
// shown is NaN: OptiX skips it.
__device__ void barPrism(float3* v, float3 p0, float3 p1, float r) {
    const float3 d = p1 - p0;
    float3 u, w;
    makeBasis(d * (1.0f / fmaxf(length(d), 1e-9f)), u, w);
    for (int k = 0; k < 3; ++k) {
        const float a = k * 2.0943951f;
        const float3 o = (u * cosf(a) + w * sinf(a)) * r;
        v[k] = p0 + o;
        v[k + 3] = p1 + o;
    }
}
__device__ void barNone(float3* v) {
    const float nan = __int_as_float(0x7fc00000);
    for (int k = 0; k < 6; ++k) v[k] = make_float3(nan, nan, nan);
}
__device__ unsigned barHash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
__device__ float3 barBend(float3 n, unsigned h, float most) {
    float3 t, b;
    makeBasis(n, t, b);
    const float a = (h & 0xffffu) * (6.2831853f / 65536.0f), s = most * float((h >> 16) & 0xffu) / 255.0f;
    return normalize(n + (t * cosf(a) + b * sinf(a)) * s);
}
__global__ void kBars(DevWorld W, float3* v, float r, float minGap, float stub) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= W.nBars) return;
    const int2 e = W.bars[t];
    const DFiber F = W.fib[e.x];
    const DInterface I = W.ifc[e.y];
    float3* v0 = v + size_t(t) * 12;
    float3* v1 = v0 + 6;
    const float4 qa = W.q[I.a], qb = W.q[I.b];
    const float3 pa = f3(W.x[I.a]) + qrot(qa, F.rA), pb = f3(W.x[I.b]) + qrot(qb, F.rB);
    const float gap = length(pb - pa);
    if (!(F.flags & F_BROKEN)) {
        if (gap > minGap) barPrism(v0, pa, pb, r);
        else barNone(v0);
        barNone(v1);
    } else if (gap > 3.0f * minGap) {
        const unsigned h1 = barHash(unsigned(e.x) * 2u + 1u), h2 = barHash(unsigned(e.x) * 2u + 2u);
        const float3 na = qrot(qmul(qa, I.qI), make_float3(1, 0, 0)), nb = qrot(qb, W.ifcNB[e.y]);
        barPrism(v0, pa, pa + barBend(na, h1, 0.35f) * (stub * (0.6f + 0.4f * float(h2 >> 24) / 255.0f)), r);
        barPrism(v1, pb, pb + barBend(nb, h2, 0.35f) * (stub * (0.6f + 0.4f * float(h1 >> 24) / 255.0f)), r);
    } else {
        barNone(v0);
        barNone(v1);
    }
}

__global__ void kTransforms(DevWorld W, char* base, size_t stride, int first, int count) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= count) return;
    const int i = first + k;
    float3 r0, r1, r2;
    qmat(W.q[i], r0, r1, r2);
    const float3 x = f3(W.x[i]);
    float* m = reinterpret_cast<float*>(base + stride * size_t(k));
    m[0] = r0.x; m[1] = r0.y; m[2] = r0.z; m[3] = x.x;
    m[4] = r1.x; m[5] = r1.y; m[6] = r1.z; m[7] = x.y;
    m[8] = r2.x; m[9] = r2.y; m[10] = r2.z; m[11] = x.z;
}

__global__ void writeDamageKernel(DevWorld W, char* first, size_t stride) {
    const int body = blockIdx.x * blockDim.x + threadIdx.x + 1;
    if (body >= W.n) return;
    int total = 0;
    int twiceDamage = 0;
    for (int k = W.adjStart[body]; k < W.adjStart[body + 1]; ++k) {
        const DInterface& face = W.ifc[W.adjList[k]];
        total += face.count;
        for (int f = face.first; f < face.first + face.count; ++f) {
            const unsigned flags = W.fib[f].flags;
            twiceDamage += (flags & F_BROKEN) ? 2 : ((flags & (F_CRACKED | F_CRUSHED)) != 0);
        }
    }
    *reinterpret_cast<float*>(first + size_t(body - 1) * stride) = total > 0 ? 0.5f * float(twiceDamage) / float(total) : 0.0f;
}

__global__ void countDamageKernel(DevWorld W) {
    int live = 0;
    int broken = 0;
    int cracked = 0;
    const int stride = gridDim.x * blockDim.x;
    const int first = blockIdx.x * blockDim.x + threadIdx.x;
    for (int i = first; i < W.nIfc; i += stride) live += W.ifc[i].alive > 0;
    for (int i = first; i < W.nFib; i += stride) {
        const unsigned flags = W.fib[i].flags;
        broken += (flags & F_BROKEN) != 0;
        cracked += (flags & F_CRACKED) != 0;
    }
    live = __reduce_add_sync(0xffffffffu, live);
    broken = __reduce_add_sync(0xffffffffu, broken);
    cracked = __reduce_add_sync(0xffffffffu, cracked);
    if ((threadIdx.x & 31) == 0) {
        atomicAdd(&W.counts->x, live);
        atomicAdd(&W.counts->y, broken);
        atomicAdd(&W.counts->z, cracked);
    }
}

