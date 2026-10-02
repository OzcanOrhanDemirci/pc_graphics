#include "scenes/tanecik/body.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace {

// ---- distances (Inigo Quilez's formulas)

float sgn(float x) { return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f; }

// a bound, not exact; good enough near the surface, where it matters
float sdEllipsoid(float3 p, float3 c, float3 r) {
    const float3 q = p - c;
    const float k0 = length(make_float3(q.x / r.x, q.y / r.y, q.z / r.z));
    const float k1 = length(make_float3(q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)));
    return k1 > 0.0f ? k0 * (k0 - 1.0f) / k1 : -std::min(r.x, std::min(r.y, r.z));
}
// a cone between two spheres (radius r1 at a, r2 at b), exact
float sdRoundCone(float3 p, float3 a, float3 b, float r1, float r2) {
    const float3 ba = b - a;
    const float l2 = dot(ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
    const float3 pa = p - a;
    const float y = dot(pa, ba), z = y - l2;
    const float3 w = pa * l2 - ba * y;
    const float x2 = dot(w, w), y2 = y * y * l2, z2 = z * z * l2;
    const float k = sgn(rr) * rr * rr * x2;
    if (sgn(z) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if (sgn(y) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}
float smin(float a, float b, float k) {
    const float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// ---- the mannequin's parts, at rest (metres). The left side (+x); the right is its mirror.

struct Part {
    bool cone;                 // round cone a..b (radii r1, r2); else an ellipsoid at a with radii b
    float3 a, b;
    float r1, r2;
    int bone;
    int blend;                 // how it joins: 0 the trunk (wide), 1 neck, 2 head, 3 limbs, 4 shoulder caps (wide)
};

std::vector<Part> makeParts() {
    std::vector<Part> v;
    auto E = [&](float3 c, float3 r, int bone, int blend) { v.push_back({false, c, r, 0, 0, bone, blend}); };
    auto C = [&](float3 a, float3 b, float r1, float r2, int bone, int blend) { v.push_back({true, a, b, r1, r2, bone, blend}); };
    auto m = mirrorX;
    // the trunk
    E(make_float3(0, 1.285f, -0.005f), make_float3(0.165f, 0.19f, 0.112f), B_CHEST, 0);
    E(make_float3(0, 1.385f, -0.012f), make_float3(0.20f, 0.07f, 0.095f), B_CHEST, 0);     // the shoulder line
    E(make_float3(0, 1.07f, 0.0f), make_float3(0.142f, 0.15f, 0.10f), B_ABDOMEN, 0);
    E(make_float3(0, 0.94f, -0.005f), make_float3(0.165f, 0.105f, 0.108f), B_PELVIS, 0);
    for (int s = 0; s < 2; ++s) {
        const float3 c = make_float3(0.07f, 0.875f, -0.045f);
        E(s ? m(c) : c, make_float3(0.085f, 0.09f, 0.08f), B_PELVIS, 0);
    }
    // neck and head (no face: a smooth mannequin's head, a jaw for which way it looks)
    C(make_float3(0, 1.45f, -0.012f), make_float3(0, 1.585f, 0.0f), 0.06f, 0.05f, B_NECK, 1);
    E(make_float3(0, 1.655f, 0.005f), make_float3(0.078f, 0.112f, 0.098f), B_HEAD, 2);
    E(make_float3(0, 1.592f, 0.022f), make_float3(0.058f, 0.05f, 0.07f), B_HEAD, 2);
    // the limbs, left then mirrored, laid out from the joints
    const RestJoints& J = restJoints();
    for (int s = 0; s < 2; ++s) {
        auto P = [&](float3 p) { return s ? mirrorX(p) : p; };
        const int ua = s ? B_UARM_R : B_UARM_L, fa = s ? B_FARM_R : B_FARM_L, ha = s ? B_HAND_R : B_HAND_L;
        const int th = s ? B_THIGH_R : B_THIGH_L, sh = s ? B_SHIN_R : B_SHIN_L, ft = s ? B_FOOT_R : B_FOOT_L;
        E(P(J.shoulder + make_float3(-0.02f, -0.008f, 0)), make_float3(0.06f, 0.055f, 0.058f), ua, 4);   // shoulder cap
        C(P(J.shoulder), P(J.elbow), 0.052f, 0.04f, ua, 3);
        C(P(J.elbow), P(J.wrist), 0.04f, 0.026f, fa, 3);
        E(P(lerp3(J.elbow, J.wrist, 0.3f)), make_float3(0.045f, 0.085f, 0.045f), fa, 3);              // forearm muscle
        E(P(lerp3(J.wrist, J.handTip, 0.5f)), make_float3(0.022f, 0.085f, 0.046f), ha, 3);            // a mitten hand
        C(P(J.hip), P(J.knee), 0.088f, 0.052f, th, 3);
        C(P(J.knee), P(J.ankle), 0.05f, 0.032f, sh, 3);
        E(P(lerp3(J.knee, J.ankle, 0.32f) + make_float3(0, 0, -0.026f)), make_float3(0.05f, 0.11f, 0.054f), sh, 3);   // calf
        C(P(J.heel), P(J.toe), 0.042f, 0.026f, ft, 3);
    }
    return v;
}
const std::vector<Part>& parts() {
    static const std::vector<Part> p = makeParts();
    return p;
}
float partDist(const Part& q, float3 p) { return q.cone ? sdRoundCone(p, q.a, q.b, q.r1, q.r2) : sdEllipsoid(p, q.a, q.b); }

// ---- meshes

// adds a triangle facing the way its vertices' normals face (generators need not care about winding)
void tri(TriMesh& m, unsigned a, unsigned b, unsigned c) {
    const float3 g = cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]);
    if (dot(g, g) < 1e-20f) return;
    if (dot(g, m.nrm[a] + m.nrm[b] + m.nrm[c]) < 0.0f) std::swap(b, c);
    m.idx.push_back(make_uint3(a, b, c));
}
void quad(TriMesh& m, unsigned a, unsigned b, unsigned c, unsigned d) {   // a b c d around
    tri(m, a, b, c);
    tri(m, a, c, d);
}
// rings of vertices joined into a strip; each ring nu+1 vertices (the seam doubled)
void joinRings(TriMesh& m, unsigned base, int rings, int nu) {
    for (int j = 0; j + 1 < rings; ++j)
        for (int i = 0; i < nu; ++i) {
            const unsigned a = base + unsigned(j * (nu + 1) + i);
            quad(m, a, a + 1, a + 1 + unsigned(nu + 1), a + unsigned(nu + 1));
        }
}
void addEllipsoid(TriMesh& m, float3 c, float3 r, int nu = 28, int nv = 16) {
    const unsigned base = unsigned(m.pos.size());
    for (int j = 0; j <= nv; ++j) {
        const float th = M_PIf * j / nv;
        for (int i = 0; i <= nu; ++i) {
            const float ph = 2.0f * M_PIf * i / nu;
            const float3 s = make_float3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.pos.push_back(c + s * r);
            m.nrm.push_back(normalize(make_float3(s.x / r.x, s.y / r.y, s.z / r.z)));
        }
    }
    joinRings(m, base, nv + 1, nu);
}
// a capsule whose two ends may differ (radius ra at a, rb at b): hemispheres joined by a tapered tube
void addCapsule(TriMesh& m, float3 a, float3 b, float ra, float rb, int nu = 16, int nv = 6) {
    const float3 w = normalize(b - a);
    float3 u, v;
    makeBasis(w, u, v);
    const unsigned base = unsigned(m.pos.size());
    int rings = 0;
    for (int e = 0; e < 2; ++e) {
        for (int j = 0; j <= nv; ++j) {
            const float th = e == 0 ? M_PIf - 0.5f * M_PIf * j / nv : 0.5f * M_PIf * (1.0f - float(j) / nv);
            const float3 c = e == 0 ? a : b;
            const float r = e == 0 ? ra : rb;
            for (int i = 0; i <= nu; ++i) {
                const float ph = 2.0f * M_PIf * i / nu;
                const float3 radial = u * std::cos(ph) + v * std::sin(ph);
                const float3 n = radial * std::sin(th) + w * std::cos(th);
                m.pos.push_back(c + n * r);
                m.nrm.push_back(n);
            }
            ++rings;
        }
    }
    joinRings(m, base, rings, nu);
}
// a tube of radius r along a polyline (parallel-transported frame, so it does not twist)
void addTube(TriMesh& m, const std::vector<float3>& pts, float r, int nu = 10) {
    const int n = int(pts.size());
    if (n < 2) return;
    const unsigned base = unsigned(m.pos.size());
    float3 u, v;
    for (int k = 0; k < n; ++k) {
        const float3 t = normalize(pts[std::min(k + 1, n - 1)] - pts[std::max(k - 1, 0)]);
        if (k == 0) makeBasis(t, u, v);
        else {
            u = normalize(u - t * dot(u, t));
            v = cross(t, u);
        }
        for (int i = 0; i <= nu; ++i) {
            const float ph = 2.0f * M_PIf * i / nu;
            const float3 nn = u * std::cos(ph) + v * std::sin(ph);
            m.pos.push_back(pts[k] + nn * r);
            m.nrm.push_back(nn);
        }
    }
    joinRings(m, base, n, nu);
}

template <class F> void parallelFor(int n, F f) {
    const int T = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    for (int t = 0; t < T; ++t)
        th.emplace_back([=] {
            for (int i = t; i < n; i += T) f(i);
        });
    for (auto& x : th) x.join();
}

}   // namespace

void TriMesh::append(const TriMesh& m) {
    const unsigned base = unsigned(pos.size());
    pos.insert(pos.end(), m.pos.begin(), m.pos.end());
    nrm.insert(nrm.end(), m.nrm.begin(), m.nrm.end());
    for (const uint3& t : m.idx) idx.push_back(make_uint3(t.x + base, t.y + base, t.z + base));
}

const BoneInfo& boneInfo(int b) {
    static const BoneInfo info[B_COUNT] = {
        {"kafa", B_NECK},          {"boyun", B_CHEST},        {"gogus", B_ABDOMEN},   {"bel", B_PELVIS},
        {"legen", -1},             {"ust kol sol", B_CHEST},  {"on kol sol", B_UARM_L}, {"el sol", B_FARM_L},
        {"ust kol sag", B_CHEST},  {"on kol sag", B_UARM_R},  {"el sag", B_FARM_R},   {"uyluk sol", B_PELVIS},
        {"baldir sol", B_THIGH_L}, {"ayak sol", B_SHIN_L},    {"uyluk sag", B_PELVIS}, {"baldir sag", B_THIGH_R},
        {"ayak sag", B_SHIN_R}};
    return info[b];
}

const RestJoints& restJoints() {
    static const RestJoints j = [] {
        RestJoints r;
        r.shoulder = make_float3(0.19f, 1.40f, -0.012f);
        r.elbow = make_float3(0.255f, 1.125f, -0.02f);
        r.wrist = make_float3(0.305f, 0.88f, 0.012f);
        r.handTip = make_float3(0.33f, 0.715f, 0.028f);
        r.hip = make_float3(0.088f, 0.90f, 0.0f);
        r.knee = make_float3(0.112f, 0.49f, 0.012f);
        r.ankle = make_float3(0.124f, 0.085f, -0.012f);
        r.heel = make_float3(0.124f, 0.045f, -0.04f);
        r.toe = make_float3(0.14f, 0.026f, 0.16f);
        r.neckTop = make_float3(0, 1.585f, -0.015f);
        r.neckBase = make_float3(0, 1.455f, -0.03f);
        r.chestLow = make_float3(0, 1.16f, -0.06f);
        r.lumbarLow = make_float3(0, 0.985f, -0.05f);
        return r;
    }();
    return j;
}

float Mannequin::sdf(float3 p, int& bone) const {
    const auto& ps = parts();
    float trunk = 1e9f, neck = 1e9f, head = 1e9f, limbs = 1e9f, caps = 1e9f, nearest = 1e9f;
    bone = B_CHEST;
    for (const Part& q : ps) {
        const float d = partDist(q, p);
        if (d < nearest) { nearest = d; bone = q.bone; }
        switch (q.blend) {
        case 0: trunk = smin(trunk, d, 0.07f); break;
        case 1: neck = std::min(neck, d); break;
        case 2: head = smin(head, d, 0.03f); break;
        case 4: caps = std::min(caps, d); break;
        default: limbs = smin(limbs, d, 0.02f); break;
        }
    }
    float d = smin(trunk, caps, 0.06f);   // shoulders round into the trunk, not perched on it
    d = smin(d, neck, 0.05f);
    d = smin(d, head, 0.035f);
    return smin(d, limbs, 0.025f);
}
float Mannequin::sdf(float3 p) const {
    int b;
    return sdf(p, b);
}
float3 Mannequin::normal(float3 p) const {
    const float e = 5e-4f;
    const float3 g = make_float3(sdf(p + make_float3(e, 0, 0)) - sdf(p - make_float3(e, 0, 0)), sdf(p + make_float3(0, e, 0)) - sdf(p - make_float3(0, e, 0)),
                                 sdf(p + make_float3(0, 0, e)) - sdf(p - make_float3(0, 0, e)));
    const float l = length(g);
    return l > 0.0f ? g / l : make_float3(0, 1, 0);
}

BodyParticles fillBody(const Mannequin& m, float s, float jitter) {
    BodyParticles out;
    out.spacing = s;
    const float3 lo = m.lo(), hi = m.hi();
    const int nx = int((hi.x - lo.x) / s), ny = int((hi.y - lo.y) / s), nz = int((hi.z - lo.z) / s);
    // slices in parallel, gathered in order (the same body every time)
    std::vector<BodyParticles> part(static_cast<size_t>(ny));   // one per slice
    parallelFor(ny, [&](int j) {
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i) {
                float3 p = lo + make_float3((i + 0.5f) * s, (j + 0.5f) * s, (k + 0.5f) * s);
                if (jitter > 0.0f) {   // a hash of the lattice cell: the same body every time
                    unsigned h = unsigned(i) * 73856093u ^ unsigned(j) * 19349663u ^ unsigned(k) * 83492791u;
                    float u[3];
                    for (float& x : u) {
                        h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
                        x = (h & 0xffffff) / 16777216.0f * 2.0f - 1.0f;
                    }
                    p = p + make_float3(u[0], u[1], u[2]) * (jitter * s);
                }
                int bone;
                const float d = m.sdf(p, bone);
                if (d > -0.5f * s) continue;
                part[j].pos.push_back(p);
                part[j].bone.push_back(uint8_t(bone));
                part[j].skin.push_back(d > -1.5f * s ? 1 : 0);
            }
    });
    for (const BodyParticles& b : part) {
        out.pos.insert(out.pos.end(), b.pos.begin(), b.pos.end());
        out.bone.insert(out.bone.end(), b.bone.begin(), b.bone.end());
        out.skin.insert(out.skin.end(), b.skin.begin(), b.skin.end());
    }
    return out;
}

// surface nets: a vertex in every cell the surface crosses (the mean of the crossings on its edges, pulled onto the
// surface), a quad around every grid edge it crosses
TriMesh skinMesh(const Mannequin& m, float c) {
    const float3 lo = m.lo(), hi = m.hi();
    const int nx = int((hi.x - lo.x) / c) + 2, ny = int((hi.y - lo.y) / c) + 2, nz = int((hi.z - lo.z) / c) + 2;
    auto G = [&](int i, int j, int k) { return (size_t(k) * ny + j) * nx + i; };
    std::vector<float> g(size_t(nx) * ny * nz);
    parallelFor(nz, [&](int k) {
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) g[G(i, j, k)] = m.sdf(lo + make_float3(i * c, j * c, k * c));
    });
    const int cx = nx - 1, cy = ny - 1, cz = nz - 1;
    auto C = [&](int i, int j, int k) { return (size_t(k) * cy + j) * cx + i; };
    std::vector<int> cell(size_t(cx) * cy * cz, -1);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    TriMesh out;
    for (int k = 0; k < cz; ++k)
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i) {
                float v[8];
                int inside = 0;
                for (int q = 0; q < 8; ++q) {
                    v[q] = g[G(i + (q & 1), j + ((q >> 1) & 1), k + ((q >> 2) & 1))];
                    inside += v[q] < 0.0f;
                }
                if (inside == 0 || inside == 8) continue;
                float3 sum = make_float3(0, 0, 0);
                int n = 0;
                for (const auto& e : E) {
                    const float a = v[e[0]], b = v[e[1]];
                    if ((a < 0.0f) == (b < 0.0f)) continue;
                    const float t = a / (a - b);
                    const float3 pa = make_float3(float(e[0] & 1), float((e[0] >> 1) & 1), float((e[0] >> 2) & 1));
                    const float3 pb = make_float3(float(e[1] & 1), float((e[1] >> 1) & 1), float((e[1] >> 2) & 1));
                    sum = sum + pa + (pb - pa) * t;
                    ++n;
                }
                float3 p = lo + (make_float3(float(i), float(j), float(k)) + sum / float(n)) * c;
                const float d = m.sdf(p);
                const float3 nn = m.normal(p);
                if (std::fabs(d) < c) p = p - nn * d;
                cell[C(i, j, k)] = int(out.pos.size());
                out.pos.push_back(p);
                out.nrm.push_back(m.normal(p));
            }
    auto emit = [&](int a, int b, int cc, int d) {
        if (a < 0 || b < 0 || cc < 0 || d < 0) return;
        quad(out, unsigned(a), unsigned(b), unsigned(cc), unsigned(d));
    };
    for (int k = 1; k < cz; ++k)
        for (int j = 1; j < cy; ++j)
            for (int i = 0; i < cx; ++i)   // edges along x
                if ((g[G(i, j, k)] < 0.0f) != (g[G(i + 1, j, k)] < 0.0f))
                    emit(cell[C(i, j - 1, k - 1)], cell[C(i, j, k - 1)], cell[C(i, j, k)], cell[C(i, j - 1, k)]);
    for (int k = 1; k < cz; ++k)
        for (int j = 0; j < cy; ++j)
            for (int i = 1; i < cx; ++i)   // along y
                if ((g[G(i, j, k)] < 0.0f) != (g[G(i, j + 1, k)] < 0.0f))
                    emit(cell[C(i - 1, j, k - 1)], cell[C(i, j, k - 1)], cell[C(i, j, k)], cell[C(i - 1, j, k)]);
    for (int k = 0; k < cz; ++k)
        for (int j = 1; j < cy; ++j)
            for (int i = 1; i < cx; ++i)   // along z
                if ((g[G(i, j, k)] < 0.0f) != (g[G(i, j, k + 1)] < 0.0f))
                    emit(cell[C(i - 1, j - 1, k)], cell[C(i, j - 1, k)], cell[C(i, j, k)], cell[C(i - 1, j, k)]);
    return out;
}

// the skeleton as primitives, each on its bone: the bones' meshes and the particles that are bone both come from here
static std::vector<BonePrim> makeSkeleton() {
    std::vector<BonePrim> v;
    auto cap = [&](int bone, float3 a, float3 b, float ra, float rb, int nu = 16, int nv = 6) {
        BonePrim q;
        q.kind = BonePrim::Capsule;
        q.bone = bone;
        q.a = a;
        q.b = b;
        q.ra = ra;
        q.rb = rb;
        q.nu = nu;
        q.nv = nv;
        v.push_back(q);
    };
    auto ell = [&](int bone, float3 c, float3 r, int nu = 28, int nv = 16) {
        BonePrim q;
        q.kind = BonePrim::Ellipsoid;
        q.bone = bone;
        q.a = c;
        q.b = r;
        q.nu = nu;
        q.nv = nv;
        v.push_back(q);
    };
    const RestJoints& J = restJoints();
    // skull and jaw, spine (neck, chest, lumbar, sacrum)
    ell(B_HEAD, make_float3(0, 1.668f, -0.004f), make_float3(0.066f, 0.088f, 0.083f));
    ell(B_HEAD, make_float3(0, 1.597f, 0.022f), make_float3(0.045f, 0.03f, 0.05f), 20, 10);
    cap(B_NECK, J.neckBase + make_float3(0, 0.005f, -0.005f), J.neckTop + make_float3(0, 0, -0.005f), 0.016f, 0.015f);
    cap(B_CHEST, J.chestLow + make_float3(0, -0.01f, -0.012f), J.neckBase + make_float3(0, 0.005f, -0.01f), 0.02f, 0.017f);
    cap(B_ABDOMEN, J.lumbarLow + make_float3(0, -0.005f, -0.012f), J.chestLow + make_float3(0, -0.01f, -0.012f), 0.024f, 0.021f);
    cap(B_PELVIS, make_float3(0, 0.90f, -0.075f), J.lumbarLow + make_float3(0, -0.005f, -0.012f), 0.014f, 0.022f);
    // ribs: hoops falling towards the front (open at the back, by the spine), a breastbone, collarbones
    for (int r = 0; r < 7; ++r) {
        const float y = 1.17f + 0.04f * r;
        const float a = 0.105f + 0.03f * std::sin(M_PIf * (r + 1) / 8.0f), b = 0.085f;
        BonePrim q;
        q.kind = BonePrim::Tube;
        q.bone = B_CHEST;
        q.ra = 0.0065f;
        q.nu = 10;
        for (int k = 0; k <= 40; ++k) {
            const float ang = -0.5f * M_PIf + (2.0f * M_PIf - 0.5f) * k / 40.0f + 0.25f;
            const float z = -0.01f + b * std::sin(ang), x = a * std::cos(ang);
            q.pts.push_back(make_float3(x, y - 0.04f * std::max(0.0f, (z + 0.01f) / b), z));
        }
        v.push_back(q);
    }
    cap(B_CHEST, make_float3(0, 1.20f, 0.07f), make_float3(0, 1.40f, 0.085f), 0.011f, 0.013f);
    for (int s = 0; s < 2; ++s) {
        auto Q = [&](float3 p) { return s ? mirrorX(p) : p; };
        auto P = [&](float x, float y, float z) { return Q(make_float3(x, y, z)); };
        const int ua = s ? B_UARM_R : B_UARM_L, fa = s ? B_FARM_R : B_FARM_L, ha = s ? B_HAND_R : B_HAND_L;
        const int th = s ? B_THIGH_R : B_THIGH_L, sh = s ? B_SHIN_R : B_SHIN_L, ft = s ? B_FOOT_R : B_FOOT_L;
        cap(B_CHEST, P(0.02f, 1.425f, 0.075f), P(0.175f, 1.445f, -0.01f), 0.009f, 0.009f);   // collarbone
        // pelvis: the iliac wing and its brim down to the hip socket, the pubic bone
        cap(B_PELVIS, P(0.0f, 0.975f, -0.055f), P(0.12f, 0.985f, -0.015f), 0.02f, 0.026f);
        cap(B_PELVIS, P(0.12f, 0.985f, -0.015f), P(0.085f, 0.89f, 0.005f), 0.024f, 0.02f);
        cap(B_PELVIS, P(0.085f, 0.89f, 0.005f), P(0.02f, 0.885f, 0.045f), 0.015f, 0.012f);
        // arm: humerus with its head, radius and ulna, a hand of a palm and fingers
        const float3 dz = make_float3(0, 0, 0.011f), palm = lerp3(J.wrist, J.handTip, 0.5f);
        ell(ua, Q(J.shoulder + make_float3(-0.002f, 0.002f, 0)), make_float3(0.023f, 0.023f, 0.023f), 16, 10);
        cap(ua, Q(J.shoulder + make_float3(0, -0.01f, 0)), Q(J.elbow + make_float3(-0.003f, 0.005f, 0)), 0.013f, 0.016f);
        cap(fa, Q(J.elbow - dz), Q(J.wrist - dz), 0.009f, 0.008f);
        cap(fa, Q(J.elbow + dz), Q(J.wrist + dz), 0.008f, 0.009f);
        cap(ha, Q(J.wrist), Q(palm), 0.012f, 0.009f);
        for (int f = 0; f < 4; ++f) {
            const float3 o = make_float3(0, 0, -0.026f + 0.017f * f);
            cap(ha, Q(palm + o * 0.8f), Q(lerp3(palm, J.handTip, 0.7f) + o * 0.7f + make_float3(0, 0.006f * std::fabs(f - 1.5f), 0)), 0.005f, 0.004f, 10, 4);
        }
        // leg: femur with its head and knee, tibia and fibula with the kneecap (its tendon runs to the tibia), a foot
        ell(th, Q(J.hip + make_float3(0, -0.008f, 0)), make_float3(0.025f, 0.025f, 0.025f), 16, 10);
        cap(th, Q(J.hip + make_float3(0.002f, -0.01f, 0)), Q(J.knee + make_float3(0, 0.015f, -0.004f)), 0.017f, 0.02f);
        ell(th, Q(J.knee + make_float3(0, 0, -0.007f)), make_float3(0.03f, 0.022f, 0.026f), 16, 10);
        ell(sh, Q(J.knee + make_float3(0, 0, 0.036f)), make_float3(0.018f, 0.022f, 0.008f), 14, 8);
        cap(sh, Q(J.knee + make_float3(0, -0.02f, -0.004f)), Q(J.ankle + make_float3(0, 0.005f, 0.004f)), 0.017f, 0.013f);
        cap(sh, Q(J.knee + make_float3(0.026f, -0.03f, -0.022f)), Q(J.ankle + make_float3(0.018f, 0.005f, -0.004f)), 0.007f, 0.007f);
        cap(ft, Q(J.heel + make_float3(0, 0.005f, -0.005f)), Q(J.ankle + make_float3(0, -0.015f, 0.012f)), 0.018f, 0.016f);   // heel, ankle
        for (int t = 0; t < 3; ++t) {
            const float3 tip = J.toe + make_float3(-0.012f + 0.012f * t, -0.002f, -0.02f - 0.012f * t);
            cap(ft, Q(J.ankle + make_float3(0, -0.025f, 0.012f)), Q(tip), 0.009f, 0.006f, 10, 4);
        }
    }
    return v;
}
const std::vector<BonePrim>& skeleton() {
    static const std::vector<BonePrim> s = makeSkeleton();
    return s;
}

TriMesh skeletonMesh(int bone) {
    TriMesh m;
    for (const BonePrim& q : skeleton()) {
        if (bone >= 0 && q.bone != bone) continue;
        if (q.kind == BonePrim::Capsule) addCapsule(m, q.a, q.b, q.ra, q.rb, q.nu, q.nv);
        else if (q.kind == BonePrim::Ellipsoid) addEllipsoid(m, q.a, q.b, q.nu, q.nv);
        else addTube(m, q.pts, q.ra, q.nu);
    }
    return m;
}

// distance to a primitive's surface (a capsule with two radii is taken as a round cone, exact; the ellipsoid's is a bound)
float bonePrimDistance(const BonePrim& q, float3 p) {
    if (q.kind == BonePrim::Capsule) return sdRoundCone(p, q.a, q.b, q.ra, q.rb);
    if (q.kind == BonePrim::Ellipsoid) return sdEllipsoid(p, q.a, q.b);
    float d = 1e9f;
    for (size_t k = 0; k + 1 < q.pts.size(); ++k) {
        const float3 ab = q.pts[k + 1] - q.pts[k], ap = p - q.pts[k];
        const float t = std::min(1.0f, std::max(0.0f, dot(ap, ab) / dot(ab, ab)));
        d = std::min(d, length(ap - ab * t));
    }
    return d - q.ra;
}

TriMesh studioMesh(float wallZ, float r, float height, float halfWidth, float frontZ) {
    // the profile in (y, z) with its normals: floor, cove, wall
    std::vector<float3> pts, nrm;
    pts.push_back(make_float3(0, 0, frontZ));
    nrm.push_back(make_float3(0, 1, 0));
    for (int k = 0; k <= 24; ++k) {
        const float a = 0.5f * M_PIf * k / 24;
        pts.push_back(make_float3(0, r - r * std::cos(a), wallZ + r - r * std::sin(a)));
        nrm.push_back(make_float3(0, std::cos(a), std::sin(a)));
    }
    pts.push_back(make_float3(0, height, wallZ));
    nrm.push_back(make_float3(0, 0, 1));
    TriMesh m;
    for (size_t k = 0; k < pts.size(); ++k)
        for (int s = 0; s < 2; ++s) {
            m.pos.push_back(make_float3(s ? halfWidth : -halfWidth, pts[k].y, pts[k].z));
            m.nrm.push_back(nrm[k]);
        }
    for (unsigned k = 0; k + 1 < pts.size(); ++k) quad(m, 2 * k, 2 * k + 1, 2 * k + 3, 2 * k + 2);
    return m;
}
