// Scene 9 "Ayna": the beams (see ayna_beams.h).
#include "scenes/ayna/ayna_beams.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>

namespace ayna {

namespace {
bool gBulge = true;

inline float2 sub2(float2 a, float2 b) { return make_float2(a.x - b.x, a.y - b.y); }
inline float2 add2(float2 a, float2 b) { return make_float2(a.x + b.x, a.y + b.y); }
inline float2 mul2(float2 a, float s) { return make_float2(a.x * s, a.y * s); }
inline float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
inline float cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
inline float len(float2 a) { return std::sqrt(dot2(a, a)); }
inline float2 unit(float2 a) { return mul2(a, 1.0f / len(a)); }
inline float2 rot(float2 a, float th) {
    const float c = std::cos(th), s = std::sin(th);
    return make_float2(a.x * c - a.y * s, a.x * s + a.y * c);
}
inline float angRel(float2 a, float2 x) { return std::atan2(cross2(a, x), dot2(a, x)); }

// an interval of directions: from `a`, counter-clockwise over `w` (< pi), or all round
struct Iv {
    float2 a{1, 0};
    float w = 0.0f;
    bool full = false;
};

Iv towards(float2 S, float2 q0, float2 q1) {
    const float2 d0 = unit(sub2(q0, S)), d1 = unit(sub2(q1, S));
    Iv r;
    if (cross2(d0, d1) >= 0.0f) {
        r.a = d0;
        r.w = angRel(d0, d1);
    } else {
        r.a = d1;
        r.w = angRel(d1, d0);
    }
    return r;
}

bool intersect(const Iv& x, const Iv& y, Iv& out) {
    if (x.full) { out = y; return y.full || y.w > 0.0f; }
    if (y.full) { out = x; return x.w > 0.0f; }
    const float s = angRel(x.a, y.a), e = s + y.w;
    const float lo = std::max(0.0f, s), hi = std::min(x.w, e);
    if (hi - lo <= 1e-6f) return false;
    out.a = rot(x.a, lo);
    out.w = hi - lo;
    out.full = false;
    return true;
}

// x minus the angular range [lo, hi] (relative to x.a): up to two pieces
int subtract(const Iv& x, float lo, float hi, Iv out[2]) {
    int n = 0;
    if (hi <= 0.0f || lo >= x.w) {
        out[n++] = x;
        return n;
    }
    if (lo > 1e-6f) {
        out[n].a = x.a;
        out[n].w = lo;
        out[n].full = false;
        ++n;
    }
    if (hi < x.w - 1e-6f) {
        out[n].a = rot(x.a, hi);
        out[n].w = x.w - hi;
        out[n].full = false;
        ++n;
    }
    return n;
}

struct Xf {   // plan transform x -> L x + t
    float m00 = 1, m01 = 0, m10 = 0, m11 = 1, tx = 0, tz = 0;
    float2 apply(float2 p) const { return make_float2(m00 * p.x + m01 * p.y + tx, m10 * p.x + m11 * p.y + tz); }
    float2 lin(float2 d) const { return make_float2(m00 * d.x + m01 * d.y, m10 * d.x + m11 * d.y); }
};
// this transform after the reflection about the line through A with unit normal n
Xf afterReflect(const Xf& M, float2 A, float2 n) {
    // R x = x - 2 n (n.x) + 2 n (n.A)
    const float r00 = 1 - 2 * n.x * n.x, r01 = -2 * n.x * n.y, r10 = r01, r11 = 1 - 2 * n.y * n.y;
    const float k = 2.0f * dot2(n, A);
    const float rtx = n.x * k, rtz = n.y * k;
    Xf o;
    o.m00 = M.m00 * r00 + M.m01 * r10;
    o.m01 = M.m00 * r01 + M.m01 * r11;
    o.m10 = M.m10 * r00 + M.m11 * r10;
    o.m11 = M.m10 * r01 + M.m11 * r11;
    o.tx = M.m00 * rtx + M.m01 * rtz + M.tx;
    o.tz = M.m10 * rtx + M.m11 * rtz + M.tz;
    return o;
}
float2 reflectPoint(float2 p, float2 A, float2 n) { return sub2(p, mul2(n, 2.0f * dot2(sub2(p, A), n))); }
float2 reflectDir(float2 d, float2 n) { return sub2(d, mul2(n, 2.0f * dot2(d, n))); }

int familyOf(float2 d) {
    int best = 0;
    float bc = 1e30f;
    for (int f = 0; f < 3; ++f) {
        const float c = std::fabs(cross2(d, famDir(f)));
        if (c < bc) {
            bc = c;
            best = f;
        }
    }
    return best;
}

float distToSegment(float2 p, float2 a, float2 b) {
    const float2 ab = sub2(b, a), ap = sub2(p, a);
    const float t = std::clamp(dot2(ap, ab) / dot2(ab, ab), 0.0f, 1.0f);
    return len(sub2(ap, mul2(ab, t)));
}

// the node's power at a point d2 away (squared, from its image): its I W over d2
float binPower(const BeamNode& n, float d2) { return n.iw / d2; }

struct Item {
    int node;
    Iv iv;
    int i, j, up;
    float2 S;
    Xf M;
    float3 thr;
    unsigned char m[3];
    int depth;
    int entryK;
    unsigned char high;
};

}   // namespace

void setBeamBulge(bool on) { gBulge = on; }

void buildBeams(const Maze& mz, const Optics& opt, const Light& L, int lightIndex, float eps, int maxNodes, std::vector<BeamNode>& out,
                BeamBuildStats* stats) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    const Lattice& g = mz.g;
    const float Imax = std::max(L.color.x, std::max(L.color.y, L.color.z));
    if (Imax <= 0.0f) return;
    const float3 W0 = make_float3(opt.mirror[0][kOpticsN - 1], opt.mirror[1][kOpticsN - 1], opt.mirror[2][kOpticsN - 1]);
    int i0, j0, up0;
    locate(g, L.pos.x, L.pos.z, i0, j0, up0);
    if (!mz.inside(i0, j0, up0)) return;
    BeamBuildStats st;
    // the root: the light in its own cell
    const int base = int(out.size());
    {
        BeamNode r;
        r.S = make_float2(L.pos.x, L.pos.z);
        r.power = Imax / sqr(std::max(0.05f, L.radius));
        r.iw = Imax;
        r.parent = -1;
        r.cell = cellIndex(g, i0, j0, up0);
        r.light = short(lightIndex);
        r.entryK = 3;
        out.push_back(r);
    }
    // a spot's cone, projected on the plan
    Iv cone;
    cone.full = true;
    if (L.cosOuter > -1.0f) {
        const float2 ah = make_float2(L.axis.x, L.axis.z);
        const float ch = len(ah);
        const float sa = std::sqrt(std::max(0.0f, 1.0f - L.cosOuter * L.cosOuter));
        if (ch > sa + 1e-4f) {
            const float beta = std::asin(std::min(1.0f, sa / ch));
            cone.full = false;
            cone.a = rot(unit(ah), -beta);
            cone.w = 2.0f * beta;
        }
    }
    std::deque<Item> q;
    {
        Item it;
        it.node = base;
        it.iv.full = true;
        it.i = i0;
        it.j = j0;
        it.up = up0;
        it.S = make_float2(L.pos.x, L.pos.z);
        it.thr = make_float3(1, 1, 1);
        it.m[0] = it.m[1] = it.m[2] = 0;
        it.depth = 0;
        it.entryK = 3;
        it.high = 0;
        q.push_back(it);
    }
    const float ht = 0.5f * g.t;
    while (!q.empty()) {
        const Item it = q.front();
        q.pop_front();
        float2 A[3], E[3], N[3], C[3];
        int type[3];
        for (int k = 0; k < 3; ++k) {
            const EdgeRef e = cellEdge(it.i, it.j, it.up, k);
            float2 B;
            edgeEnds(g, e.fam, e.i, e.j, A[k], B);
            E[k] = famDir(e.fam);
            const float2 n = famNormal(e.fam);
            N[k] = make_float2(n.x * float(e.side), n.y * float(e.side));
            type[k] = mz.edge(e.fam, e.i, e.j);
        }
        cellCorners(g, it.i, it.j, it.up, C);
        for (int k = 0; k < 3; ++k) {
            if (k == it.entryK || type[k] == kEdgeOut) continue;
            const float vS = dot2(sub2(it.S, A[k]), N[k]);
            if (vS <= 1e-4f) continue;   // the image is not on this cell's side of the line: no ray reaches it from inside
            // the window's straight sides through the slab's depth (the near face and the far face); then the
            // horseshoe's bulge on either side, above the jambs' top (its beams are marked: high ways only)
            const float hb = std::max(g.wHalf, g.ra);
            for (int strip = 0; strip < (hb > g.wHalf + 1e-4f && gBulge ? 3 : 1); ++strip) {
            const float u0 = strip == 0 ? 0.5f * g.L - g.wHalf : strip == 1 ? 0.5f * g.L - hb : 0.5f * g.L + g.wHalf;
            const float u1 = strip == 0 ? 0.5f * g.L + g.wHalf : strip == 1 ? 0.5f * g.L - g.wHalf : 0.5f * g.L + hb;
            const float2 P0 = add2(A[k], mul2(E[k], u0)), P1 = add2(A[k], mul2(E[k], u1));
            Iv ap = towards(it.S, add2(P0, mul2(N[k], -ht)), add2(P1, mul2(N[k], -ht)));
            if (vS > ht + 1e-4f) {
                Iv nearIv = towards(it.S, add2(P0, mul2(N[k], ht)), add2(P1, mul2(N[k], ht)));
                Iv both;
                if (!intersect(nearIv, ap, both)) continue;
                ap = both;
            }
            Iv iv;
            if (!intersect(it.iv, ap, iv)) continue;
            if (it.entryK == 3 && !intersect(iv, cone, iv)) continue;
            // the columns' parts inside this cell (60-degree sectors at the corners) cut it
            Iv pieces[8];
            int np = 1;
            pieces[0] = iv;
            for (int c = 0; c < 3; ++c) {
                const int ci = (c + 1) % 3, cp = (c + 2) % 3;
                const float2 V = C[c];
                const float2 u1 = unit(sub2(C[ci], V)), u2 = unit(sub2(C[cp], V));
                float2 cand[5];
                int nc = 0;
                cand[nc++] = V;
                cand[nc++] = add2(V, mul2(u1, g.rc));
                cand[nc++] = add2(V, mul2(u2, g.rc));
                const float2 qv = sub2(it.S, V);
                const float dq = len(qv);
                if (dq > g.rc * 1.0001f) {
                    const float phi = std::acos(g.rc / dq);
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        const float2 tdir = rot(mul2(qv, 1.0f / dq), float(sgn) * phi);
                        // inside the sector between u1 and u2 (the corner's 60 degrees)?
                        const bool in = cross2(u1, u2) > 0.0f ? (cross2(u1, tdir) >= 0.0f && cross2(tdir, u2) >= 0.0f)
                                                               : (cross2(u2, tdir) >= 0.0f && cross2(tdir, u1) >= 0.0f);
                        if (in) cand[nc++] = add2(V, mul2(tdir, g.rc));
                    }
                }
                Iv next[8];
                int nn = 0;
                for (int p = 0; p < np; ++p) {
                    float lo = 1e30f, hi = -1e30f;
                    for (int s = 0; s < nc; ++s) {
                        const float a = angRel(pieces[p].a, sub2(cand[s], it.S));
                        lo = std::min(lo, a);
                        hi = std::max(hi, a);
                    }
                    if (hi - lo > 3.0f) {   // straddles behind: the sector is not in front of this piece
                        next[nn++] = pieces[p];
                        continue;
                    }
                    Iv o[2];
                    const int no = subtract(pieces[p], lo, hi, o);
                    for (int s = 0; s < no && nn < 8; ++s) next[nn++] = o[s];
                }
                np = nn;
                for (int p = 0; p < np; ++p) pieces[p] = next[p];
            }
            for (int p = 0; p < np; ++p) {
                if (pieces[p].w < 1e-5f) continue;
                Item ch = it;
                ch.depth = it.depth + 1;
                ch.entryK = k;
                BeamNode nd;
                nd.parent = it.node;
                nd.light = short(lightIndex);
                nd.entryK = (unsigned char)k;
                if (type[k] == kEdgeMirror) {
                    ch.S = reflectPoint(it.S, A[k], N[k]);
                    const float2 end = rot(pieces[p].a, pieces[p].w);
                    ch.iv.a = reflectDir(end, N[k]);
                    ch.iv.w = pieces[p].w;
                    ch.iv.full = false;
                    ch.M = afterReflect(it.M, A[k], N[k]);
                    ch.thr = make_float3(it.thr.x * W0.x, it.thr.y * W0.y, it.thr.z * W0.z);
                    const int fam = familyOf(it.M.lin(E[k]));
                    ch.m[fam] = (unsigned char)std::min(255, int(ch.m[fam]) + 1);
                } else {
                    ch.iv = pieces[p];
                    int ni, nj, nup;
                    cellAcross(it.i, it.j, it.up, k, ni, nj, nup);
                    if (!mz.inside(ni, nj, nup)) continue;
                    ch.i = ni;
                    ch.j = nj;
                    ch.up = nup;
                }
                const float dmin = std::max(0.05f, distToSegment(ch.S, P0, P1));
                const float power = std::max(L.color.x * ch.thr.x, std::max(L.color.y * ch.thr.y, L.color.z * ch.thr.z)) / (dmin * dmin);
                if (power < eps || int(out.size()) - base >= maxNodes) {
                    ++st.cut;
                    continue;
                }
                nd.S = ch.S;
                nd.a = ch.iv.a;
                nd.b = rot(ch.iv.a, ch.iv.w);
                nd.m00 = ch.M.m00;
                nd.m01 = ch.M.m01;
                nd.m10 = ch.M.m10;
                nd.m11 = ch.M.m11;
                nd.tx = ch.M.tx;
                nd.tz = ch.M.tz;
                // the entry window's middle and direction, seen in the light's frame
                const float2 mid = add2(A[k], mul2(E[k], 0.5f * g.L));
                nd.em = it.M.apply(mid);
                nd.ed = it.M.lin(E[k]);
                nd.power = power;
                nd.iw = power * dmin * dmin;
                nd.cell = cellIndex(g, ch.i, ch.j, ch.up);
                nd.m[0] = ch.m[0];
                nd.m[1] = ch.m[1];
                nd.m[2] = ch.m[2];
                ch.high = (unsigned char)(it.high | (strip != 0 ? 1 : 0));
                nd.high = ch.high;
                ch.node = int(out.size());
                out.push_back(nd);
                st.maxDepth = std::max(st.maxDepth, ch.depth);
                q.push_back(ch);
            }
            }
        }
    }
    st.nodes = int(out.size()) - base;
    st.ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    if (stats) *stats = st;
}

void sortIntoCells(const Maze& mz, std::vector<BeamNode>& nodes, std::vector<int>& cellStart, std::vector<int>& cellNodes) {
    // each cell's bounding box in kBins x kBins bins; a node goes to the bins its wedge reaches, ordered there by the
    // power it can give at that bin's nearest point
    const Lattice& g = mz.g;
    const int nc = cellCount(g), nb = kBins * kBins;
    std::vector<std::vector<std::pair<float, int>>> lists(size_t(nc) * nb);
    const float lx = g.L / kBins, lz = g.h / kBins;
    for (int idx = 0; idx < int(nodes.size()); ++idx) {
        const BeamNode& n = nodes[idx];
        if (n.cell < 0) continue;
        const int up = (n.cell & 1) == 0 ? 1 : 0, r = n.cell >> 1;
        const int ci = r % g.ni + g.i0, cj = r / g.ni + g.j0;
        float2 c[3];
        cellCorners(g, ci, cj, up, c);
        const float bx = std::min(c[0].x, std::min(c[1].x, c[2].x)), bz = std::min(c[0].y, std::min(c[1].y, c[2].y));
        for (int by = 0; by < kBins; ++by)
            for (int bxi = 0; bxi < kBins; ++bxi) {
                const float x0 = bx + bxi * lx, z0 = bz + by * lz;
                const float2 q[4] = {make_float2(x0, z0), make_float2(x0 + lx, z0), make_float2(x0, z0 + lz), make_float2(x0 + lx, z0 + lz)};
                if (n.entryK != 3) {
                    bool outA = true, outB = true;
                    for (int t = 0; t < 4; ++t) {
                        const float2 d = sub2(q[t], n.S);
                        if (cross2(n.a, d) >= 0.0f) outA = false;
                        if (cross2(d, n.b) >= 0.0f) outB = false;
                    }
                    if (outA || outB) continue;
                }
                // the nearest point of the bin to the image
                const float px = std::clamp(n.S.x, x0, x0 + lx), pz = std::clamp(n.S.y, z0, z0 + lz);
                const float d2 = std::max(0.0025f, sqr(px - n.S.x) + sqr(pz - n.S.y));
                // the power it can give in this bin: I W over the nearest distance, never above its own bound
                lists[size_t(n.cell) * nb + by * kBins + bxi].push_back({std::min(n.power, binPower(n, d2)), idx});
            }
    }
    cellStart.assign(size_t(nc) * nb + 1, 0);
    size_t total = 0;
    for (size_t b = 0; b < lists.size(); ++b) {
        cellStart[b] = int(total);
        total += lists[b].size();
    }
    cellStart[lists.size()] = int(total);
    // eight words an entry (two float4): the wedge test's data in line, then the node for those that pass:
    //   S.x S.y a.x a.y | b.x b.y power-in-bin index   (a root's wedge: a = b = 0, the test passes all)
    cellNodes.resize(total * 8);
    for (size_t b = 0; b < lists.size(); ++b) {
        auto& L = lists[b];
        std::sort(L.begin(), L.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (size_t k = 0; k < L.size(); ++k) {
            const BeamNode& n = nodes[L[k].second];
            const bool root = n.entryK == 3;
            float f[7] = {n.S.x, n.S.y, root ? 0.0f : n.a.x, root ? 0.0f : n.a.y, root ? 0.0f : n.b.x, root ? 0.0f : n.b.y, L[k].first};
            int* o = &cellNodes[(cellStart[b] + k) * 8];
            std::memcpy(o, f, sizeof(f));
            o[7] = L[k].second;
        }
    }
}

}   // namespace ayna
