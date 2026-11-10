// Analytic contacts for boxes aligned to the body's principal axes.
// Intersect the actual face boundaries and keep every vertex of the clipped polygon.
#pragma once

constexpr int kBoxFaceCount = 6;
constexpr int kFaceContactLanes = 8;
constexpr int kBoxContactThreads = 128;

struct BoxPose {
    float3 axis[3];
    float3 half;
    float3 centre;
};

__device__ __forceinline__ float component(float3 v, int axis) {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

__device__ __forceinline__ float3 axisVector(int axis, float value) {
    return make_float3(axis == 0 ? value : 0.0f, axis == 1 ? value : 0.0f, axis == 2 ? value : 0.0f);
}

__device__ __forceinline__ BoxPose boxPose(const DevWorld& world, int body, float4 rotation) {
    const Mat3 m = rotMat(rotation);
    return {{make_float3(m.r0.x, m.r1.x, m.r2.x), make_float3(m.r0.y, m.r1.y, m.r2.y), make_float3(m.r0.z, m.r1.z, m.r2.z)},
            world.localH[body], m.mul(world.localC[body])};
}

struct BoxAxis {
    int feature;
    float sign;
};

__device__ __forceinline__ bool stillSeparated(const BoxPose& a, const BoxPose& b, float3 delta, int feature, float slop) {
    if (feature < 0) return false;
    float3 axis;
    float axisLength = 1.0f;
    if (feature < kBoxFaceCount) {
        axis = feature < 3 ? a.axis[feature] : b.axis[feature - 3];
    } else {
        const int edge = feature - kBoxFaceCount;
        axis = cross(a.axis[edge / 3], b.axis[edge % 3]);
        const float norm = lengthSq(axis);
        if (norm < 1e-8f) return false;
        axisLength = sqrtf(norm);
    }
    const float radiusA = a.half.x * fabsf(dot(a.axis[0], axis)) + a.half.y * fabsf(dot(a.axis[1], axis)) +
                          a.half.z * fabsf(dot(a.axis[2], axis));
    const float radiusB = b.half.x * fabsf(dot(b.axis[0], axis)) + b.half.y * fabsf(dot(b.axis[1], axis)) +
                          b.half.z * fabsf(dot(b.axis[2], axis));
    return radiusA + radiusB - fabsf(dot(delta, axis)) < (slop - 1e-5f) * axisLength;
}

// The signed axis points from B towards A. The nine edge axes use their unnormalised
// tests until one can be the minimum, so almost parallel edges need no divisions.
__device__ __forceinline__ BoxAxis findBoxAxis(const BoxPose& a, const BoxPose& b, float3 fromB, float slop, int& separatedBy) {
    float relative[3][3];
    float alongA[3];
    float alongB[3];
    #pragma unroll
    for (int i = 0; i < 3; ++i) {
        alongA[i] = dot(fromB, a.axis[i]);
        alongB[i] = dot(fromB, b.axis[i]);
        #pragma unroll
        for (int j = 0; j < 3; ++j) relative[i][j] = dot(a.axis[i], b.axis[j]);
    }
    int feature = -1;
    float best = 1e30f;
    float sign = 1.0f;
    #pragma unroll
    for (int i = 0; i < kBoxFaceCount; ++i) {
        const int k = i % 3;
        const bool fromA = i < 3;
        const float centre = fromA ? alongA[k] : alongB[k];
        const float3 half = fromA ? b.half : a.half;
        const float rx = fabsf(fromA ? relative[k][0] : relative[0][k]);
        const float ry = fabsf(fromA ? relative[k][1] : relative[1][k]);
        const float rz = fabsf(fromA ? relative[k][2] : relative[2][k]);
        const float radius = half.x * rx + half.y * ry + half.z * rz;
        const float depth = component(fromA ? a.half : b.half, k) + radius - fabsf(centre);
        if (depth <= slop) {
            separatedBy = i;
            return {-1, 1.0f};
        }
        if (depth < best) {
            best = depth;
            feature = i;
            sign = copysignf(1.0f, centre);
        }
    }
    #pragma unroll
    for (int i = 0; i < 3; ++i) {
        const int u = (i + 1) % 3, v = (i + 2) % 3;
        #pragma unroll
        for (int j = 0; j < 3; ++j) {
            const int s = (j + 1) % 3, t = (j + 2) % 3;
            const float3 axis = cross(a.axis[i], b.axis[j]);
            const float normSquared = dot(axis, axis);
            if (normSquared < 1e-8f) continue;
            const float centre = alongA[v] * relative[u][j] - alongA[u] * relative[v][j];
            const float radiusA = component(a.half, u) * fabsf(relative[v][j]) + component(a.half, v) * fabsf(relative[u][j]);
            const float radiusB = component(b.half, s) * fabsf(relative[i][t]) + component(b.half, t) * fabsf(relative[i][s]);
            const float depth = radiusA + radiusB - fabsf(centre);
            if (depth <= 0.0f || depth * depth <= slop * slop * normSquared) {
                separatedBy = kBoxFaceCount + 3 * i + j;
                return {-1, 1.0f};
            }
            // A near tie uses a face for continuity when an edge is almost parallel to it.
            const float faceLimit = feature < kBoxFaceCount ? best * 0.98f : best;
            if (depth * depth < faceLimit * faceLimit * normSquared) {
                best = depth * rsqrtf(normSquared);
                feature = kBoxFaceCount + 3 * i + j;
                sign = copysignf(1.0f, centre);
            }
        }
    }
    separatedBy = -1;
    return {feature, sign};
}

struct BoxPoint {
    float3 incident;
    float3 reference;
    bool valid;
};

struct FaceClip {
    float2 centre;
    float2 u;
    float2 v;
    float2 half;
    float3 localCentre;
    float3 localU;
    float3 localV;
};

// The incident square uses coordinates [-1, 1]^2. Each of the 24 independent
// candidates is a corner of either square or the crossing of two of their sides.
__device__ __forceinline__ BoxPoint clipVertex(const FaceClip& clip, int vertex) {
    constexpr float kClipEpsilon = 1e-6f;
    float2 square;
    bool valid;
    if (vertex < 4) {
        square = make_float2((vertex & 1) ? 1.0f : -1.0f, (vertex & 2) ? 1.0f : -1.0f);
        const float2 p = clip.centre + clip.u * square.x + clip.v * square.y;
        valid = fabsf(p.x) <= clip.half.x && fabsf(p.y) <= clip.half.y;
    } else if (vertex < 8) {
        const float x = ((vertex & 1) ? clip.half.x : -clip.half.x) - clip.centre.x;
        const float y = ((vertex & 2) ? clip.half.y : -clip.half.y) - clip.centre.y;
        const float det = clip.u.x * clip.v.y - clip.u.y * clip.v.x;
        if (fabsf(det) <= 1e-12f) return {};
        const float inverse = 1.0f / det;
        square = make_float2((x * clip.v.y - y * clip.v.x) * inverse, (y * clip.u.x - x * clip.u.y) * inverse);
        valid = fabsf(square.x) < 1.0f - kClipEpsilon && fabsf(square.y) < 1.0f - kClipEpsilon;
    } else {
        const int side = (vertex - 8) / 4;
        const float fixed = (side & 1) ? 1.0f : -1.0f;
        const bool fixU = side < 2;
        const float2 start = clip.centre + (fixU ? clip.u : clip.v) * fixed;
        const float2 direction = fixU ? clip.v : clip.u;
        const bool clipX = (vertex & 2) == 0;
        const float factor = clipX ? direction.x : direction.y;
        if (fabsf(factor) <= 1e-9f) return {};
        const float extent = clipX ? clip.half.x : clip.half.y;
        const float edge = (vertex & 1) ? extent : -extent;
        const float t = (edge - (clipX ? start.x : start.y)) / factor;
        const float other = clipX ? start.y + t * direction.y : start.x + t * direction.x;
        valid = fabsf(t) < 1.0f - kClipEpsilon && fabsf(other) < (clipX ? clip.half.y : clip.half.x) - kClipEpsilon;
        square = fixU ? make_float2(fixed, t) : make_float2(t, fixed);
    }
    return {clip.localCentre + clip.localU * square.x + clip.localV * square.y, {}, valid};
}

__device__ __forceinline__ FaceClip projectIncidentFace(const DevWorld& world, const BoxPose& p, const BoxPose& q, int incident,
                                                        float3 centreFromRef, float3 normal, int refAxis) {
    int face = 0;
    float alignment = dot(p.axis[0], normal);
    for (int i = 1; i < 3; ++i) {
        const float candidate = dot(p.axis[i], normal);
        if (fabsf(candidate) > fabsf(alignment)) {
            alignment = candidate;
            face = i;
        }
    }
    const int refU = (refAxis + 1) % 3, refV = (refAxis + 2) % 3;
    const int incU = (face + 1) % 3, incV = (face + 2) % 3;
    const float side = -copysignf(component(p.half, face), alignment);
    const float3 centre = centreFromRef + p.axis[face] * side;
    const float3 u = p.axis[incU] * component(p.half, incU), v = p.axis[incV] * component(p.half, incV);
    return {make_float2(dot(centre, q.axis[refU]), dot(centre, q.axis[refV])),
            make_float2(dot(u, q.axis[refU]), dot(u, q.axis[refV])),
            make_float2(dot(v, q.axis[refU]), dot(v, q.axis[refV])),
            make_float2(component(q.half, refU), component(q.half, refV)),
            world.localC[incident] + axisVector(face, side),
            axisVector(incU, component(p.half, incU)), axisVector(incV, component(p.half, incV))};
}

__device__ __forceinline__ float3 supportEdge(const BoxPose& box, float3 localCentre, int axis, float3 direction) {
    for (int k = 1; k <= 2; ++k) {
        const int i = (axis + k) % 3;
        localCentre += axisVector(i, copysignf(component(box.half, i), dot(box.axis[i], direction)));
    }
    return localCentre;
}

__device__ BoxPoint closestEdgePoint(const DevWorld& world, const PairState& state, int a, int b, int axisA, int axisB, float sign) {
    const BoxPose boxA = boxPose(world, a, state.qa), boxB = boxPose(world, b, state.qb);
    const float3 u = boxA.axis[axisA], v = boxB.axis[axisB];
    const float3 normal = cross(u, v) * sign;
    const float3 localA = supportEdge(boxA, world.localC[a], axisA, -normal);
    const float3 localB = supportEdge(boxB, world.localC[b], axisB, normal);
    const float3 delta = state.rel() + qrot(state.qa, localA) - qrot(state.qb, localB);
    const float alignment = dot(u, v);
    const float au = dot(u, delta), bv = dot(v, delta);
    const float ha = component(boxA.half, axisA), hb = component(boxB.half, axisB);
    const float alongA = clampf((alignment * bv - au) / fmaxf(1e-8f, 1.0f - alignment * alignment), -ha, ha);
    const float alongB = clampf(bv + alignment * alongA, -hb, hb);
    const float endA = clampf(alignment * alongB - au, -ha, ha);
    return {localA + axisVector(axisA, endA), localB + axisVector(axisB, alongB), true};
}

// A clipped face has one normal. Accumulate in the two body frames, where inertia
// is diagonal, and rotate the sums once instead of transforming a Jacobian per point.
template <int Lanes, int Points>
__device__ void solveFacePoints(PairState& state, const BoxPoint (&points)[Points], bool incidentA, float3 normalQ, float plane,
                                float h, const PhysParams& params, PatchAcc& acc) {
    const float4 qp = incidentA ? state.qa : state.qb;
    const float4 qq = incidentA ? state.qb : state.qa;
    const float4 relative = qmul(qconj(qq), qp);
    const Mat3 rotation = rotMat(relative);
    const float3 normalP = qrotInv(relative, normalQ);
    const float3 translation = qrotInv(qq, incidentA ? state.rel() : -state.rel());
    const float3 inverseP = incidentA ? state.iIa : state.iIb;
    const float3 inverseQ = incidentA ? state.iIb : state.iIa;
    const float inverseMass = state.ima + state.imb;
    float3 angleP = make_float3(0, 0, 0);
    float3 angleQ = make_float3(0, 0, 0);
    #pragma unroll
    for (int i = 0; i < Points; ++i) {
        if (!points[i].valid) continue;
        const float3 leverP = points[i].incident;
        const float3 inQ = rotation.mul(leverP) + translation;
        const float depth = plane - dot(inQ, normalQ);
        if (params.deepOverlap > 0.0f && depth > params.deepOverlap) continue;
        const float correction = fminf(depth - fmaxf(0.0f, params.slop), params.maxDepenetration * h);
        if (correction <= 0.0f) continue;
        const float3 torqueP = cross(leverP, normalP);
        const float3 torqueQ = cross(inQ, normalQ);
        const float3 inertiaP = inverseP * torqueP;
        const float3 inertiaQ = inverseQ * torqueQ;
        const float weight = inverseMass + dot(torqueP, inertiaP) + dot(torqueQ, inertiaQ);
        if (weight <= 0.0f) continue;
        const float lambda = __fdividef(correction, weight);
        angleP += inertiaP * lambda;
        angleQ -= inertiaQ * lambda;
        const float3 leverA = incidentA ? leverP : inQ + normalQ * depth;
        acc.lam += lambda;
        acc.r += leverA * lambda;
        acc.rr += lambda * dot(leverA, leverA);
        ++acc.points;
        acc.depth = fmaxf(acc.depth, depth);
        acc.maxC = fmaxf(acc.maxC, correction);
    }
    acc.lam = tileSum<Lanes>(acc.lam);
    acc.r = qrot(state.qa, tileSum3<Lanes>(acc.r));
    acc.rr = tileSum<Lanes>(acc.rr);
    acc.points = int(tileSum<Lanes>(float(acc.points)));
    acc.depth = tileMax<Lanes>(acc.depth);
    acc.maxC = tileMax<Lanes>(acc.maxC);
    const float3 normalA = qrot(qq, incidentA ? normalQ : -normalQ);
    acc.n = normalA * acc.lam;
    state.dxa = acc.n * state.ima;
    state.dxb = acc.n * -state.imb;
    state.dta = qrot(state.qa, tileSum3<Lanes>(incidentA ? angleP : angleQ));
    state.dtb = qrot(state.qb, tileSum3<Lanes>(incidentA ? angleQ : angleP));
}

template <bool Edge>
__device__ void solveBoxPair(DevWorld& world, int a, int b, int feature, float sign, float h, const PhysParams& prm,
                            int frame, int lane) {
    constexpr int kLanes = Edge ? 1 : kFaceContactLanes;
    constexpr int kPointsPerLane = Edge ? 1 : 3;
    PairState state = loadPair(world, a, b);
    const bool incidentA = Edge || feature >= 3;
    const int refAxis = feature % 3;
    const int edgeA = Edge ? (feature - kBoxFaceCount) / 3 : 0;
    const int edgeB = Edge ? (feature - kBoxFaceCount) % 3 : 0;
    const int refBody = incidentA ? b : a;
    const float3 localNormal = axisVector(refAxis, incidentA ? sign : -sign);
    const float plane = component(world.localH[refBody], refAxis) + dot(world.localC[refBody], localNormal);
    BoxPoint points[kPointsPerLane];
    if constexpr (Edge) {
        points[0] = closestEdgePoint(world, state, a, b, edgeA, edgeB, sign);
    } else {
        const int incident = incidentA ? a : b;
        const float4 qp = incidentA ? state.qa : state.qb, qq = incidentA ? state.qb : state.qa;
        const BoxPose p = boxPose(world, incident, qp), q = boxPose(world, refBody, qq);
        const float3 centres = (incidentA ? state.rel() : -state.rel()) + p.centre - q.centre;
        const FaceClip clip = projectIncidentFace(world, p, q, incident, centres, qrot(qq, localNormal), refAxis);
        #pragma unroll
        for (int i = 0; i < kPointsPerLane; ++i) points[i] = clipVertex(clip, i * kLanes + lane);
        if (!__any_sync(tileMask<kLanes>(), points[0].valid || points[1].valid || points[2].valid)) return;
    }

    PatchAcc last;
    float impulse = 0.0f;
    for (int iteration = 0; iteration < prm.contactIters; ++iteration) {
        PatchAcc acc;
        if constexpr (Edge) {
            const PointCtx ctx = pointCtx(state, true, rotMat(state.qa), rotMat(state.qb));
            const float3 normal = normalize(cross(qrot(state.qa, axisVector(edgeA, sign)), qrot(state.qb, axisVector(edgeB, 1.0f))));
            const float3 lever = qrot(state.qa, points[0].incident);
            const float depth = dot(qrot(state.qb, points[0].reference) - (ctx.rel + lever), normal);
            if (prm.deepOverlap <= 0.0f || depth <= prm.deepOverlap)
                pointCorrection(state, ctx, true, lever, normal, depth, h, prm, acc);
        } else {
            solveFacePoints<kLanes>(state, points, incidentA, localNormal, plane, h, prm, acc);
        }
        acc.J = impulse;
        if (!applyPoints(state, acc)) break;
        impulse = acc.J;
        const bool done = iteration == prm.contactIters - 1 || (iteration > 0 && acc.maxC < prm.contactTol);
        frictionOnLane0<kLanes>(world, a, b, frame, state, acc, prm, done, lane);
        last = acc;
        if (done) break;
    }
    if (lane == 0) finishPair(world, a, b, state, last, impulse);
}

template <bool Edge>
__global__ void __launch_bounds__(kBoxContactThreads, 4) solveBoxContactsKernel(DevWorld world, float h, PhysParams params) {
    constexpr int kLanes = Edge ? 1 : kFaceContactLanes;
    const int lane = threadIdx.x & (kLanes - 1);
    const int tiles = gridDim.x * blockDim.x / kLanes;
    const int count = world.nActive[Edge ? 3 : 2];
    const int frame = world.step->frame;
    for (int i = (blockIdx.x * blockDim.x + threadIdx.x) / kLanes; i < count; i += tiles) {
        const uint2 entry = world.boxActive[Edge ? world.maxPairs - 1 - i : i];
        const int2 pair = world.pairs[entry.x];
        solveBoxPair<Edge>(world, pair.x, pair.y, int(entry.y >> 1), (entry.y & 1u) ? -1.0f : 1.0f, h, params, frame, lane);
    }
}
