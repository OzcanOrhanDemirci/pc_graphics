// Scene 12 "Ateş": the logs as rigid bodies (ates_body.h).
#include "scenes/ates/ates_body.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

// quaternions as ALog keeps them: (w, x, y, z) in a float4's (x, y, z, w)
float4 qmul(float4 a, float4 b) {
    return make_float4(a.x * b.x - a.y * b.y - a.z * b.z - a.w * b.w, a.x * b.y + a.y * b.x + a.z * b.w - a.w * b.z,
                       a.x * b.z - a.y * b.w + a.z * b.x + a.w * b.y, a.x * b.w + a.y * b.z - a.z * b.y + a.w * b.x);
}
float4 qnorm(float4 q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0.0f ? q * (1.0f / l) : make_float4(1, 0, 0, 0);
}
float3 qrot(float4 q, float3 v) {
    const float3 u = make_float3(q.y, q.z, q.w);
    const float3 t = cross(u, v) * 2.0f;
    return v + t * q.x + cross(u, t);
}
float3 qrotInv(float4 q, float3 v) { return qrot(make_float4(q.x, -q.y, -q.z, -q.w), v); }

struct Body {
    int log = -1;
    bool dynamic = false;
    float invMass = 0.0f;
    float3 invI{0, 0, 0};          // the inverse inertia about the log's own axes (x along it)
    float3 x{0, 0, 0}, xPrev{0, 0, 0}, v{0, 0, 0}, w{0, 0, 0};
    float4 q{1, 0, 0, 0}, qPrev{1, 0, 0, 0};
    float3 axis{1, 0, 0};
    float half = 0.0f;             // the capsule's half segment
    float rMax = 0.0f;
    const ALog* L = nullptr;
};

float3 applyInvI(const Body& b, float3 v) {
    if (!b.dynamic) return make_float3(0, 0, 0);
    const float3 l = qrotInv(b.q, v);
    return qrot(b.q, make_float3(l.x * b.invI.x, l.y * b.invI.y, l.z * b.invI.z));
}
float genInvMass(const Body& b, float3 r, float3 n) {
    if (!b.dynamic) return 0.0f;
    const float3 rn = cross(r, n);
    return b.invMass + dot(rn, applyInvI(b, rn));
}
void applyPos(Body& b, float3 r, float3 P) {
    if (!b.dynamic) return;
    b.x += P * b.invMass;
    const float3 dw = applyInvI(b, cross(r, P));
    b.q = qnorm(b.q + qmul(make_float4(0, dw.x, dw.y, dw.z), b.q) * 0.5f);
}
void applyVel(Body& b, float3 r, float3 p) {
    if (!b.dynamic) return;
    b.v += p * b.invMass;
    b.w += applyInvI(b, cross(r, p));
}

// the log's radius at a fraction of its length (its rings', linear between their middles)
float radiusAt(const ALog& L, float f) {
    if (L.ringRadius.empty()) return L.radius;
    const float x = std::clamp(f * float(L.nZ) - 0.5f, 0.0f, float(L.nZ - 1));
    const int i = std::min(int(x), L.nZ - 2 >= 0 ? L.nZ - 2 : 0);
    const float t = x - float(i);
    const float a = L.ringRadius[size_t(i)], b = L.nZ > 1 ? L.ringRadius[size_t(i + 1)] : a;
    return a + (b - a) * t;
}
// a point of the capsule's segment (s in 0..1) as a fraction of the log's length
float fracOf(const Body& b, float s) { return std::clamp((s * 2.0f * b.half - b.half) / b.L->len + 0.5f, 0.0f, 1.0f); }

// the closest points of two segments p1 + s d1 and p2 + t d2, s and t in 0..1 (Ericson, Real-Time Collision Detection)
void closestSegments(float3 p1, float3 d1, float3 p2, float3 d2, float& s, float& t) {
    const float3 r = p1 - p2;
    const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    const float c = dot(d1, r), bb = dot(d1, d2);
    const float den = a * e - bb * bb;
    s = den > 1e-9f ? std::clamp((bb * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
    t = (bb * s + f) / e;
    if (t < 0.0f) {
        t = 0.0f;
        s = std::clamp(-c / a, 0.0f, 1.0f);
    } else if (t > 1.0f) {
        t = 1.0f;
        s = std::clamp((bb - c) / a, 0.0f, 1.0f);
    }
}

struct Contact {
    int a = -1, b = -1;            // bodies (b -1: the ground or a stone)
    float3 n{0, 1, 0};             // from b to a
    float3 la{0, 0, 0}, lb{0, 0, 0};   // the points in the bodies' own frames (b's in the world when fixed)
    float lambda = 0.0f;
    float vnPre = 0.0f;
};

}   // namespace

void AtesBodies::lower(const std::vector<ALog>& logs, ALog& L) const {
    auto clear = [&](float3 at) {
        const float3 ax = qrot(qnorm(L.q), make_float3(1, 0, 0));
        const float half = std::max(0.5f * L.len - 0.5f * L.r0, 0.0f);
        const float3 a0 = at - ax * half, d0 = ax * (2.0f * half);
        for (int k = 0; k <= 4; ++k) {
            const float3 p = at + ax * ((k / 4.0f - 0.5f) * L.len);
            const float3 up = make_float3(0, 1, 0);
            const float3 dn = up - ax * dot(ax, up);
            const float dl = length(dn);
            const float3 low = dl > 1e-3f ? p - dn * (L.r0 / dl) : p - make_float3(0, L.r0, 0);
            if (low.y < (ground ? ground(low.x, low.z) : 0.0f) + 0.001f) return false;
        }
        for (const ABodyObstacle& o : stones) {
            float s, t;
            closestSegments(a0, d0, o.a, o.b - o.a, s, t);
            if (length((a0 + d0 * s) - (o.a + (o.b - o.a) * t)) < L.r0 + o.r + 0.002f) return false;
        }
        for (const ALog& M : logs) {
            if (&M == &L || !M.alive) continue;
            const float3 bx = qrot(qnorm(M.q), make_float3(1, 0, 0));
            const float bh = std::max(0.5f * M.len - 0.5f * M.radius, 0.0f);
            const float3 a1 = M.pos - bx * bh, d1 = bx * (2.0f * bh);
            float s, t;
            closestSegments(a0, d0, a1, d1, s, t);
            if (length((a0 + d0 * s) - (a1 + d1 * t)) < L.r0 + M.radius + 0.002f) return false;
        }
        return true;
    };
    // down in two-millimetre steps from where it was put while it stays clear (it may start inside something: then up)
    float3 at = L.pos;
    while (!clear(at) && at.y < 2.0f) at.y += 0.002f;
    while (at.y > -0.5f) {
        const float3 nx = at - make_float3(0, 0.002f, 0);
        if (!clear(nx)) break;
        at = nx;
    }
    L.pos = at;
}

void AtesBodies::wakeAll(std::vector<ALog>& logs) {
    for (ALog& L : logs) {
        L.sleeping = false;
        L.sleepTime = 0.0f;
    }
}

void AtesBodies::step(std::vector<ALog>& logs, float dt) {
    const auto t0 = std::chrono::steady_clock::now();
    stats = ABodyStats();
    if (logs.empty() || dt <= 0.0f) return;
    std::vector<Body> B(logs.size());
    int awake = 0;
    for (size_t i = 0; i < logs.size(); ++i) {
        ALog& L = logs[i];
        Body& b = B[i];
        b.L = &L;
        b.log = int(i);
        b.x = L.pos;
        b.q = qnorm(L.q);
        b.v = L.vel;
        b.w = L.angVel;
        b.rMax = 0.0f;
        for (float r : L.ringRadius) b.rMax = std::max(b.rMax, r);
        if (L.ringRadius.empty()) b.rMax = L.radius;
        b.half = std::max(0.5f * L.len - 0.5f * L.radius, 0.0f);
        b.dynamic = L.alive && !L.sleeping && L.mass > 1e-4f && L.radius > 1e-4f;
        if (b.dynamic) {
            const float m = L.mass, r = L.radius;
            b.invMass = 1.0f / m;
            b.invI = make_float3(1.0f / (0.5f * m * r * r), 12.0f / (m * (3.0f * r * r + L.len * L.len)), 12.0f / (m * (3.0f * r * r + L.len * L.len)));
            ++awake;
        }
    }
    stats.awake = awake;
    if (awake == 0) return;
    const int ns = std::max(1, substeps);
    const float h = dt / float(ns);
    const float3 g = make_float3(0.0f, -9.81f, 0.0f);
    std::vector<Contact> C;
    std::vector<char> wake(B.size(), 0);
    std::vector<char> touched(B.size(), 0);
    for (int sub = 0; sub < ns; ++sub) {
        std::fill(touched.begin(), touched.end(), 0);
        for (Body& b : B) {
            b.xPrev = b.x;
            b.qPrev = b.q;
            if (!b.dynamic) continue;
            b.v += g * h;
            b.w *= std::exp(-0.4f * h);   // (the air's and the bark's small losses on a spin)
            b.x += b.v * h;
            b.q = qnorm(b.q + qmul(make_float4(0, b.w.x, b.w.y, b.w.z), b.q) * (0.5f * h));
        }
        for (Body& b : B) b.axis = qrot(b.q, make_float3(1, 0, 0));
        // the contacts at the predicted poses
        C.clear();
        auto add = [&](int a, int bi, float3 n, float3 pa, float3 pb) {
            Contact c;
            c.a = a;
            c.b = bi;
            c.n = n;
            const Body& A = B[size_t(a)];
            c.la = qrotInv(A.q, pa - A.x);
            if (bi >= 0) {
                const Body& Bb = B[size_t(bi)];
                c.lb = qrotInv(Bb.q, pb - Bb.x);
                const float3 vb = Bb.v + cross(Bb.w, pb - Bb.x);
                c.vnPre = dot(n, (A.v + cross(A.w, pa - A.x)) - vb);
            } else {
                c.lb = pb;
                c.vnPre = dot(n, A.v + cross(A.w, pa - A.x));
            }
            C.push_back(c);
        };
        for (size_t i = 0; i < B.size(); ++i) {
            const Body& b = B[i];
            if (!b.dynamic) continue;
            const ALog& L = *b.L;
            // the ground under the rim: the lowest point of the section at five places along the log, and the end
            // faces' rims (a log stood on its end rests on its face, not on a point)
            const float3 up = make_float3(0, 1, 0);
            float3 dn = up - b.axis * dot(b.axis, up);
            const float dl = length(dn);
            float3 e1, e2;
            {
                const float3 t = std::fabs(b.axis.y) < 0.9f ? up : make_float3(1, 0, 0);
                e1 = normalize(cross(b.axis, t));
                e2 = cross(b.axis, e1);
            }
            auto groundAt = [&](float3 p) { return ground ? ground(p.x, p.z) : 0.0f; };
            for (int k = 0; k < 5; ++k) {
                const float f = k / 4.0f;
                const float3 c = b.x + b.axis * ((f - 0.5f) * L.len);
                const float r = radiusAt(L, f);
                const float3 p = dl > 1e-3f ? c - dn * (r / dl) : c;
                const float gy = groundAt(p);
                if (p.y < gy + 0.002f) add(int(i), -1, up, p, make_float3(p.x, gy, p.z));
            }
            for (int end = 0; end < 2; ++end) {
                const float f = float(end);
                const float3 c = b.x + b.axis * ((f - 0.5f) * L.len);
                const float r = radiusAt(L, f);
                const float3 rim[4] = {c + e1 * r, c - e1 * r, c + e2 * r, c - e2 * r};
                for (const float3& p : rim) {
                    const float gy = groundAt(p);
                    if (p.y < gy + 0.002f) add(int(i), -1, up, p, make_float3(p.x, gy, p.z));
                }
            }
            // the pit's stones
            const float3 a0 = b.x - b.axis * b.half, d0 = b.axis * (2.0f * b.half);
            for (const ABodyObstacle& o : stones) {
                float s, t;
                const float3 od = o.b - o.a;
                if (dot(od, od) < 1e-10f) {
                    t = 0.0f;
                    s = std::clamp(dot(o.a - a0, d0) / std::max(dot(d0, d0), 1e-10f), 0.0f, 1.0f);
                } else closestSegments(a0, d0, o.a, od, s, t);
                const float3 pa = a0 + d0 * s, pb = o.a + od * t;
                float3 d = pa - pb;
                const float dist = length(d);
                const float r = radiusAt(L, fracOf(b, s));
                if (dist < r + o.r + 0.002f && dist > 1e-6f) {
                    d = d * (1.0f / dist);
                    add(int(i), -1, d, pa - d * r, pb + d * o.r);
                }
            }
            // the other logs (each pair once: the dynamic one first, or the lower index)
            for (size_t j = 0; j < B.size(); ++j) {
                if (j == i) continue;
                const Body& o = B[j];
                if (!o.L->alive || o.L->radius <= 1e-4f) continue;
                if (o.dynamic && j < i) continue;
                const float reach = b.half + b.rMax + o.half + o.rMax + 0.01f;
                const float3 dc = b.x - o.x;
                if (dot(dc, dc) > reach * reach) continue;
                const float3 a1 = o.x - o.axis * o.half, d1 = o.axis * (2.0f * o.half);
                auto pair = [&](float s, float t) {
                    const float3 pa = a0 + d0 * s, pb = a1 + d1 * t;
                    float3 d = pa - pb;
                    const float dist = length(d);
                    const float ra = radiusAt(L, fracOf(b, s)), rb = radiusAt(*o.L, fracOf(o, t));
                    if (dist >= ra + rb + 0.001f || dist < 1e-6f) return;
                    d = d * (1.0f / dist);
                    add(int(i), int(j), d, pa - d * ra, pb + d * rb);
                    // a sleeping log struck, or pressed into, wakes
                    if (!o.dynamic && (ra + rb - dist > 0.003f || -dot(d, b.v) > 0.2f)) wake[j] = 1;
                };
                float s, t;
                closestSegments(a0, d0, a1, d1, s, t);
                pair(s, t);
                // nearly parallel: the closest pair is any along their overlap; hold them at both ends of it too
                if (length(cross(b.axis, o.axis)) < 0.25f) {
                    for (int e = 0; e < 2; ++e) {
                        const float3 pe = a0 + d0 * float(e);
                        const float te = std::clamp(dot(pe - a1, d1) / std::max(dot(d1, d1), 1e-10f), 0.0f, 1.0f);
                        if (std::fabs(float(e) - s) > 0.2f) pair(float(e), te);
                    }
                }
            }
        }
        stats.contacts = std::max(stats.contacts, int(C.size()));
        // positions: each contact pushes its two bodies apart along its normal; static friction holds what has not
        // slid further than the normal correction allows
        // (the logs' contacts first, the ground's and the stones' last: what is pressed into the ground comes out of it;
        // twice over, as a kilogram log on a ten-gram coal needs)
        std::stable_sort(C.begin(), C.end(), [](const Contact& x, const Contact& y) { return (x.b >= 0) > (y.b >= 0); });
        for (int iter = 0; iter < 2; ++iter)
        for (Contact& c : C) {
            Body& A = B[size_t(c.a)];
            Body* Bp = c.b >= 0 ? &B[size_t(c.b)] : nullptr;
            const float3 pa = A.x + qrot(A.q, c.la);
            const float3 pb = Bp ? Bp->x + qrot(Bp->q, c.lb) : c.lb;
            // (a deep overlap, as a log laid into another, is undone at most maxDepenetration fast: undone in one
            // substep it would throw them apart at metres a second)
            const float d = Bp ? std::min(dot(pb - pa, c.n), maxDepenetration * h) : dot(pb - pa, c.n);
            if (d <= 0.0f) continue;
            const float3 ra = pa - A.x, rb = Bp ? pb - Bp->x : make_float3(0, 0, 0);
            const float wa = genInvMass(A, ra, c.n), wb = Bp ? genInvMass(*Bp, rb, c.n) : 0.0f;
            if (wa + wb <= 0.0f) continue;
            const float dl = d / (wa + wb);
            c.lambda += dl;
            applyPos(A, ra, c.n * dl);
            if (Bp) applyPos(*Bp, rb, c.n * -dl);
            // static friction
            const float3 paN = A.x + qrot(A.q, c.la), paP = A.xPrev + qrot(A.qPrev, c.la);
            const float3 pbN = Bp ? Bp->x + qrot(Bp->q, c.lb) : c.lb, pbP = Bp ? Bp->xPrev + qrot(Bp->qPrev, c.lb) : c.lb;
            const float3 dp = (paN - paP) - (pbN - pbP);
            const float3 dt3 = dp - c.n * dot(dp, c.n);
            const float tl = length(dt3);
            if (tl > 1e-7f) {
                const float3 tn = dt3 * (1.0f / tl);
                const float wta = genInvMass(A, paN - A.x, tn), wtb = Bp ? genInvMass(*Bp, pbN - Bp->x, tn) : 0.0f;
                const float lt = tl / std::max(wta + wtb, 1e-9f);
                if (lt < muStatic * c.lambda) {
                    applyPos(A, paN - A.x, tn * -lt);
                    if (Bp) applyPos(*Bp, pbN - Bp->x, tn * lt);
                }
            }
        }
        // velocities from the poses (never faster than a log falls from a man's height: a coal squeezed out from under a
        // log does not shoot across the night)
        for (Body& b : B) {
            if (!b.dynamic) continue;
            b.v = (b.x - b.xPrev) * (1.0f / h);
            const float sp = length(b.v);
            if (sp > maxSpeed) b.v = b.v * (maxSpeed / sp);
            const float4 dq = qmul(b.q, make_float4(b.qPrev.x, -b.qPrev.y, -b.qPrev.z, -b.qPrev.w));
            b.w = make_float3(dq.y, dq.z, dq.w) * (2.0f / h);
            if (dq.x < 0.0f) b.w = -b.w;
        }
        // dynamic friction and restitution
        for (const Contact& c : C) {
            if (c.lambda <= 0.0f) continue;
            Body& A = B[size_t(c.a)];
            Body* Bp = c.b >= 0 ? &B[size_t(c.b)] : nullptr;
            const float3 pa = A.x + qrot(A.q, c.la);
            const float3 ra = pa - A.x;
            const float3 pb = Bp ? Bp->x + qrot(Bp->q, c.lb) : c.lb;
            const float3 rb = Bp ? pb - Bp->x : make_float3(0, 0, 0);
            const float3 vr = (A.v + cross(A.w, ra)) - (Bp ? Bp->v + cross(Bp->w, rb) : make_float3(0, 0, 0));
            const float vn = dot(c.n, vr);
            const float3 vt = vr - c.n * vn;
            const float vtl = length(vt);
            // the sliding slowed by at most mu times the normal impulse (lambda / h), as the speed that impulse gives
            // the pair (times their inverse mass along the slide: a ten-gram coal stops as a kilogram log does)
            if (vtl > 1e-6f) {
                const float3 tn = vt * (1.0f / vtl);
                const float wt = genInvMass(A, ra, tn) + (Bp ? genInvMass(*Bp, rb, tn) : 0.0f);
                if (wt > 0.0f) {
                    const float dvt = std::min(muDynamic * c.lambda / h * wt, vtl);
                    applyVel(A, ra, tn * (-dvt / wt));
                    if (Bp) applyVel(*Bp, rb, tn * (dvt / wt));
                }
            }
            const float e = std::fabs(c.vnPre) > 2.0f * 9.81f * h ? restitution : 0.0f;
            const float dvn = -vn + std::max(-e * c.vnPre, 0.0f);
            const float wn = genInvMass(A, ra, c.n) + (Bp ? genInvMass(*Bp, rb, c.n) : 0.0f);
            if (wn > 0.0f && std::fabs(dvn) > 1e-7f) {
                applyVel(A, ra, c.n * (dvn / wn));
                if (Bp) applyVel(*Bp, rb, c.n * (-dvn / wn));
            }
            touched[size_t(c.a)] = 1;
            if (c.b >= 0) touched[size_t(c.b)] = 1;
        }
        // rolling resistance: a charred log is no true cylinder and the ash gives under it; its roll about its own
        // axis slows as a rolling speed would at rollResist g (a chunk set rolling at 1 m/s stops within a stone's throw
        // of the pit, not across the night)
        for (size_t i = 0; i < B.size(); ++i) {
            Body& b = B[i];
            if (!b.dynamic || !touched[i]) continue;
            const float wa = dot(b.w, b.axis);
            const float cut = std::min(std::fabs(wa), rollResist * 9.81f * h / std::max(b.L->radius, 1e-3f));
            b.w = b.w - b.axis * (wa > 0.0f ? cut : -cut);
        }
    }
    // back to the logs; a body that has hardly moved for half a second sleeps
    for (size_t i = 0; i < B.size(); ++i) {
        ALog& L = logs[i];
        const Body& b = B[i];
        if (wake[i] && L.sleeping) {
            L.sleeping = false;
            L.sleepTime = 0.0f;
        }
        if (!b.dynamic) continue;
        L.pos = b.x;
        L.q = b.q;
        L.vel = b.v;
        L.angVel = b.w;
        const float vmax = std::max(length(b.v), length(b.w) * (0.5f * L.len));
        L.sleepTime = vmax < 0.01f ? L.sleepTime + dt : 0.0f;
        if (L.sleepTime > 0.5f) {
            L.sleeping = true;
            L.vel = L.angVel = make_float3(0, 0, 0);
        }
    }
    stats.ms = float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
