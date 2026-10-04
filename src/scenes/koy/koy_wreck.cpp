// Scene 5 "Koy", Ada: the wreck (see koy_wreck.h). Built in the ship's frame (x to the bow, y up from the keel's bottom,
// z to port), then placed on the slope.
#include "scenes/koy/koy_wreck.h"

#include "scenes/koy/koy_noise.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {
constexpr float kPi = 3.14159265f;

float clampf1(float v, float a, float b) { return std::min(std::max(v, a), b); }
float sstep(float a, float b, float x) {
    const float t = clampf1((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------------------------------
// The hull: one function for the outer surface's half-breadth, the decks' heights, the keel's line
struct Hull {
    float L = 38.0f, B = 10.5f, D0 = 6.2f;
    float xs = -19.0f, xb = 19.0f;        // the sternpost, the stem's foot of the curve
    float xF = 10.5f, xQ = -6.0f, xP = -13.5f;   // forecastle, quarterdeck, poop
    // the bow's profile (the stem's outside), x against y
    float stemX(float y) const { return xb - 1.2f + 1.6f * std::sqrt(clampf1(y / 9.5f, 0.0f, 1.0f)); }
    float keelY(float x) const {   // the keel's bottom rises into the stem
        if (x <= xb - 5.0f) return 0.0f;
        const float u = (x - (xb - 5.0f)) / 5.0f;
        return 6.0f * u * u * u;
    }
    float sheer(float x) const {   // the upper deck at the side
        const float s = 2.0f * x / L;
        return D0 + 0.6f * s * s + 0.15f * s;
    }
    float gunDeck(float x) const { return sheer(x) - 2.45f; }
    float foreDeck(float x) const { return sheer(x) + 2.0f; }
    float quarterDeck(float x) const { return sheer(x) + 2.2f; }
    float poopDeck(float x) const { return quarterDeck(x) + 2.1f; }
    float topDeck(float x) const {
        if (x <= xP) return poopDeck(x);
        if (x <= xQ) return quarterDeck(x);
        if (x >= xF) return foreDeck(x);
        return sheer(x);
    }
    float railY(float x) const { return topDeck(x) + 1.0f; }
    // the outer surface's half-breadth at (x, y): a deadrise floor, the widest at ~62 % of the depth, tumblehome above;
    // fine at the bow (finer low down), the run aft narrowing to the sternpost low down and to a transom high up
    float half(float x, float y) const {
        const float s = 2.0f * x / L;
        const float t = clampf1((y - keelY(x)) / std::max(sheer(x) - keelY(x), 0.5f), 0.0f, 1.8f);
        float shape = t < 0.6f ? 0.07f + 0.93f * (1.0f - (1.0f - t / 0.6f) * (1.0f - t / 0.6f)) : 1.0f - 0.55f * (t - 0.6f) * (t - 0.6f);
        float f = 1.0f;
        if (s > 0.25f) {
            const float u = (s - 0.25f) / 0.75f;
            f *= std::sqrt(std::max(0.0f, 1.0f - std::pow(std::min(u, 1.0f), 1.6f + 0.9f * (1.0f - std::min(t, 1.0f)))));
        }
        if (s < -0.45f) {
            const float u = (-0.45f - s) / 0.55f;
            f *= 1.0f - (0.40f + 0.52f * (1.0f - std::min(t, 1.0f))) * u * u;
        }
        if (x > stemX(y)) f = 0.0f;
        return std::max(0.5f * B * shape * f, 0.0f);
    }
};

// ---------------------------------------------------------------------------------------------------
// mesh building in the ship's frame
struct Emit {
    KMeshCPU* m;
    void quad(float3 a, float3 b, float3 c, float3 d, float3 na, float3 nb, float3 nc, float3 nd, float2 ua, float2 ub, float2 uc, float2 ud) {
        const unsigned base = unsigned(m->pos.size());
        m->pos.push_back(a); m->pos.push_back(b); m->pos.push_back(c); m->pos.push_back(d);
        m->nrm.push_back(na); m->nrm.push_back(nb); m->nrm.push_back(nc); m->nrm.push_back(nd);
        m->uv.push_back(ua); m->uv.push_back(ub); m->uv.push_back(uc); m->uv.push_back(ud);
        // wound so the face looks along the normals' mean
        const float3 n = na + nb + nc + nd;
        if (dot(cross(b - a, c - a), n) >= 0.0f) {
            m->idx.push_back(make_uint3(base, base + 1, base + 2));
            m->idx.push_back(make_uint3(base, base + 2, base + 3));
        } else {
            m->idx.push_back(make_uint3(base, base + 2, base + 1));
            m->idx.push_back(make_uint3(base, base + 3, base + 2));
        }
    }
    void flat(float3 a, float3 b, float3 c, float3 d, float3 n, float2 ua, float2 ub, float2 uc, float2 ud) { quad(a, b, c, d, n, n, n, n, ua, ub, uc, ud); }
    // an oriented box (axes unit), faces subdivided every `seg` metres (the baked occlusion lives in the vertices); uv in
    // tiles of `tile` metres with the grain along ax
    void box(float3 c, float3 ax, float3 ay, float3 az, float3 h, float tile, float2 off, float seg = 0.4f) {
        const float3 A[3] = {ax, ay, az};
        const float H[3] = {h.x, h.y, h.z};
        for (int f = 0; f < 6; ++f) {
            const int n = f / 2, u = n == 0 ? 1 : 0, v = n == 2 ? 1 : 2;
            const float sg = (f & 1) ? 1.0f : -1.0f;
            const float3 N = A[n] * sg;
            const int nu = std::max(1, int(std::ceil(2.0f * H[u] / seg))), nv = std::max(1, int(std::ceil(2.0f * H[v] / seg)));
            for (int j = 0; j < nv; ++j)
                for (int i = 0; i < nu; ++i) {
                    const float u0 = -H[u] + 2.0f * H[u] * i / nu, u1 = -H[u] + 2.0f * H[u] * (i + 1) / nu;
                    const float v0 = -H[v] + 2.0f * H[v] * j / nv, v1 = -H[v] + 2.0f * H[v] * (j + 1) / nv;
                    auto P = [&](float su, float sv) { return c + N * H[n] + A[u] * su + A[v] * sv; };
                    auto T = [&](float su, float sv) { return u == 0 ? make_float2(su / tile + off.x, sv / tile + off.y) : make_float2(sv / tile + off.x, su / tile + off.y); };
                    flat(P(u0, v0), P(u1, v0), P(u1, v1), P(u0, v1), N, T(u0, v0), T(u1, v0), T(u1, v1), T(u0, v1));
                }
        }
    }
    // a lathe about the axis from p0 (profile: (radius, distance along) pairs), with a lumpy crust of amplitude `crust`
    void lathe(float3 p0, float3 w, const std::vector<float2>& prof, int around, float crust, unsigned seed) {
        w = normalize(w);
        const float3 e1 = normalize(cross(std::fabs(w.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), w)), e2 = cross(w, e1);
        const unsigned base = unsigned(m->pos.size());
        const int n = int(prof.size());
        for (int j = 0; j < n; ++j)
            for (int i = 0; i <= around; ++i) {
                const float ph = 2.0f * kPi * float(i % around) / around;
                const float3 d = e1 * std::cos(ph) + e2 * std::sin(ph);
                const float3 c = p0 + w * prof[size_t(j)].y;
                const float r = prof[size_t(j)].x + crust * std::max(0.0f, kn::fbm3(c.x * 3.0f + d.x + float(seed % 97), c.y * 3.0f + d.y, c.z * 3.0f + d.z, 3) + 0.3f);
                m->pos.push_back(c + d * r);
                m->nrm.push_back(make_float3(0, 0, 0));
                m->uv.push_back(make_float2(ph * 0.3f, prof[size_t(j)].y));
            }
        const size_t first = m->idx.size();
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i < around; ++i) {
                const unsigned a = base + unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
                m->idx.push_back(make_uint3(a, b, d));
                m->idx.push_back(make_uint3(a, d, c));
            }
        for (size_t t = first; t < m->idx.size(); ++t) {
            const uint3 tr = m->idx[t];
            const float3 nn = cross(m->pos[tr.y] - m->pos[tr.x], m->pos[tr.z] - m->pos[tr.x]);
            m->nrm[tr.x] += nn; m->nrm[tr.y] += nn; m->nrm[tr.z] += nn;
        }
        for (int j = 0; j < n; ++j) {   // the seam
            const size_t a0 = base + size_t(j) * (around + 1), a1 = a0 + around;
            m->nrm[a0] = m->nrm[a1] = m->nrm[a0] + m->nrm[a1];
        }
        for (size_t v = base; v < m->pos.size(); ++v) {
            const float L = length(m->nrm[v]);
            m->nrm[v] = L > 1e-12f ? m->nrm[v] / L : w;
        }
    }
    // a cylinder (a mast, a spar) with an optional jagged broken top
    void spar(float3 p0, float3 p1, float r0, float r1, float jag, unsigned seed) {
        std::vector<float2> prof;
        const float L = length(p1 - p0);
        const int n = std::max(2, int(L / 0.3f));
        for (int k = 0; k <= n; ++k) prof.push_back(make_float2(r0 + (r1 - r0) * float(k) / n, L * float(k) / n));
        const size_t first = m->pos.size();
        lathe(p0, p1 - p0, prof, 16, 0.02f, seed);
        if (jag > 0.0f) {   // the broken end: the last ring's vertices pulled to ragged lengths, splinters
            const float3 w = normalize(p1 - p0);
            for (int i = 0; i <= 16; ++i) {
                const size_t v = first + size_t(n) * 17 + size_t(i);
                const float h = jag * (kn::hash01(int(seed), i % 16, 3) - 0.6f) * (1.0f + 0.8f * std::sin(i * 2.7f));
                m->pos[v] = m->pos[v] + w * h;
            }
        }
    }
};

struct Damage {
    unsigned seed = 3;
    float rnd(int a, int b, int c) const { return kn::hash01(a, b + int(seed) * 131, c); }
    // the starboard side fell in amidships and forward: its planking stands only low; the stern castle stands
    float starboardKeep(float x) const {
        const float low = 1.3f + 1.4f * (0.5f + 0.5f * kn::noise3(x * 0.25f, 1.3f, float(seed)));
        const float sternHold = sstep(-8.0f, -12.0f, x);
        return low + sternHold * 20.0f;
    }
};
}   // namespace

KoyWreckPlacement koyBuildWreck(const Config& cfg, const std::function<float(float, float)>& floorY, unsigned int matWood, unsigned int matIron,
                                unsigned int matStone, unsigned int id, std::vector<KMeshCPU>& out, std::string* log) {
    Hull H;
    Damage D;
    D.seed = unsigned(cfg.i("batik.seed", 3));
    std::mt19937 rng(D.seed * 7919u + 1u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    KMeshCPU wood, iron, stone;
    wood.name = "batik (ahsap)";
    iron.name = "batik (demir)";
    stone.name = "batik (safra)";
    wood.material = matWood;
    iron.material = matIron;
    stone.material = matStone;
    wood.id = iron.id = stone.id = id;
    wood.aoRange = iron.aoRange = stone.aoRange = 9.0f;   // the hold and the decks are rooms: the occlusion must see across them
    Emit W{&wood}, I{&iron}, S{&stone};
    const float3 X = make_float3(1, 0, 0), Y = make_float3(0, 1, 0), Z = make_float3(0, 0, 1);
    const float plankT = 0.08f, frameT = 0.24f;
    const float dx = 0.5f;
    const int N = int((H.xb + 1.0f - H.xs) / dx);
    // ---- the hull's outer surface sampled per station along its girth (keel to rail)
    struct Section {
        float x;
        std::vector<float3> p;   // outer points, starboard (z < 0) mirrored for port
        std::vector<float> s;    // girth
    };
    auto section = [&](float x) {
        Section sec;
        sec.x = x;
        const float y0 = H.keelY(x), y1 = H.railY(x);
        float acc = 0.0f;
        float3 prev{};
        for (int j = 0;; ++j) {
            const float y = std::min(y0 + j * 0.04f, y1);
            const float3 p = make_float3(x, y, H.half(x, y));
            if (j > 0) acc += length(p - prev);
            sec.p.push_back(p);
            sec.s.push_back(acc);
            prev = p;
            if (y >= y1) break;
        }
        return sec;
    };
    auto atGirth = [&](const Section& sec, float frac) {   // the outer point at frac of the girth, its height
        const float g = frac * sec.s.back();
        size_t j = 1;
        while (j + 1 < sec.s.size() && sec.s[j] < g) ++j;
        const float t = (g - sec.s[j - 1]) / std::max(sec.s[j] - sec.s[j - 1], 1e-6f);
        return sec.p[j - 1] + (sec.p[j] - sec.p[j - 1]) * clampf1(t, 0.0f, 1.0f);
    };
    std::vector<Section> secs;
    for (int i = 0; i <= N; ++i) secs.push_back(section(H.xs + i * dx));
    // the outward normal of the port surface at (x, y) (numerically); starboard mirrors z
    auto hullN = [&](float x, float y) {
        const float e = 0.02f;
        const float3 p = make_float3(x, y, H.half(x, y));
        const float3 tx = make_float3(2 * e, H.half(x, y) * 0.0f, H.half(x + e, y) - H.half(x - e, y));
        const float3 ty = make_float3(0.0f, 2 * e, H.half(x, y + e) - H.half(x, y - e));
        float3 n = normalize(cross(ty, tx));
        if (n.z < 0.0f) n = n * -1.0f;
        (void)p;
        return n;
    };
    // ---- gunports: one row on the gun deck, both sides
    struct Port { float x, y0, y1, hw; };
    std::vector<Port> ports;
    for (float x = -13.0f; x <= 11.5f; x += 2.7f) ports.push_back({x, H.gunDeck(x) + 0.45f, H.gunDeck(x) + 1.05f, 0.34f});
    auto inPort = [&](float x, float ya, float yb) {
        for (const Port& p : ports)
            if (std::fabs(x - p.x) < p.hw + 0.25f && yb > p.y0 && ya < p.y1) return true;
        return false;
    };
    // ---- planking: strakes over the girth of every section, 0.5 m pieces (a missing piece, a port, a hole)
    const int K = 30;
    int pieces = 0, gone = 0;
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < K; ++k) {
            const float fa = float(k) / K, fb = float(k + 1) / K - 0.004f;
            const float offU = U(rng) * 9.0f;
            for (int i = 0; i < N; ++i) {
                const Section &s0 = secs[size_t(i)], &s1 = secs[size_t(i + 1)];
                if (s0.s.back() < 0.2f || s1.s.back() < 0.2f) continue;
                float3 a0 = atGirth(s0, fa), b0 = atGirth(s0, fb), a1 = atGirth(s1, fa), b1 = atGirth(s1, fb);
                const float xm = 0.5f * (s0.x + s1.x), ya = std::min(a0.y, a1.y), yb = std::max(b0.y, b1.y);
                // the wales: two strakes thicker (the black bands below the ports and at the water line)
                const bool wale = (k == 11 || k == 12 || k == 17);
                const float T = wale ? plankT * 2.0f : plankT;
                if (inPort(xm, ya, yb)) continue;
                bool keep;
                const float ym = 0.5f * (ya + yb);
                // rot works in patches: a slow noise over the side decides, single planks go only at its edges
                const float rot = kn::fbm3(xm * 0.22f, ym * 0.35f, side * 3.7f + float(D.seed), 3) + 0.08f * (D.rnd(i, k, 3) - 0.5f);
                if (side < 0) keep = yb < D.starboardKeep(xm) && rot < 0.32f;
                else keep = rot < 0.38f && !(xm > 1.5f && xm < 5.5f && ya > 1.4f && yb < 3.8f + 0.6f * std::sin(xm * 2.0f));   // a hole stove in the port side
                if (H.half(xm, 0.5f * (ya + yb)) < 0.25f) keep = keep && true;
                if (!keep) { ++gone; continue; }
                const float sz = float(side);
                auto P = [&](float3 q) { return make_float3(q.x, q.y, sz * q.z); };
                const float3 na0 = hullN(a0.x, a0.y), nb0 = hullN(b0.x, b0.y), na1 = hullN(a1.x, a1.y), nb1 = hullN(b1.x, b1.y);
                auto Nn = [&](float3 n) { return make_float3(n.x, n.y, sz * n.z); };
                const float3 A0 = P(a0), B0 = P(b0), A1 = P(a1), B1 = P(b1);
                const float3 iA0 = A0 - Nn(na0) * T, iB0 = B0 - Nn(nb0) * T, iA1 = A1 - Nn(na1) * T, iB1 = B1 - Nn(nb1) * T;
                const float gu0 = fa * 6.0f, gu1 = fb * 6.0f;   // across the plank in tiles
                const float2 u00 = make_float2(s0.x / 0.5f + offU, gu0), u01 = make_float2(s0.x / 0.5f + offU, gu1);
                const float2 u10 = make_float2(s1.x / 0.5f + offU, gu0), u11 = make_float2(s1.x / 0.5f + offU, gu1);
                W.quad(A0, A1, B1, B0, Nn(na0), Nn(na1), Nn(nb1), Nn(nb0), u00, u10, u11, u01);                               // outside
                W.quad(iA0, iB0, iB1, iA1, Nn(na0) * -1.0f, Nn(nb0) * -1.0f, Nn(nb1) * -1.0f, Nn(na1) * -1.0f, u00, u01, u11, u10);   // inside
                const float3 eTop = normalize(B0 - A0), eBot = eTop * -1.0f;
                W.flat(B0, B1, iB1, iB0, eTop, u01, u11, u11, u01);   // the edges (seen where a neighbour is gone)
                W.flat(A0, iA0, iA1, A1, eBot, u00, u00, u10, u10);
                const float3 e0 = make_float3(-1, 0, 0), e1 = make_float3(1, 0, 0);
                W.flat(A0, B0, iB0, iA0, e0, u00, u01, u01, u00);
                W.flat(A1, iA1, iB1, B1, e1, u10, u10, u11, u11);
                ++pieces;
            }
        }
    }
    // ---- frames every metre, both sides, inside the planking; broken off on the starboard side (ribs standing)
    int frames = 0;
    for (int i = 2; i < N - 1; i += 2) {
        const Section& sec = secs[size_t(i)];
        if (sec.s.back() < 0.5f) continue;
        for (int side = -1; side <= 1; side += 2) {
            float top = 1.0f;
            if (side < 0) top = clampf1((D.starboardKeep(sec.x) + 0.6f + 2.6f * D.rnd(i, 0, 7)) / sec.p.back().y, 0.0f, 1.0f);
            if (side > 0 && D.rnd(i, 1, 8) < 0.08f) top = 0.4f + 0.4f * D.rnd(i, 2, 8);
            const int nseg = 24;
            const float sz = float(side);
            for (int q = 0; q < nseg; ++q) {
                const float f0 = float(q) / nseg, f1 = float(q + 1) / nseg;
                if (f0 >= top) break;
                const float3 o0 = atGirth(sec, f0), o1 = atGirth(sec, std::min(f1, top));
                const float3 n0 = hullN(o0.x, o0.y), n1 = hullN(o1.x, o1.y);
                auto P = [&](float3 v) { return make_float3(v.x, v.y, sz * v.z); };
                auto Nn = [&](float3 n) { return make_float3(n.x, n.y, sz * n.z); };
                const float3 c0 = P(o0) - Nn(n0) * plankT, c1 = P(o1) - Nn(n1) * plankT;
                const float3 d0 = c0 - Nn(n0) * frameT, d1 = c1 - Nn(n1) * frameT;
                const float hw = 0.1f;
                const float3 xo = X * hw;
                const float2 t0 = make_float2(f0 * 8.0f, 0.0f), t1 = make_float2(f1 * 8.0f, 0.4f);
                W.flat(c0 - xo, c1 - xo, d1 - xo, d0 - xo, X * -1.0f, t0, t1, t1, t0);
                W.flat(c0 + xo, d0 + xo, d1 + xo, c1 + xo, X, t0, t0, t1, t1);
                W.quad(d0 - xo, d1 - xo, d1 + xo, d0 + xo, Nn(n0) * -1.0f, Nn(n1) * -1.0f, Nn(n1) * -1.0f, Nn(n0) * -1.0f, t0, t1, t1, t0);
                W.quad(c0 - xo, c0 + xo, c1 + xo, c1 - xo, Nn(n0), Nn(n0), Nn(n1), Nn(n1), t0, t0, t1, t1);
                if (f1 >= top) W.flat(c1 - xo, c1 + xo, d1 + xo, d1 - xo, normalize(o1 - o0), t1, t1, t1, t1);   // the broken end
            }
            ++frames;
        }
    }
    // ---- keel, keelson, the stem and the sternpost
    W.box(make_float3(0.5f * (H.xs + H.xb - 4.0f), 0.2f, 0.0f), X, Y, Z, make_float3(0.5f * (H.xb - 4.0f - H.xs), 0.2f, 0.18f), 0.5f, make_float2(1, 2));
    W.box(make_float3(-1.0f, 0.75f, 0.0f), X, Y, Z, make_float3(15.0f, 0.18f, 0.2f), 0.5f, make_float2(3, 1));
    for (int k = 0; k < 10; ++k) {   // the stem: a curved timber along the bow's profile
        const float y0 = H.keelY(H.xb - 5.0f + 0.5f * k) * 0.0f + 9.5f * float(k) / 10.0f, y1 = 9.5f * float(k + 1) / 10.0f;
        const float3 a = make_float3(H.stemX(y0) + 0.05f, y0, 0.0f), b = make_float3(H.stemX(y1) + 0.05f, y1, 0.0f);
        const float3 ax = normalize(b - a), az = Z, ay = normalize(cross(az, ax));
        W.box((a + b) * 0.5f, ax, ay, az, make_float3(0.5f * length(b - a) + 0.05f, 0.18f, 0.16f), 0.5f, make_float2(float(k), 0));
    }
    W.box(make_float3(H.xs - 0.1f, 0.5f * H.railY(H.xs) - 0.5f, 0.0f), Y, X, Z, make_float3(0.5f * H.railY(H.xs) - 0.5f, 0.2f, 0.18f), 0.5f, make_float2(5, 5));
    // ---- decks: beams across, planks along (0.5 m pieces), hatches; collapsed where the starboard side went
    auto deck = [&](float xa, float xb, const std::function<float(float)>& yOf, float keep, bool collapseStarboard, const std::vector<float4>& holes,
                    int tag) {
        for (float x = xa + 0.3f; x < xb; x += 1.0f) {   // beams
            const float y = yOf(x) - plankT - 0.13f, hw = H.half(x, y) - 0.25f;
            if (hw < 0.5f) continue;
            float z0 = -hw, z1 = hw;
            if (collapseStarboard && x > -5.0f && x < 13.0f) {
                if (D.rnd(int(x * 3), tag, 11) < 0.6f) continue;   // broken and gone
                z0 = -0.3f - 2.0f * D.rnd(int(x * 3), tag, 12);     // a stub on the port side
            }
            W.box(make_float3(x, y, 0.5f * (z0 + z1)), Z, Y, X * -1.0f, make_float3(0.5f * (z1 - z0), 0.13f, 0.14f), 0.5f, make_float2(x, float(tag)), 0.45f);
        }
        // planks: boards of 3-6 m along x; a 0.5 m step decides where a board is whole (inside the hull, not a hatch, not
        // rotted through: a slow noise, the starboard part gone where the side fell); a board is emitted per whole run
        int b = 0;
        for (float z = -5.2f; z < 5.2f; z += 0.25f, ++b) {
            const float off = U(rng) * 9.0f;
            float runA = 0.0f;
            bool inRun = false;
            float boardEnd = xa + 3.0f + 3.0f * U(rng) - 2.0f * U(rng);
            auto flush = [&](float x1) {
                if (inRun && x1 - runA > 0.3f) {
                    const float xm = 0.5f * (runA + x1), y = yOf(xm) - 0.5f * plankT;
                    const float s = (yOf(x1) - yOf(runA)) / (x1 - runA);   // the deck's sheer along the board
                    const float3 ax = normalize(make_float3(1.0f, s, 0.0f)), ay = normalize(make_float3(-s, 1.0f, 0.0f));
                    W.box(make_float3(xm, y, z), ax, ay, Z, make_float3(0.5f * (x1 - runA) - 0.004f, 0.5f * plankT, 0.115f), 0.5f, make_float2(runA / 0.5f + off, 0.0f), 0.5f);
                }
                inRun = false;
            };
            for (float x = xa; x < xb; x += 0.5f) {
                const float xm = x + 0.25f, y = yOf(xm) - 0.5f * plankT;
                bool ok = std::fabs(z) + 0.12f <= H.half(xm, y - 0.2f) - 0.12f;
                for (const float4& h : holes)
                    if (xm > h.x && xm < h.y && z > h.z && z < h.w) ok = false;
                if (collapseStarboard && xm > -5.0f && xm < 13.0f && z < 0.4f + 1.5f * kn::noise3(xm * 0.4f, float(tag), 2.0f)) ok = false;
                if (kn::fbm3(xm * 0.3f, z * 0.45f, float(tag) * 5.1f, 3) > keep - 0.5f) ok = false;
                if (x >= boardEnd) {   // a butt joint: the next board
                    flush(x);
                    boardEnd = x + 3.0f + 3.0f * U(rng);
                }
                if (ok && !inRun) { runA = x; inRun = true; }
                if (!ok) flush(x);
            }
            flush(xb);
        }
    };
    const std::vector<float4> hatches = {make_float4(-1.0f, 1.6f, -1.1f, 1.1f), make_float4(7.0f, 8.6f, -0.9f, 0.9f), make_float4(-5.5f, -4.4f, -0.8f, 0.8f)};
    deck(-16.5f, 16.0f, [&](float x) { return H.gunDeck(x); }, 0.93f, true, hatches, 1);
    deck(-18.4f, 17.0f, [&](float x) { return H.sheer(x); }, 0.86f, true, hatches, 2);
    deck(H.xF, 18.2f, [&](float x) { return H.foreDeck(x); }, 0.8f, true, {}, 3);
    deck(-18.6f, H.xQ, [&](float x) { return H.quarterDeck(x); }, 0.9f, false, {make_float4(-9.0f, -8.0f, 1.0f, 2.2f)}, 4);
    deck(-18.8f, H.xP, [&](float x) { return H.poopDeck(x); }, 0.86f, false, {}, 5);
    // ---- bulkheads: the quarterdeck's and the poop's fronts (a door and windows), the forecastle's back (a door)
    auto bulkhead = [&](float x, float y0, float y1, float doorZ, int tag) {
        for (float z = -5.0f; z < 5.0f; z += 0.26f) {
            const float hw = H.half(x, 0.5f * (y0 + y1));
            if (std::fabs(z) + 0.13f > hw - 0.1f) continue;
            float ya = y0, yb = y1;
            const bool door = std::fabs(z - doorZ) < 0.55f;
            const bool win = (std::fabs(std::fabs(z) - 2.6f) < 0.35f);
            if (door) ya = y0 + 1.9f;
            if (ya >= yb - 0.05f) continue;
            if (win) {   // a window: the boards below and above it
                const float wa = y0 + 1.1f, wb = y0 + 1.7f;
                if (wa - ya > 0.05f) W.box(make_float3(x, 0.5f * (ya + wa), z), Y, X, Z, make_float3(0.5f * (wa - ya), 0.04f, 0.125f), 0.5f, make_float2(z, float(tag)));
                if (yb - wb > 0.05f) W.box(make_float3(x, 0.5f * (wb + yb), z), Y, X, Z, make_float3(0.5f * (yb - wb), 0.04f, 0.125f), 0.5f, make_float2(z, float(tag)));
                continue;
            }
            if (D.rnd(int(z * 4), tag, 30) < 0.07f) continue;
            W.box(make_float3(x, 0.5f * (ya + yb), z), Y, X, Z, make_float3(0.5f * (yb - ya), 0.04f, 0.125f), 0.5f, make_float2(z * 2.0f, float(tag)));
        }
    };
    bulkhead(H.xQ, H.sheer(H.xQ), H.quarterDeck(H.xQ) - plankT, 0.6f, 1);
    bulkhead(H.xP, H.quarterDeck(H.xP), H.poopDeck(H.xP) - plankT, -0.6f, 2);
    bulkhead(H.xF, H.sheer(H.xF), H.foreDeck(H.xF) - plankT, 0.4f, 3);
    // ---- the transom: boards across from the wing transom up to the taffrail, two rows of the great cabin's windows
    {
        const float x = H.xs + 0.25f, y0 = 4.2f, y1 = H.railY(H.xs) + 0.6f;
        int r = 0;
        for (float y = y0; y < y1; y += 0.24f, ++r) {
            const float hw = H.half(x + 0.3f, y) * 1.02f + 0.05f;
            if (hw < 0.3f) continue;
            // the window rows: openings at z = +-0.9, +-2.5 (0.7 wide), y in [9.0, 9.9] and [11.1, 12.0]
            std::vector<float2> gaps;
            for (float wy : {9.0f, 11.1f})
                if (y + 0.12f > wy && y - 0.12f < wy + 0.9f)
                    for (float wz : {-2.5f, -0.9f, 0.9f, 2.5f}) gaps.push_back(make_float2(wz - 0.35f, wz + 0.35f));
            std::sort(gaps.begin(), gaps.end(), [](float2 a, float2 b) { return a.x < b.x; });
            float z = -hw;
            for (size_t g = 0; g <= gaps.size(); ++g) {
                const float ze = g < gaps.size() ? gaps[g].x : hw;
                if (ze - z > 0.08f && !(D.rnd(r, int(g), 40) < 0.05f))
                    W.box(make_float3(x, y, 0.5f * (z + ze)), Z, Y, X * -1.0f, make_float3(0.5f * (ze - z), 0.115f, 0.06f), 0.5f, make_float2(float(r) * 1.7f, y));
                if (g < gaps.size()) z = gaps[g].y;
            }
        }
        // the windows' frames: a mullion and a transom bar in each, and the cabin behind is dark
        for (float wy : {9.0f, 11.1f})
            for (float wz : {-2.5f, -0.9f, 0.9f, 2.5f}) {
                W.box(make_float3(x - 0.02f, wy + 0.45f, wz), Y, X, Z, make_float3(0.45f, 0.05f, 0.03f), 0.5f, make_float2(wz, wy));
                W.box(make_float3(x - 0.02f, wy + 0.45f, wz), Z, Y, X * -1.0f, make_float3(0.35f, 0.03f, 0.05f), 0.5f, make_float2(wy, wz));
            }
        // the taffrail: an arched board resting on the transom's top, rising 0.7 m at the middle
        for (int k = 0; k < 14; ++k) {
            const float za = -3.0f + 6.0f * k / 14.0f, zb = -3.0f + 6.0f * (k + 1) / 14.0f, zm = 0.5f * (za + zb);
            const float top = y1 + 0.7f * (1.0f - (zm / 3.0f) * (zm / 3.0f));
            W.box(make_float3(x, 0.5f * (y1 - 0.15f + top), zm), Z, Y, X * -1.0f, make_float3(0.5f * (zb - za), 0.5f * (top - y1 + 0.15f), 0.07f), 0.5f,
                  make_float2(float(k), 2.0f));
        }
        // the transom's dress: moulded rails across under and over the window rows, pilasters between the windows
        for (float ry : {8.75f, 10.05f, 10.85f, 12.15f})
            W.box(make_float3(x - 0.08f, ry, 0.0f), Z, Y, X * -1.0f, make_float3(H.half(x + 0.3f, ry) * 0.98f, 0.07f, 0.06f), 0.5f, make_float2(ry, 1.0f));
        for (float pz : {-3.3f, -1.7f, 0.0f, 1.7f, 3.3f})
            if (std::fabs(pz) < H.half(x + 0.3f, 10.5f) - 0.1f)
                W.box(make_float3(x - 0.1f, 10.45f, pz), Y, X, Z, make_float3(1.75f, 0.06f, 0.1f), 0.5f, make_float2(pz, 3.0f));
    }
    // ---- quarter galleries: boxes on the stern's sides with three window recesses each
    for (int side = -1; side <= 1; side += 2) {
        const float x0 = -18.6f, x1 = -15.8f, y0 = 8.4f, y1 = 12.2f;
        const float zIn = H.half(-17.2f, 10.0f) - 0.05f;
        const float sz = float(side);
        if (side < 0 && D.rnd(9, 9, 9) < 0.5f) continue;   // the starboard one fell
        W.box(make_float3(0.5f * (x0 + x1), 0.5f * (y0 + y1), sz * (zIn + 0.35f)), X, Y, Z, make_float3(0.5f * (x1 - x0), 0.5f * (y1 - y0), 0.35f), 0.5f,
              make_float2(3.0f, 7.0f));
        for (int w = 0; w < 3; ++w)   // the window recesses: dark wood set in
            W.box(make_float3(x0 + 0.5f + 0.9f * w, 10.4f, sz * (zIn + 0.72f)), X, Y, Z, make_float3(0.3f, 0.45f, 0.03f), 0.3f, make_float2(float(w), 1.0f));
    }
    // ---- gunport lids: a few hanging open from their top hinge on the port side
    for (size_t k = 0; k < ports.size(); ++k) {
        if (D.rnd(int(k), 5, 50) < 0.5f) continue;
        const Port& p = ports[k];
        const float zc = H.half(p.x, p.y1) + 0.05f;
        const float open = 0.4f + 1.0f * D.rnd(int(k), 6, 51);   // radians out from the side
        const float3 hinge = make_float3(p.x, p.y1, zc);
        const float3 down = normalize(make_float3(0.0f, -std::cos(open), std::sin(open)));
        W.box(hinge + down * 0.32f, X, down, normalize(cross(X, down)), make_float3(0.36f, 0.32f, 0.05f), 0.5f, make_float2(float(k), 3.0f));
    }
    // ---- masts: stumps through the decks, broken ragged; the mizzen low
    struct Mast { float x, r, top; };
    const Mast masts[3] = {{12.4f, 0.36f, H.foreDeck(12.4f) + 1.4f}, {1.6f, 0.42f, H.sheer(1.6f) + 5.2f}, {-10.0f, 0.28f, H.quarterDeck(-10.0f) + 1.9f}};
    for (int k = 0; k < 3; ++k) W.spar(make_float3(masts[k].x, 0.9f, 0.0f), make_float3(masts[k].x, masts[k].top, 0.0f), masts[k].r, masts[k].r * 0.95f, 0.7f, unsigned(k + 3));
    W.spar(make_float3(16.8f, H.foreDeck(16.8f) + 0.2f, 0.0f), make_float3(20.2f, H.foreDeck(16.8f) + 2.0f, 0.0f), 0.3f, 0.26f, 0.5f, 9u);   // the bowsprit's stump
    // ---- the rudder, hanging askew from the sternpost
    {
        const float a = 0.45f;   // turned out to port
        const float3 ax = make_float3(-std::cos(a), 0.0f, std::sin(a));
        for (float y = 0.4f; y < 7.6f; y += 0.6f) {
            const float w = 1.7f - 0.11f * y;
            W.box(make_float3(H.xs - 0.25f, y, 0.0f) + ax * (0.5f * w), ax, Y, normalize(cross(ax, Y)), make_float3(0.5f * w, 0.3f, 0.14f), 0.5f, make_float2(y, 4.0f));
        }
    }
    // ---- guns: on the gun deck behind the port side's ports, on their carriages; some knocked over inside
    std::vector<float2> gunProf = {{0.0f, -0.02f}, {0.07f, 0.0f}, {0.08f, 0.08f}, {0.05f, 0.14f}, {0.21f, 0.2f}, {0.24f, 0.28f}, {0.23f, 0.9f},
                                    {0.21f, 0.95f}, {0.2f, 1.6f}, {0.18f, 1.65f}, {0.165f, 2.3f}, {0.19f, 2.45f}, {0.19f, 2.6f}, {0.08f, 2.62f}, {0.07f, 2.4f}, {0.0f, 2.4f}};
    int guns = 0;
    for (size_t k = 0; k < ports.size(); ++k) {
        const Port& p = ports[k];
        const float gy = H.gunDeck(p.x) + 0.62f, zc = H.half(p.x, gy) - 2.9f;
        if (D.rnd(int(k), 7, 60) < 0.25f) continue;
        const bool over = D.rnd(int(k), 8, 61) < 0.3f;
        const float3 w = over ? normalize(make_float3(0.6f, -0.1f, 0.5f)) : make_float3(0.0f, 0.0f, 1.0f);
        I.lathe(make_float3(p.x, gy - (over ? 0.35f : 0.0f), over ? zc - 0.8f : zc), w, gunProf, 18, 0.03f, unsigned(k));
        if (!over)   // the carriage
            W.box(make_float3(p.x, gy - 0.38f, zc + 1.0f), Z, Y, X * -1.0f, make_float3(0.7f, 0.2f, 0.33f), 0.5f, make_float2(float(k), 5.0f));
        ++guns;
    }
    // ---- ballast stones in the hold (most under the sand that filled it)
    for (int k = 0; k < 160; ++k) {
        const float x = -11.0f + 22.0f * U(rng), z = (U(rng) - 0.5f) * 3.4f, r = 0.07f + 0.12f * U(rng);
        S.box(make_float3(x, 1.25f + 0.1f * U(rng), z), normalize(make_float3(U(rng) - 0.5f, 0.3f * (U(rng) - 0.5f), U(rng) - 0.5f) + X), Y, Z,
              make_float3(r * 1.2f, r * 0.7f, r), 0.3f, make_float2(0, 0), 0.5f);
        // (boxes turned roughly: the cheap stand-in for ballast cobbles; S.box uses its own axes)
    }
    // ---- place it: the keel's ends on the slope (buried), bow down, listing to starboard
    KoyWreckPlacement pl{};
    {
        const float3 c = cfg.v3("batik.centre", make_float3(2.0f, 0.0f, -97.0f));
        const float yaw = cfg.f("batik.deg", 35.0f) * kPi / 180.0f;
        const float3 d = make_float3(std::sin(yaw), 0.0f, -std::cos(yaw));   // towards the bow, seaward
        const float bury = cfg.f("batik.bury", 1.3f), list = cfg.f("batik.list_deg", 18.0f) * kPi / 180.0f;
        const float3 sternW = c - d * 19.0f, bowW = c + d * 15.0f;
        const float ys = floorY(sternW.x, sternW.z), yb = floorY(bowW.x, bowW.z);
        const float pitch = std::atan2(yb - ys, 34.0f);
        const float3 X1 = normalize(d * std::cos(pitch) + make_float3(0, 1, 0) * std::sin(pitch));
        const float3 Y1 = normalize(make_float3(0, 1, 0) * std::cos(pitch) - d * std::sin(pitch));
        const float3 Z1 = cross(X1, Y1);
        // list: the port side (z) up, the starboard down
        const float3 Y2 = Y1 * std::cos(list) - Z1 * std::sin(list), Z2 = Z1 * std::cos(list) + Y1 * std::sin(list);
        const float3 O = make_float3(c.x, floorY(c.x, c.z) - bury, c.z);
        const float m[12] = {X1.x, Y2.x, Z2.x, O.x, X1.y, Y2.y, Z2.y, O.y, X1.z, Y2.z, Z2.z, O.z};
        std::copy(m, m + 12, pl.m);
        auto toW = [&](float3 p) { return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]); };
        auto toWv = [&](float3 v) { return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z); };
        for (KMeshCPU* q : {&wood, &iron, &stone}) {
            for (float3& p : q->pos) p = toW(p);
            for (float3& n : q->nrm) n = normalize(toWv(n));
        }
        // where the fans grow (ship frame first): the port rail every ~1.2 m, the castles' rails and the taffrail, the
        // broken tops of the starboard ribs, the mast stumps' tops
        {
            std::vector<std::pair<float3, float3>> loc;
            for (float x = -17.5f; x < 17.0f; x += 0.8f) {
                const float y = H.railY(x) - 0.05f;
                if (D.rnd(int(x * 5), 1, 70) < 0.25f) continue;
                loc.push_back({make_float3(x, y, H.half(x, y) - 0.05f), normalize(make_float3(0.0f, 1.0f, 0.5f))});
            }
            for (float z = -2.6f; z <= 2.7f; z += 1.3f) loc.push_back({make_float3(H.xs + 0.25f, H.railY(H.xs) + 1.2f, z), normalize(make_float3(-0.4f, 1.0f, 0.0f))});
            for (int i = 2; i < N - 1; i += 2) {
                const float x = H.xs + i * dx;
                if (x < -8.0f || x > 15.0f || D.rnd(i, 3, 71) < 0.4f) continue;
                const float top = D.starboardKeep(x) + 0.6f + 2.6f * D.rnd(i, 0, 7);
                loc.push_back({make_float3(x, top, -H.half(x, top) + 0.3f), normalize(make_float3(0.0f, 1.0f, -0.6f))});
            }
            for (int k = 0; k < 3; ++k) loc.push_back({make_float3(masts[k].x, masts[k].top - 0.2f, 0.0f), make_float3(0.0f, 1.0f, 0.0f)});
            for (const auto& g : loc) pl.growth.push_back({toW(g.first), normalize(toWv(g.second))});
        }
        pl.bow = toW(make_float3(19.0f, 0.0f, 0.0f));
        pl.stern = toW(make_float3(-19.0f, 0.0f, 0.0f));
        // things on the sand (world): the main mast's upper part fallen to starboard, guns and planks spilled from the
        // fallen side, the anchor off the bow
        const float3 star = Z2 * -1.0f;   // the starboard side's outward direction (down-slope-ish)
        const float3 sh = make_float3(star.x, 0.0f, star.z);
        const float3 sideH = normalize(sh);
        const float3 mastBase = toW(make_float3(1.6f, 6.0f, -4.0f)) + sideH * 2.0f;
        const float3 mastDir = normalize(sideH * 0.8f + make_float3(X1.x, 0.0f, X1.z) * 0.6f);
        const float3 mastEnd = mastBase + mastDir * 14.0f;
        auto onSand = [&](float3 p, float r) { return make_float3(p.x, floorY(p.x, p.z) + 0.3f * r, p.z); };
        W.spar(onSand(mastBase, 0.38f), onSand(mastEnd, 0.3f), 0.38f, 0.3f, 0.6f, 21u);
        W.spar(onSand(mastBase + mastDir * 5.0f - sideH * 3.0f, 0.18f), onSand(mastBase + mastDir * 6.0f + sideH * 6.5f, 0.15f), 0.18f, 0.14f, 0.3f, 22u);   // a yard
        for (int k = 0; k < 5; ++k) {   // guns on the sand
            const float3 p = toW(make_float3(-6.0f + 3.5f * k + 1.5f * U(rng), 1.0f, -6.0f)) + sideH * (1.0f + 3.0f * U(rng));
            const float a = U(rng) * 2.0f * kPi;
            const float3 w = make_float3(std::cos(a), -0.05f, std::sin(a));
            I.lathe(onSand(p, 0.1f) - make_float3(0, 0.12f, 0), w, gunProf, 18, 0.035f, unsigned(40 + k));
            ++guns;
        }
        for (int k = 0; k < 46; ++k) {   // planks and frame pieces fallen from the starboard side
            const float3 p = toW(make_float3(-9.0f + 22.0f * U(rng), 1.0f, -5.0f)) + sideH * (0.5f + 7.0f * U(rng) * U(rng));
            const float a = U(rng) * 2.0f * kPi, len = 1.2f + 3.0f * U(rng);
            const float3 ax = make_float3(std::cos(a), (U(rng) - 0.5f) * 0.12f, std::sin(a));
            const float3 az = normalize(cross(ax, make_float3(0, 1, 0)));
            const float3 ay = cross(az, ax);
            W.box(onSand(p, 0.1f), ax, ay, az, make_float3(0.5f * len, 0.045f, 0.12f + 0.04f * U(rng)), 0.5f, make_float2(U(rng) * 9.0f, 1.0f), 0.5f);
        }
        {   // the anchor: shank, crown, two curved arms with flukes, the ring; off the bow on the sand, an arm dug in
            const float3 at = onSand(pl.bow + make_float3(X1.x, 0.0f, X1.z) * 4.0f + sideH * 2.5f, 0.0f);
            const float a = 1.1f;
            const float3 sx = make_float3(std::cos(a), 0.0f, std::sin(a)), up = make_float3(0, 1, 0);
            const float3 crown = at + make_float3(0.0f, 0.12f, 0.0f);
            I.spar(crown, crown + sx * 3.3f + up * 0.25f, 0.11f, 0.09f, 0.0f, 31u);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                const float3 perp = normalize(cross(up, sx)) * float(s2);
                float3 prev = crown;
                for (int k = 1; k <= 5; ++k) {
                    const float t = float(k) / 5.0f;
                    const float3 p = crown + perp * (1.3f * std::sin(t * 1.4f)) + sx * (0.9f * (1.0f - std::cos(t * 1.4f))) + up * (s2 > 0 ? -0.25f * t : 0.35f * t);
                    I.spar(prev, p, 0.1f, 0.09f, 0.0f, unsigned(32 + k));
                    prev = p;
                }
                I.box(prev + sx * 0.25f, normalize(sx + perp * 0.2f), up, normalize(cross(sx, up)), make_float3(0.35f, 0.04f, 0.28f), 0.5f, make_float2(0, 0), 0.3f);
            }
            I.lathe(crown + sx * 3.4f + up * 0.27f, make_float3(0, 0, 1), {{0.24f, -0.05f}, {0.27f, 0.0f}, {0.24f, 0.05f}}, 16, 0.0f, 3u);   // (the ring, a squat torus stand-in)
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "batik: %d kaplama parcasi (%d eksik), %d posta, %d top; %zu + %zu + %zu ucgen", pieces, gone, frames, guns, wood.idx.size(),
                  iron.idx.size(), stone.idx.size());
    if (log) *log = buf;
    out.push_back(std::move(wood));
    out.push_back(std::move(iron));
    out.push_back(std::move(stone));
    return pl;
}
