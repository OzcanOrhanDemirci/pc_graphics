// Closest points for round wrecking balls. The renderer already supplies the
// sphere radius; generic convex pairs still use their original sampled hulls.
#pragma once

struct SphereContact {
    float3 normal;
    float depth;
};

__device__ __forceinline__ SphereContact sphereContact(float3 fromB, float radius, float otherRadius, float4 otherRotation,
                                                        float3 otherCentre, float3 otherHalf) {
    if (otherRadius > 0.0f) {
        const float distance = length(fromB);
        const float3 normal = distance > 0.0f ? fromB * (1.0f / distance) : make_float3(0, 1, 0);
        return {normal, radius + otherRadius - distance};
    }
    const float3 p = qrotInv(otherRotation, fromB) - otherCentre;
    const float3 nearest = make_float3(clampf(p.x, -otherHalf.x, otherHalf.x), clampf(p.y, -otherHalf.y, otherHalf.y),
                                       clampf(p.z, -otherHalf.z, otherHalf.z));
    const float3 delta = p - nearest;
    const float distance = length(delta);
    if (distance > 0.0f) return {qrot(otherRotation, delta * (1.0f / distance)), radius - distance};

    // An interior centre leaves the box through its nearest face.
    const float3 clearance = otherHalf - make_float3(fabsf(p.x), fabsf(p.y), fabsf(p.z));
    const int axis = clearance.x <= clearance.y && clearance.x <= clearance.z ? 0 : clearance.y <= clearance.z ? 1 : 2;
    const float3 normal = axisVector(axis, copysignf(1.0f, component(p, axis)));
    return {qrot(otherRotation, normal), radius + component(clearance, axis)};
}

__global__ void __launch_bounds__(128) solveSphereContactsKernel(DevWorld world, float h, PhysParams params) {
    const int frame = world.step->frame;
    const int stride = gridDim.x * blockDim.x;
    const int count = world.nActive[4];
    for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < count; i += stride) {
        const int2 pair = world.pairs[world.sphereActive[i]];
        const bool sphereIsA = world.sphereRadius[pair.x] > 0.0f;
        const int sphere = sphereIsA ? pair.x : pair.y;
        const int other = sphereIsA ? pair.y : pair.x;
        const float radius = world.sphereRadius[sphere], otherRadius = world.sphereRadius[other];
        PairState state = loadPair(world, pair.x, pair.y);
        PatchAcc last;
        float impulse = 0.0f;
        for (int iteration = 0; iteration < params.contactIters; ++iteration) {
            const float4 qp = sphereIsA ? state.qa : state.qb;
            const float4 qq = sphereIsA ? state.qb : state.qa;
            const float3 delta = sphereIsA ? state.rel() : -state.rel();
            const SphereContact contact = sphereContact(delta, radius, otherRadius, qq, world.localC[other], world.localH[other]);
            if (contact.depth <= params.slop || (params.deepOverlap > 0.0f && contact.depth > params.deepOverlap)) break;
            const PointCtx ctx = pointCtx(state, sphereIsA, rotMat(qp), rotMat(qq));
            PatchAcc acc;
            pointCorrection(state, ctx, sphereIsA, contact.normal * -radius, contact.normal, contact.depth, h, params, acc);
            acc.J = impulse;
            if (!applyPoints(state, acc)) break;
            impulse = acc.J;
            const bool done = iteration == params.contactIters - 1 || (iteration > 0 && acc.maxC < params.contactTol);
            patchFriction(world, pair.x, pair.y, frame, state, acc, params, done);
            last = acc;
            if (done) break;
        }
        finishPair(world, pair.x, pair.y, state, last, impulse);
    }
}
