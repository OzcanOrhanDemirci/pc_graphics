// Bonded components can solve in parallel because contacts read the integrated poses
// and only merge their corrections after the bond pass. Each component keeps the
// original colour order, and kinematic bodies never connect two components.
#pragma once

struct IfcBody {         // one side of an interface during its solve
    float im;
    Mat3 R, Iinv;
    float3 dx, dth;      // accumulated correction (world)
    __device__ float w(float3 r, float3 n) const { const float3 rn = cross(r, n); return im + dot(rn, Iinv.mul(rn)); }
    __device__ void push(float3 r, float3 p) {   // impulse p at lever r
        if (im <= 0.0f) return;
        dx = dx + p * im;
        dth = dth + Iinv.mul(cross(r, p));
    }
    __device__ float3 moved(float3 r) const { return dx + cross(dth, r); }
};

__device__ void retireInterface(DevWorld& world, const DInterface& face) {
    atomicSub(&world.bodyBonds[face.a], 1);
    atomicSub(&world.bodyBonds[face.b], 1);
    atomicExch(world.topologyDirty, 1);
}

__global__ void prepareComplianceKernel(DevWorld W, float hh) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.nFib) return;
    const DFiber& f = W.fib[i];
    W.compliance[i] = make_float3(1.0f / (f.kn * hh), 1.0f / (f.ks * hh), f.kSteel > 0.0f ? 1.0f / (f.kSteel * hh) : 0.0f);
}

// (Tried and measured: one warp per interface, lanes loading the fibres at once and walking them in lockstep, gave the
// same 11.6 ms: the cost is the 13 colours x 2 passes x 40 substeps of small kernels, each as long as its longest
// interface's serial walk, not memory waiting. 13 colours is the colouring's lower bound, the most interfaces on a body.)
__device__ void solveInterface(DevWorld& W, int k, float h, int iter) {
    const DInterface I = W.ifc[k];
    if (I.alive <= 0) return;
    const int a = I.a, b = I.b;
    const float4 qa = W.q[a], qb = W.q[b];
    IfcBody A, B;
    A.im = W.invMass[a]; B.im = W.invMass[b];
    A.R = rotMat(qa); B.R = rotMat(qb);
    A.Iinv = invInertiaWorld(A.R, W.invI[a]); B.Iinv = invInertiaWorld(B.R, W.invI[b]);
    A.dx = A.dth = B.dx = B.dth = make_float3(0, 0, 0);
    const float3 xab = diff3(W.x[b], W.x[a]);   // b's centre from a's
    const float4 qI = qmul(qa, I.qI);
    const float3 n = qrot(qI, make_float3(1, 0, 0)), t1 = qrot(qI, make_float3(0, 1, 0)), t2 = qrot(qI, make_float3(0, 0, 1));
    const PhysMaterial M = W.mats[I.mat];
    const float hh = h * h;
    const float forceScale = 1.0f / hh;
    int alive = 0;
    for (int f = I.first; f < I.first + I.count; ++f) {
        DFiber F = W.fib[f];
        if (F.flags & F_BROKEN) continue;
        const float3 compliance = W.compliance[f];
        // the multipliers the earlier passes of this substep applied (XPBD accumulates them; capacities bound the
        // total: measured, bounding each pass alone let two passes carry twice the yield moment)
        const float lam0 = iter == 0 ? 0.0f : -F.force.x * hh;
        const float l10 = iter == 0 ? 0.0f : -F.force.y * hh, l20 = iter == 0 ? 0.0f : -F.force.z * hh;
        const float3 ra = A.R.mul(F.rA), rb = B.R.mul(F.rB);
        const float wn = A.w(ra, n) + B.w(rb, n);
        // ---- normal (axial) part
        float3 sep = xab + rb + B.moved(rb) - ra - A.moved(ra);
        const float C = dot(sep, n) - F.plastic.x;
        float fn = 0.0f;   // total normal force this substep, >0 tension
        if (wn > 0.0f) {
            float at = compliance.x;
            float dl = __fdividef(-(C + at * lam0), wn + at);
            float lamT = lam0 + dl;
            fn = -lamT * forceScale;
            if (fn > 0.0f && !(F.flags & F_CRACKED) && fn > F.crackF) F.flags |= F_CRACKED;
            if (fn > 0.0f && (F.flags & F_CRACKED)) {
                if (F.kSteel <= 0.0f) {
                    // an open crack without bars: no tension, and an opening is a gap, not a permanent offset
                    lamT = 0.0f;
                    fn = 0.0f;
                    if (C > M.openBreak) F.flags |= F_BROKEN;
                } else {
                    at = compliance.z;
                    lamT = lam0 - __fdividef(C + at * lam0, wn + at);
                    fn = -lamT * forceScale;
                    if (fn > F.capSteel) {   // the bars yield
                        fn = F.capSteel;
                        lamT = -fn * hh;
                        F.plastic.x += C + wn * (lamT - lam0) + at * lamT;
                    }
                }
            } else if (fn < 0.0f) {
                const float cap = (F.flags & F_CRUSHED) ? F.capC * M.residual : F.capC;
                if (-fn > cap) {   // the concrete crushes
                    fn = -cap;
                    lamT = cap * hh;
                    F.plastic.x += C + wn * (lamT - lam0) + at * lamT;
                    if (F.plastic.x < -M.crushStart * F.len) F.flags |= F_CRUSHED;
                }
            }
            dl = lamT - lam0;
            const float3 p = n * dl;   // on b; -p on a
            B.push(rb, p);
            A.push(ra, p * -1.0f);
        }
        if (F.plastic.x > M.steelBreak || F.plastic.x < -M.crushBreak * F.len) F.flags |= F_BROKEN;
        F.force = make_float3(fn, 0.0f, 0.0f);
        // ---- shear part (two tangents), after the normal correction
        if (!(F.flags & F_BROKEN)) {
            sep = xab + rb + B.moved(rb) - ra - A.moved(ra);
            const float c1 = dot(sep, t1) - F.plastic.y, c2 = dot(sep, t2) - F.plastic.z;
            const float at = compliance.y;
            const float g1 = c1 + at * l10, g2 = c2 + at * l20;
            const float g2sum = g1 * g1 + g2 * g2;
            if (g2sum > 1e-28f) {
                const float3 dir = (t1 * g1 + t2 * g2) * rsqrtf(g2sum);
                const float ws = A.w(ra, dir) + B.w(rb, dir);
                if (ws > 0.0f) {
                    const float inverse = __fdividef(1.0f, ws + at);
                    float n1 = l10 - g1 * inverse, n2 = l20 - g2 * inverse;   // total multipliers
                    const float S = sqrtf(n1 * n1 + n2 * n2) * forceScale;
                    const float cap = ((F.flags & F_CRACKED) ? 0.0f : F.capCoh) + M.mu * fmaxf(0.0f, -fn) + F.capDowel;
                    if (S > cap) {   // slips: cohesion is gone, friction and dowels hold what they can
                        const float sc = cap / S;
                        n1 *= sc;
                        n2 *= sc;
                        F.plastic.y += c1 + ws * (n1 - l10) + at * n1;
                        F.plastic.z += c2 + ws * (n2 - l20) + at * n2;
                        F.flags |= F_CRACKED;
                    }
                    const float3 p = t1 * (n1 - l10) + t2 * (n2 - l20);
                    B.push(rb, p);
                    A.push(ra, p * -1.0f);
                    F.force.y = -n1 * forceScale;
                    F.force.z = -n2 * forceScale;
                }
            }
            if (F.plastic.y * F.plastic.y + F.plastic.z * F.plastic.z > M.slipBreak * M.slipBreak) F.flags |= F_BROKEN;
        }
        if (F.flags & F_BROKEN) F.force = make_float3(0, 0, 0);
        W.fib[f].plastic = F.plastic;
        W.fib[f].force = F.force;
        W.fib[f].flags = F.flags;
        if (!(F.flags & F_BROKEN)) ++alive;
    }
    if (A.im > 0.0f) { W.x[a] = W.x[a] + d3(A.dx); W.q[a] = qaddRot(qa, A.dth); }
    if (B.im > 0.0f) { W.x[b] = W.x[b] + d3(B.dx); W.q[b] = qaddRot(qb, B.dth); }
    if (alive != I.alive) {
        W.ifc[k].alive = alive;
        if (alive == 0) retireInterface(W, I);
    }
}

__global__ void breakInterfacesKernel(DevWorld world, int body) {
    for (int entry = world.adjStart[body] + threadIdx.x; entry < world.adjStart[body + 1]; entry += blockDim.x) {
        DInterface& face = world.ifc[world.adjList[entry]];
        if (face.alive > 0) retireInterface(world, face);
        face.alive = 0;
        for (int f = face.first; f < face.first + face.count; ++f) world.fib[f].flags |= F_BROKEN | F_CRACKED;
    }
}

__global__ void initializeIslandsKernel(DevWorld world) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < world.n) {
        world.islandParent[i] = i;
        world.islandFaceCount[i] = 0;
    }
}

__device__ __forceinline__ int islandRoot(int* parents, int body) {
    int next = atomicAdd(&parents[body], 0);
    while (next != body) {
        body = next;
        next = atomicAdd(&parents[body], 0);
    }
    return body;
}

__global__ void linkInterfaceIslandsKernel(DevWorld world) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= world.nIfc) return;
    const DInterface& face = world.ifc[i];
    if (face.alive <= 0 || world.kin[face.a] || world.kin[face.b]) return;
    int a = islandRoot(world.islandParent, face.a);
    int b = islandRoot(world.islandParent, face.b);
    while (a != b) {
        const int hi = max(a, b);
        const int lo = min(a, b);
        // Parent IDs strictly decrease, even when concurrent links meet.
        if (atomicCAS(&world.islandParent[hi], hi, lo) == hi) return;
        a = islandRoot(world.islandParent, hi);
        b = islandRoot(world.islandParent, lo);
    }
}

__global__ void compressInterfaceIslandsKernel(DevWorld world) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= world.n) return;
    atomicMin(&world.islandParent[i], islandRoot(world.islandParent, i));
}

__global__ void assignInterfaceIslandsKernel(DevWorld world) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= world.nIfc) return;
    const DInterface& face = world.ifc[i];
    if (face.alive <= 0) {
        world.faceIsland[i] = -1;
        return;
    }
    const int root = world.islandParent[world.kin[face.a] ? face.b : face.a];
    world.faceIsland[i] = root;
    const int entry = atomicAdd(&world.islandFaceCount[root], 1);
    if (entry < kIslandFaces) world.islandFaces[size_t(root) * kIslandFaces + entry] = i;
}

__global__ void collectSmallIslandsKernel(DevWorld world) {
    const int body = blockIdx.x * blockDim.x + threadIdx.x;
    if (body >= world.n) return;
    const int count = world.islandFaceCount[body];
    if (count > 0 && count <= kIslandFaces)
        world.smallIslands[atomicAdd(world.nSmallIslands, 1)] = body;
}

__global__ void solveIslandInterfacesKernel(DevWorld world, float h, int iterations) {
    constexpr unsigned kWarp = 0xffffffffu;
    const int lane = threadIdx.x & 31;
    const int count = *world.nSmallIslands;
    const int stride = gridDim.x * blockDim.x / kIslandFaces;
    for (int entry = (blockIdx.x * blockDim.x + threadIdx.x) / kIslandFaces; entry < count; entry += stride) {
        const int root = world.smallIslands[entry];
        const int face = lane < world.islandFaceCount[root] ? world.islandFaces[size_t(root) * kIslandFaces + lane] : -1;
        const unsigned bit = face >= 0 ? 1u << world.faceColour[face] : 0;
        const unsigned colours = __reduce_or_sync(kWarp, bit);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            for (unsigned remaining = colours; remaining; remaining &= remaining - 1u) {
                if (bit == (remaining & (0u - remaining))) solveInterface(world, face, h, iteration);
                __syncwarp(kWarp);
            }
        }
    }
}

__global__ void solveLargeInterfacesKernel(DevWorld world, const int* list, int count, float h, int iteration) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= count) return;
    const int face = list[t];
    if (world.nColours <= kIslandColours) {
        const int island = world.faceIsland[face];
        if (island < 0 || world.islandFaceCount[island] <= kIslandFaces) return;
    }
    solveInterface(world, face, h, iteration);
}

__device__ __forceinline__ bool dependencyReady(int* done, int previous, int wrap, int iteration) {
    const int dependency = previous >= 0 ? previous : wrap;
    const int required = iteration + (previous >= 0);
    if (dependency < 0 || required == 0) return true;
    cuda::atomic_ref<int, cuda::thread_scope_device> completed(done[dependency]);
    return completed.load(cuda::memory_order_acquire) >= required;
}

__device__ __forceinline__ int livePredecessor(const DevWorld& world, int face, int body) {
    while (face >= 0 && world.ifc[face].alive <= 0) {
        const int4 deps = world.faceDependencies[face];
        face = world.ifc[face].a == body ? deps.x : deps.y;
    }
    return face;
}

__global__ void collectLiveFacesKernel(DevWorld world) {
    constexpr unsigned kWarp = 0xffffffffu;
    const int face = blockIdx.x * blockDim.x + threadIdx.x;
    const bool live = face < world.nIfc && world.ifc[face].alive > 0;
    const unsigned mask = __ballot_sync(kWarp, live);
    if (!live) return;
    const DInterface& interface = world.ifc[face];
    const int4 links = world.faceDependencies[face];
    world.liveDependencies[face] = make_int4(livePredecessor(world, links.x, interface.a), livePredecessor(world, links.y, interface.b),
                                             livePredecessor(world, links.z, interface.a), livePredecessor(world, links.w, interface.b));
    const int lane = threadIdx.x & 31;
    const int leader = __ffs(mask) - 1;
    const int start = lane == leader ? atomicAdd(world.nLiveFaces, __popc(mask)) : 0;
    world.liveFaces[__shfl_sync(mask, start, leader) + __popc(mask & ((1u << lane) - 1u))] = face;
}

// Both input poses must include the last writer in colour order. The cooperative
// grid keeps every predecessor resident, so waiting lanes cannot starve a writer.
__global__ void __launch_bounds__(kDependencyThreads, 4) solveDependencyWorldKernel(DevWorld world, float h, int iterations) {
    const int entry = blockIdx.x * blockDim.x + threadIdx.x;
    const int face = entry < *world.nLiveFaces ? world.liveFaces[entry] : -1;
    const int4 deps = face >= 0 ? world.liveDependencies[face] : make_int4(-1, -1, -1, -1);
    int iteration = face >= 0 ? 0 : iterations;
    while (__any_sync(0xffffffffu, iteration < iterations)) {
        if (iteration < iterations && dependencyReady(world.faceDone, deps.x, deps.z, iteration) &&
            dependencyReady(world.faceDone, deps.y, deps.w, iteration)) {
            solveInterface(world, face, h, iteration);
            cuda::atomic_ref<int, cuda::thread_scope_device> completed(world.faceDone[face]);
            completed.store(++iteration, cuda::memory_order_release);
        }
        __syncwarp();
    }
}

static std::vector<int4> orderInterfaceDependencies(std::vector<std::vector<int>>& adjacent, const std::vector<DInterface>& faces,
                                                    const std::vector<int>& colours, const std::vector<unsigned>& kinematic) {
    std::vector<int4> result(faces.size(), make_int4(-1, -1, -1, -1));
    for (size_t body = 0; body < adjacent.size(); ++body) {
        auto& list = adjacent[body];
        if (kinematic[body] || list.size() < 2) continue;
        std::sort(list.begin(), list.end(), [&](int a, int b) { return colours[a] < colours[b]; });
        int previous = -1;
        for (int face : list) {
            int4& deps = result[face];
            if (faces[face].a == int(body)) {
                deps.x = previous;
                deps.z = list.back();
            } else {
                deps.y = previous;
                deps.w = list.back();
            }
            previous = face;
        }
    }
    return result;
}

static void prepareInterfaceIslands(DevWorld& world, cudaStream_t stream) {
    constexpr int kThreads = 128;
    if (world.faceDependencies) {
        PCHECK(cudaMemsetAsync(world.nLiveFaces, 0, sizeof(int), stream));
        collectLiveFacesKernel<<<(world.nIfc + kThreads - 1) / kThreads, kThreads, 0, stream>>>(world);
        return;
    }
    if (world.nIfc == 0 || world.nColours > kIslandColours) return;
    const int bodyBlocks = (world.n + kThreads - 1) / kThreads;
    const int faceBlocks = (world.nIfc + kThreads - 1) / kThreads;
    initializeIslandsKernel<<<bodyBlocks, kThreads, 0, stream>>>(world);
    linkInterfaceIslandsKernel<<<faceBlocks, kThreads, 0, stream>>>(world);
    compressInterfaceIslandsKernel<<<bodyBlocks, kThreads, 0, stream>>>(world);
    assignInterfaceIslandsKernel<<<faceBlocks, kThreads, 0, stream>>>(world);
    PCHECK(cudaMemsetAsync(world.nSmallIslands, 0, sizeof(int), stream));
    collectSmallIslandsKernel<<<bodyBlocks, kThreads, 0, stream>>>(world);
}
