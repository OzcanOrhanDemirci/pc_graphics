#include "scenes/tanecik/skeleton_sim.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// ---- quaternions (x, y, z, w) and the bodies' frames; host and device

__host__ __device__ inline float4 qmul(float4 a, float4 b) {
    return make_float4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
__host__ __device__ inline float4 qconj(float4 q) { return make_float4(-q.x, -q.y, -q.z, q.w); }
__host__ __device__ inline float4 qnormalize(float4 q) {
    const float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return make_float4(q.x / l, q.y / l, q.z / l, q.w / l);
}
__host__ __device__ inline float3 qrot(float4 q, float3 v) {
    const float3 u = make_float3(q.x, q.y, q.z);
    const float3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// q turned by a small rotation vector d (world): q += 1/2 [d, 0] q
__host__ __device__ inline void qturn(float4& q, float3 d) {
    const float4 r = qmul(make_float4(d.x, d.y, d.z, 0.0f), q);
    q = qnormalize(make_float4(q.x + 0.5f * r.x, q.y + 0.5f * r.y, q.z + 0.5f * r.z, q.w + 0.5f * r.w));
}
__host__ __device__ inline float3 mat3(const float* m, float3 v) {
    return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z);
}
// the world inverse inertia applied to u: R I0^-1 R^T u
__host__ __device__ inline float3 invI(const SkelBody& b, float3 u) {
    if (b.invMass <= 0.0f) return make_float3(0, 0, 0);
    return qrot(b.q, mat3(b.invI, qrot(qconj(b.q), u)));
}
__host__ __device__ inline float clampf_(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

// ---- constraints (XPBD, compliance 0: the joints are rigid)

// the two pivots brought together
__device__ void solvePivot(SkelBody& A, SkelBody& B, float3 la, float3 lb) {
    const float3 ra = qrot(A.q, la), rb = qrot(B.q, lb);
    const float3 d = (B.x + rb) - (A.x + ra);
    const float c = sqrtf(dot(d, d));
    if (c < 1e-9f) return;
    const float3 n = d / c;
    const float3 ca = cross(ra, n), cb = cross(rb, n);
    const float w = A.invMass + dot(ca, invI(A, ca)) + B.invMass + dot(cb, invI(B, cb));
    if (w <= 0.0f) return;
    const float3 p = n * (c / w);   // A towards B, B towards A
    A.x = A.x + p * A.invMass;
    qturn(A.q, invI(A, cross(ra, p)));
    B.x = B.x - p * B.invMass;
    qturn(B.q, invI(B, cross(rb, -p)));
}
// a rotation vector that, applied to A, would align it: shared between the two by their inertia about its axis
__device__ void solveTurn(SkelBody& A, SkelBody& B, float3 corr) {
    const float th = sqrtf(dot(corr, corr));
    if (th < 1e-7f) return;
    const float3 n = corr / th;
    const float w = dot(n, invI(A, n)) + dot(n, invI(B, n));
    if (w <= 0.0f) return;
    const float3 p = n * (th / w);
    qturn(A.q, invI(A, p));
    qturn(B.q, invI(B, -p));
}
// the signed angle from n1 to n2 about n (n1, n2 perpendicular to n)
__host__ __device__ inline float angleAbout(float3 n, float3 n1, float3 n2) { return atan2f(dot(cross(n1, n2), n), dot(n1, n2)); }
__host__ __device__ inline float3 perpUnit(float3 v, float3 n) {
    const float3 p = v - n * dot(v, n);
    const float l = sqrtf(dot(p, p));
    return l > 1e-9f ? p / l : make_float3(0, 0, 0);
}

__device__ void solveJoint(SkelBody* bodies, const SkelJoint& j) {
    SkelBody& A = bodies[j.a];
    SkelBody& B = bodies[j.b];
    solvePivot(A, B, j.la, j.lb);
    const float3 a1 = qrot(A.q, j.axis), a2 = qrot(B.q, j.axis);
    if (j.hinge) {
        solveTurn(A, B, cross(a1, a2));   // the axes aligned
        const float3 n = qrot(A.q, j.axis);
        const float3 n1 = perpUnit(qrot(A.q, j.ref), n), n2 = perpUnit(qrot(B.q, j.ref), n);
        const float phi = angleAbout(n, n1, n2);
        const float c = clampf_(phi, j.lo, j.hi);
        if (phi != c) solveTurn(A, B, n * (phi - c));
    } else {
        {   // swing: the child's axis within a cone about the parent's
            const float3 x = cross(a1, a2);
            const float s = sqrtf(dot(x, x));
            const float phi = atan2f(s, dot(a1, a2));
            if (phi > j.swing && s > 1e-7f) solveTurn(A, B, x / s * (phi - j.swing));
        }
        {   // twist about the mean axis
            const float3 b1 = qrot(A.q, j.axis), b2 = qrot(B.q, j.axis);
            float3 n = b1 + b2;
            const float l = sqrtf(dot(n, n));
            if (l > 1e-6f) {
                n = n / l;
                const float3 n1 = perpUnit(qrot(A.q, j.ref), n), n2 = perpUnit(qrot(B.q, j.ref), n);
                const float phi = angleAbout(n, n1, n2);
                const float c = clampf_(phi, j.lo, j.hi);
                if (phi != c) solveTurn(A, B, n * (phi - c));
            }
        }
    }
}

// the muscle: the child's rotation relative to the parent pulled towards the target, softly (XPBD compliance per
// substep) and no harder than its strength (an angular impulse of at most maxTorque * h per substep)
__device__ void solveMuscle(SkelBody& A, SkelBody& B, const SkelJoint& j, float h) {
    if (j.maxTorque <= 0.0f) return;
    // the error, in the parent's frame: what the relative rotation has beyond the target
    const float4 rel = qmul(qconj(A.q), B.q);
    float4 e = qmul(rel, qconj(j.target));
    if (e.w < 0.0f) e = make_float4(-e.x, -e.y, -e.z, -e.w);
    const float3 el = make_float3(e.x, e.y, e.z) * 2.0f;          // small-angle rotation vector, parent frame
    const float3 corr = qrot(A.q, el);                             // world: A turning this way would close it
    const float th = sqrtf(dot(corr, corr));
    if (th < 1e-7f) return;
    const float3 n = corr / th;
    const float w = dot(n, invI(A, n)) + dot(n, invI(B, n));
    if (w <= 0.0f) return;
    const float at = j.compliance / (h * h);
    float lam = th / (w + at);                                     // angular impulse x h (XPBD units)
    lam = fminf(lam, j.maxTorque * h * h);                         // the muscle's strength
    const float3 p = n * lam;
    qturn(A.q, invI(A, p));
    qturn(B.q, invI(B, -p));
}

// the ground under one body: its contact points that sink below the ground pushed up, their mean shared (Jacobi: a flat
// foot rises by the depth, not by the depth times its points); friction at the patch's centre, against how far that
// point slid this substep: static within muS times the normal correction so far, else slowed by muD
__device__ void solveGround(SkelBody& b, const float3* pts, int2 range, float radius, float muS, float muD, float& normalSum) {
    if (b.invMass <= 0.0f || range.y <= 0) return;
    float3 lin = make_float3(0, 0, 0), ang = make_float3(0, 0, 0), centre = make_float3(0, 0, 0);
    int count = 0;
    float depth = 0.0f;
    for (int k = 0; k < range.y; ++k) {
        const float3 r = qrot(b.q, pts[range.x + k]);
        const float3 p = b.x + r;
        const float pen = radius - p.y;
        if (pen <= 0.0f) continue;
        const float3 n = make_float3(0, 1, 0);
        const float3 rn = cross(r, n);
        const float w = b.invMass + dot(rn, invI(b, rn));
        const float lam = pen / w;
        lin = lin + n * lam;
        ang = ang + cross(r, n * lam);
        centre = centre + p;
        depth = fmaxf(depth, pen);
        ++count;
    }
    if (count == 0) return;
    const float inv = 1.0f / count;
    b.x = b.x + lin * (b.invMass * inv);
    qturn(b.q, invI(b, ang * inv));
    normalSum += sqrtf(dot(lin, lin)) * inv;
    // friction at the centre of the contact
    centre = centre * inv;
    const float3 r = centre - b.x;
    const float3 rl = qrot(qconj(b.q), r);
    const float3 prev = b.xPrev + qrot(b.qPrev, rl);
    float3 slide = centre - prev;
    slide.y = 0.0f;
    const float s = sqrtf(dot(slide, slide));
    if (s < 1e-9f) return;
    const float3 t = slide / s;
    const float3 rt = cross(r, t);
    const float w = b.invMass + dot(rt, invI(b, rt));
    float lam = s / w;   // what stops it
    const float limS = muS * normalSum, limD = muD * normalSum;
    if (lam > limS) lam = fminf(lam, limD);
    const float3 p = t * (-lam);
    b.x = b.x + p * b.invMass;
    qturn(b.q, invI(b, cross(r, p)));
}

// one substep, every body and joint (they are few: one thread, Gauss-Seidel in a fixed order: it repeats exactly)
__global__ void kStep(SkelBody* bodies, int nb, const SkelJoint* joints, int nj, float h, float3 g, int iters, float damping, const float3* pts,
                      const int2* range, SkelGround ground) {
    if (threadIdx.x != 0 || blockIdx.x != 0) return;
    for (int i = 0; i < nb; ++i) {
        SkelBody& b = bodies[i];
        b.xPrev = b.x;
        b.qPrev = b.q;
        if (b.invMass <= 0.0f) continue;
        b.v = b.v + g * h;
        b.x = b.x + b.v * h;
        {   // the gyroscopic term: w += h I^-1 (-w x I w), in the body's frame (a spinning limb precesses)
            const float3 wl = qrot(qconj(b.q), b.w);
            const float3 Iw = mat3(b.I, wl);
            const float3 dl = mat3(b.invI, cross(wl, Iw) * (-h));
            b.w = b.w + qrot(b.q, dl);
        }
        const float4 r = qmul(make_float4(b.w.x, b.w.y, b.w.z, 0.0f), b.q);
        b.q = qnormalize(make_float4(b.q.x + 0.5f * h * r.x, b.q.y + 0.5f * h * r.y, b.q.z + 0.5f * h * r.z, b.q.w + 0.5f * h * r.w));
    }
    float normalSum[B_COUNT];
    for (int i = 0; i < nb; ++i) normalSum[i] = 0.0f;
    for (int it = 0; it < iters; ++it) {
        for (int k = 0; k < nj; ++k) solveMuscle(bodies[joints[k].a], bodies[joints[k].b], joints[k], h);
        if (ground.on)
            for (int i = 0; i < nb; ++i) solveGround(bodies[i], pts, range[i], ground.radius, ground.muStatic, ground.muDynamic, normalSum[i]);
        for (int k = 0; k < nj; ++k) solveJoint(bodies, joints[k]);
    }
    for (int i = 0; i < nb; ++i) {
        SkelBody& b = bodies[i];
        if (b.invMass <= 0.0f) { b.v = b.w = make_float3(0, 0, 0); continue; }
        b.v = (b.x - b.xPrev) / h;
        const float4 dq = qmul(b.q, qconj(b.qPrev));
        const float sgn = dq.w < 0.0f ? -1.0f : 1.0f;
        b.w = make_float3(dq.x, dq.y, dq.z) * (2.0f * sgn / h);
    }
    // joint friction: each joint's relative spin damped (a share per second), shared by the two bodies' inertia
    const float k = fminf(1.0f, damping * h);
    if (k > 0.0f)
        for (int j = 0; j < nj; ++j) {
            SkelBody& A = bodies[joints[j].a];
            SkelBody& B = bodies[joints[j].b];
            const float3 dw = B.w - A.w;
            const float l = sqrtf(dot(dw, dw));
            if (l < 1e-9f) continue;
            const float3 n = dw / l;
            const float wa = dot(n, invI(A, n)), wb = dot(n, invI(B, n));
            if (wa + wb <= 0.0f) continue;
            const float3 p = n * (k * l / (wa + wb));   // an angular impulse moving the two spins towards each other
            A.w = A.w + invI(A, p);
            B.w = B.w - invI(B, p);
        }
}

void inverse3(const double* a, float* out) {
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    const double id = 1.0 / det;
    out[0] = float((a[4] * a[8] - a[5] * a[7]) * id);
    out[1] = float((a[2] * a[7] - a[1] * a[8]) * id);
    out[2] = float((a[1] * a[5] - a[2] * a[4]) * id);
    out[3] = float((a[5] * a[6] - a[3] * a[8]) * id);
    out[4] = float((a[0] * a[8] - a[2] * a[6]) * id);
    out[5] = float((a[2] * a[3] - a[0] * a[5]) * id);
    out[6] = float((a[3] * a[7] - a[4] * a[6]) * id);
    out[7] = float((a[1] * a[6] - a[0] * a[7]) * id);
    out[8] = float((a[0] * a[4] - a[1] * a[3]) * id);
}

}   // namespace

bool SkeletonSim::init(const BodyGraph& g, const BodyParticles& body, float particleMass, bool wholeSegments) {
    destroy();
    nb_ = B_COUNT;
    hRest_.assign(nb_, SkelBody{});
    mass_.assign(nb_, 0.0f);
    std::vector<std::vector<uint32_t>> segment(nb_);
    if (wholeSegments)
        for (size_t i = 0; i < g.bone.size(); ++i) segment[g.bone[i]].push_back(uint32_t(i));
    for (int b = 0; b < nb_; ++b) {
        const std::vector<uint32_t>& ps = wholeSegments ? segment[b] : g.boneParticles[b];
        if (ps.empty()) return false;
        double cx = 0, cy = 0, cz = 0;
        for (uint32_t i : ps) { cx += body.pos[i].x; cy += body.pos[i].y; cz += body.pos[i].z; }
        const double n = double(ps.size());
        const float3 c = make_float3(float(cx / n), float(cy / n), float(cz / n));
        // inertia about the centre (point masses, plus each particle's own small sphere so a thin bone is not singular)
        double I[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        const double own = 0.4 * double(particleMass) * 0.25 * double(body.spacing) * double(body.spacing);
        for (uint32_t i : ps) {
            const double rx = body.pos[i].x - c.x, ry = body.pos[i].y - c.y, rz = body.pos[i].z - c.z, m = particleMass;
            const double r2 = rx * rx + ry * ry + rz * rz;
            I[0] += m * (r2 - rx * rx) + own; I[1] -= m * rx * ry;            I[2] -= m * rx * rz;
            I[3] -= m * ry * rx;            I[4] += m * (r2 - ry * ry) + own; I[5] -= m * ry * rz;
            I[6] -= m * rz * rx;            I[7] -= m * rz * ry;            I[8] += m * (r2 - rz * rz) + own;
        }
        SkelBody& sb = hRest_[b];
        sb.x = sb.xPrev = sb.x0 = c;
        sb.q = sb.qPrev = make_float4(0, 0, 0, 1);
        sb.v = sb.w = make_float3(0, 0, 0);
        mass_[b] = float(n) * particleMass;
        sb.invMass = 1.0f / mass_[b];
        inverse3(I, sb.invI);
        for (int k = 0; k < 9; ++k) sb.I[k] = float(I[k]);
    }
    hJoints_.clear();
    for (const JointDef& jd : g.joints) {
        SkelJoint j;
        j.a = jd.parent;
        j.b = jd.child;
        j.la = jd.pivot - hRest_[j.a].x0;
        j.lb = jd.pivot - hRest_[j.b].x0;
        j.hinge = jd.hinge ? 1 : 0;
        j.axis = normalize(jd.axis);
        float3 t, u;
        makeBasis(j.axis, t, u);
        j.ref = t;
        j.lo = jd.lo;
        j.hi = jd.hi;
        j.swing = jd.swing;
        j.target = make_float4(0, 0, 0, 1);
        j.compliance = 0.0f;
        j.maxTorque = 0.0f;
        hJoints_.push_back(j);
    }
    nj_ = int(hJoints_.size());
    cudaMalloc(reinterpret_cast<void**>(&dBodies_), sizeof(SkelBody) * nb_);
    cudaMalloc(reinterpret_cast<void**>(&dJoints_), sizeof(SkelJoint) * nj_);
    cudaMemcpy(dBodies_, hRest_.data(), sizeof(SkelBody) * nb_, cudaMemcpyHostToDevice);
    cudaMemcpy(dJoints_, hJoints_.data(), sizeof(SkelJoint) * nj_, cudaMemcpyHostToDevice);
    return true;
}

void SkeletonSim::destroy() {
    for (void* p : {static_cast<void*>(dBodies_), static_cast<void*>(dJoints_), static_cast<void*>(dPts_), static_cast<void*>(dRange_)})
        if (p) cudaFree(p);
    dBodies_ = nullptr;
    dJoints_ = nullptr;
    dPts_ = nullptr;
    dRange_ = nullptr;
    ground_ = SkelGround{};
    nb_ = nj_ = 0;
}

void SkeletonSim::pin(int bone, bool on) {
    SkelBody b;
    cudaMemcpy(&b, dBodies_ + bone, sizeof(SkelBody), cudaMemcpyDeviceToHost);
    if (on) {
        b.invMass = 0.0f;
        std::memset(b.invI, 0, sizeof(b.invI));
        b.v = b.w = make_float3(0, 0, 0);
    } else {
        b.invMass = hRest_[bone].invMass;
        std::memcpy(b.invI, hRest_[bone].invI, sizeof(b.invI));
    }
    cudaMemcpy(dBodies_ + bone, &b, sizeof(SkelBody), cudaMemcpyHostToDevice);
}

void SkeletonSim::setPose(const Xform xf[B_COUNT]) {
    std::vector<SkelBody> b(nb_);
    cudaMemcpy(b.data(), dBodies_, sizeof(SkelBody) * nb_, cudaMemcpyDeviceToHost);
    for (int i = 0; i < nb_; ++i) {
        const float* m = xf[i].m;
        // the rotation's quaternion (Shepperd), the centre carried
        const float tr = m[0] + m[5] + m[10];
        float4 q;
        if (tr > 0.0f) {
            const float s = std::sqrt(tr + 1.0f) * 2.0f;
            q = make_float4((m[9] - m[6]) / s, (m[2] - m[8]) / s, (m[4] - m[1]) / s, 0.25f * s);
        } else if (m[0] > m[5] && m[0] > m[10]) {
            const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
            q = make_float4(0.25f * s, (m[1] + m[4]) / s, (m[2] + m[8]) / s, (m[9] - m[6]) / s);
        } else if (m[5] > m[10]) {
            const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
            q = make_float4((m[1] + m[4]) / s, 0.25f * s, (m[6] + m[9]) / s, (m[2] - m[8]) / s);
        } else {
            const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
            q = make_float4((m[2] + m[8]) / s, (m[6] + m[9]) / s, 0.25f * s, (m[4] - m[1]) / s);
        }
        b[i].q = b[i].qPrev = qnormalize(q);
        b[i].x = b[i].xPrev = xf[i].apply(hRest_[i].x0);
    }
    cudaMemcpy(dBodies_, b.data(), sizeof(SkelBody) * nb_, cudaMemcpyHostToDevice);
}

void SkeletonSim::setVelocity(float3 v, float3 w) {
    std::vector<SkelBody> b(nb_);
    cudaMemcpy(b.data(), dBodies_, sizeof(SkelBody) * nb_, cudaMemcpyDeviceToHost);
    double M = 0;
    float3 c = make_float3(0, 0, 0);
    for (int i = 0; i < nb_; ++i) { c = c + b[i].x * mass_[i]; M += mass_[i]; }
    c = c / float(M);
    for (int i = 0; i < nb_; ++i) {
        if (b[i].invMass <= 0.0f) continue;
        b[i].v = v + cross(w, b[i].x - c);
        b[i].w = w;
    }
    cudaMemcpy(dBodies_, b.data(), sizeof(SkelBody) * nb_, cudaMemcpyHostToDevice);
}

void SkeletonSim::step(float h, float3 gravity, int iterations, float jointDamping, cudaStream_t s) {
    kStep<<<1, 1, 0, s>>>(dBodies_, nb_, dJoints_, nj_, h, gravity, iterations, jointDamping, dPts_, dRange_, ground_);
}

void SkeletonSim::setGround(const BodyGraph& g, const BodyParticles& body, const SkelGround& ground, int every) {
    std::vector<float3> pts;
    std::vector<int2> range(nb_, make_int2(0, 0));
    std::vector<int> seen(nb_, 0);
    for (int b = 0; b < nb_; ++b) {
        range[b].x = int(pts.size());
        const bool foot = b == B_FOOT_L || b == B_FOOT_R;
        for (size_t i = 0; i < body.pos.size(); ++i) {
            if (g.bone[i] != b || g.kind[i] != PK_SKIN) continue;
            const bool sole = foot && body.pos[i].y < 0.02f;   // a foot stands on all of its sole
            if (!sole && (seen[b]++ % every) != 0) continue;
            pts.push_back(body.pos[i] - hRest_[b].x0);
        }
        range[b].y = int(pts.size()) - range[b].x;
    }
    if (dPts_) cudaFree(dPts_);
    if (dRange_) cudaFree(dRange_);
    cudaMalloc(reinterpret_cast<void**>(&dPts_), std::max<size_t>(1, pts.size()) * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&dRange_), range.size() * sizeof(int2));
    cudaMemcpy(dPts_, pts.data(), pts.size() * sizeof(float3), cudaMemcpyHostToDevice);
    cudaMemcpy(dRange_, range.data(), range.size() * sizeof(int2), cudaMemcpyHostToDevice);
    ground_ = ground;
    ground_.on = true;
}

void SkeletonSim::setMuscles(const std::vector<float4>& target, const std::vector<float>& compliance, const std::vector<float>& maxTorque) {
    for (int k = 0; k < nj_; ++k) {
        hJoints_[k].target = target[k];
        hJoints_[k].compliance = compliance[k];
        hJoints_[k].maxTorque = maxTorque[k];
    }
    cudaMemcpy(dJoints_, hJoints_.data(), sizeof(SkelJoint) * nj_, cudaMemcpyHostToDevice);
}

void SkeletonSim::readBodies(std::vector<SkelBody>& out) const {
    out.resize(nb_);
    cudaMemcpy(out.data(), dBodies_, sizeof(SkelBody) * nb_, cudaMemcpyDeviceToHost);
}

void SkeletonSim::push(int bone, float3 J, float3 at) {
    SkelBody b;
    cudaMemcpy(&b, dBodies_ + bone, sizeof(SkelBody), cudaMemcpyDeviceToHost);
    b.v = b.v + J * b.invMass;
    b.w = b.w + invI(b, cross(at - b.x, J));
    cudaMemcpy(dBodies_ + bone, &b, sizeof(SkelBody), cudaMemcpyHostToDevice);
}

float SkeletonSim::totalMass() const {
    float m = 0.0f;
    for (float x : mass_) m += x;
    return m;
}

void SkeletonSim::transforms(Xform out[B_COUNT]) {
    std::vector<SkelBody> b(nb_);
    cudaMemcpy(b.data(), dBodies_, sizeof(SkelBody) * nb_, cudaMemcpyDeviceToHost);
    for (int i = 0; i < nb_; ++i) {
        const float4 q = b[i].q;
        const float3 c0 = qrot(q, make_float3(1, 0, 0)), c1 = qrot(q, make_float3(0, 1, 0)), c2 = qrot(q, make_float3(0, 0, 1));
        float* m = out[i].m;
        m[0] = c0.x; m[1] = c1.x; m[2] = c2.x;
        m[4] = c0.y; m[5] = c1.y; m[6] = c2.y;
        m[8] = c0.z; m[9] = c1.z; m[10] = c2.z;
        const float3 t = b[i].x - qrot(q, hRest_[i].x0);   // p = R (p0 - x0) + x = R p0 + (x - R x0)
        m[3] = t.x;
        m[7] = t.y;
        m[11] = t.z;
    }
}

SkelStats SkeletonSim::stats(float3 g) {
    std::vector<SkelBody> b(nb_);
    cudaMemcpy(b.data(), dBodies_, sizeof(SkelBody) * nb_, cudaMemcpyDeviceToHost);
    SkelStats s;
    for (const SkelJoint& j : hJoints_) {
        const SkelBody &A = b[j.a], &B = b[j.b];
        const float3 d = (B.x + qrot(B.q, j.lb)) - (A.x + qrot(A.q, j.la));
        s.maxGap = std::fmax(s.maxGap, std::sqrt(dot(d, d)));
        const float3 a1 = qrot(A.q, j.axis), a2 = qrot(B.q, j.axis);
        float over = 0.0f;
        if (j.hinge) {
            const float3 n1 = perpUnit(qrot(A.q, j.ref), a1), n2 = perpUnit(qrot(B.q, j.ref), a1);
            const float phi = angleAbout(a1, n1, n2);
            over = std::fmax(0.0f, std::fmax(j.lo - phi, phi - j.hi));
            const float3 x = cross(a1, a2);
            over = std::fmax(over, std::asin(std::fmin(1.0f, std::sqrt(dot(x, x)))));   // the axes apart
        } else {
            const float3 x = cross(a1, a2);
            const float phi = std::atan2(std::sqrt(dot(x, x)), dot(a1, a2));
            over = std::fmax(0.0f, phi - j.swing);
            float3 n = a1 + a2;
            const float l = std::sqrt(dot(n, n));
            if (l > 1e-6f) {
                n = n / l;
                const float t = angleAbout(n, perpUnit(qrot(A.q, j.ref), n), perpUnit(qrot(B.q, j.ref), n));
                over = std::fmax(over, std::fmax(j.lo - t, t - j.hi));
            }
        }
        s.maxOver = std::fmax(s.maxOver, over);
    }
    double M = 0;
    float3 c = make_float3(0, 0, 0), P = make_float3(0, 0, 0);
    for (int i = 0; i < nb_; ++i) { c = c + b[i].x * mass_[i]; M += mass_[i]; P = P + b[i].v * mass_[i]; }
    c = c / float(M);
    for (int i = 0; i < nb_; ++i) {
        const float m = mass_[i];
        const float sp = std::sqrt(dot(b[i].v, b[i].v));
        if (sp > s.maxSpeed) { s.maxSpeed = sp; s.fastest = i; }
        // the inertia in the world: I w = R I0 R^T w, I0 the inverse of the rest inverse inertia
        double inv[9], I0[9];
        for (int k = 0; k < 9; ++k) inv[k] = hRest_[i].invI[k];
        float I0f[9];
        inverse3(inv, I0f);
        for (int k = 0; k < 9; ++k) I0[k] = I0f[k];
        const float3 lw = qrot(qconj(b[i].q), b[i].w);
        const float3 Il = make_float3(float(I0[0] * lw.x + I0[1] * lw.y + I0[2] * lw.z), float(I0[3] * lw.x + I0[4] * lw.y + I0[5] * lw.z),
                                      float(I0[6] * lw.x + I0[7] * lw.y + I0[8] * lw.z));
        const float3 Iw = qrot(b[i].q, Il);
        s.L = s.L + cross(b[i].x - c, b[i].v * m) + Iw;
        s.energy += 0.5f * m * dot(b[i].v, b[i].v) + 0.5f * dot(b[i].w, Iw) - m * dot(g, b[i].x);
    }
    (void)P;
    return s;
}
