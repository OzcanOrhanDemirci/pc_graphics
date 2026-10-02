#include "scenes/yikim/yikim_scenes.h"

#include <algorithm>
#include <cmath>

unsigned kindColor(int kind) {
    switch (kind) {
    case PK_COLUMN: return 0x9a9a96;
    case PK_BEAM: return 0xa8a49c;
    case PK_SLAB: return 0xbab6ae;
    case PK_WALL: return 0xb06a4a;
    default: return 0xa0a0a0;
    }
}

// n points over [-1, 1] at the cell centres, pushed out so that their mean square equals the uniform
// distribution's (1/3): a fibre grid then has the section's true second moment (bending stiffness).
static std::vector<float> fibreStations(int n) {
    std::vector<float> s(n);
    const float k = n > 1 ? float(n) / std::sqrt(float(n * n - 1)) : 0.0f;
    for (int i = 0; i < n; ++i) s[i] = (-1.0f + (2.0f * i + 1.0f) / n) * k;
    return s;
}

std::vector<FiberDesc> fibreGrid(float3 c, float3 u, float3 v, float a, float b, float steelArea) {
    const int nu = std::clamp(int(std::ceil(2.0f * a / 0.25f)), 2, 4);
    const int nv = std::clamp(int(std::ceil(2.0f * b / 0.25f)), 2, 4);
    const std::vector<float> su = fibreStations(nu), sv = fibreStations(nv);
    std::vector<FiberDesc> out;
    int perimeter = 0;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j)
            if (i == 0 || j == 0 || i == nu - 1 || j == nv - 1) ++perimeter;
    const float cellA = 4.0f * a * b / float(nu * nv);
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            FiberDesc f;
            f.p = c + u * (su[i] * a) + v * (sv[j] * b);
            const bool edge = i == 0 || j == 0 || i == nu - 1 || j == nv - 1;
            f.as = edge && perimeter ? steelArea / perimeter : 0.0f;
            f.ac = std::max(0.0f, cellA - f.as);
            out.push_back(f);
        }
    return out;
}

static float steelRatio(int ka, int kb, const FrameSpec& s) {
    auto r = [&](int k) { return k == PK_COLUMN ? s.rhoColumn : k == PK_BEAM ? s.rhoBeam : k == PK_SLAB ? s.rhoSlab : 0.0f; };
    if (ka == PK_COLUMN && kb == PK_COLUMN) return s.rhoColumn;
    // a beam or slab framing into something: its own bars are anchored there
    if (ka == PK_COLUMN) return r(kb);
    if (kb == PK_COLUMN) return r(ka);
    return std::min(r(ka), r(kb));
}

Poly piecePoly(const Piece& p) {
    Poly q = polyAabb(p.lo, p.hi);
    for (const PieceCut& c : p.cuts) polyClip(q, c.n, c.d, 0);
    return q;
}

bool pieceContains(const Piece& p, double3 x, double eps) {
    if (x.x < p.lo.x - eps || x.y < p.lo.y - eps || x.z < p.lo.z - eps || x.x > p.hi.x + eps || x.y > p.hi.y + eps || x.z > p.hi.z + eps)
        return false;
    for (const PieceCut& c : p.cuts)
        if (ddot(c.n, x) > c.d + eps) return false;
    return true;
}

// A cut piece owns only part of its box's faces, often a thin slanting band (where two roof slabs meet, a gable's
// sloping edge): a 4 x 4 grid laid over the whole rectangle missed it (measured: the ridge got no fibre at all, the
// gable tops carried the roof alone and tore while the house settled). The rectangle (centre c, axes u, v, half sizes
// a, b) is sampled finely instead, the samples inside both pieces kept, and up to 16 of them, evenly in order, become
// the fibres, sharing the kept area and the steel (ratio rho).
static std::vector<FiberDesc> sharedFibres(float3 c, float3 u, float3 v, float a, float b, const Piece& A, const Piece* B, float rho) {
    const int G = 24;
    std::vector<float3> kept;
    for (int i = 0; i < G; ++i)
        for (int j = 0; j < G; ++j) {
            const float3 x = c + u * (a * ((2.0f * i + 1.0f) / G - 1.0f)) + v * (b * ((2.0f * j + 1.0f) / G - 1.0f));
            if (pieceContains(A, d3(x), 1e-5) && (!B || pieceContains(*B, d3(x), 1e-5))) kept.push_back(x);
        }
    std::vector<FiberDesc> out;
    if (kept.empty()) return out;
    const float area = 4.0f * a * b / float(G * G) * float(kept.size());
    const int M = std::min<int>(16, int(kept.size()));
    for (int k = 0; k < M; ++k) {
        FiberDesc f;
        f.p = kept[size_t(k) * kept.size() / size_t(M) + kept.size() / size_t(2 * M)];
        f.as = rho * area / float(M);
        f.ac = std::max(0.0f, area / float(M) - f.as);
        out.push_back(f);
    }
    return out;
}
static bool isCut(const Piece& p) { return !p.cuts.empty(); }

void glueTouching(PhysWorld& w, std::vector<Piece>& P, int mat, const FrameSpec& spec, bool groundBond) {
    const double eps = 1e-6, minSide = 0.05;
    for (size_t i = 0; i < P.size(); ++i) {
        // the ground: the foot of every piece standing on y = 0
        if (groundBond && std::fabs(P[i].lo.y) < eps) {
            const double ax = (P[i].hi.x - P[i].lo.x) * 0.5, az = (P[i].hi.z - P[i].lo.z) * 0.5;
            InterfaceDesc d;
            d.a = P[i].body;
            d.b = 0;
            d.n = make_float3(0, -1, 0);
            d.len = float(P[i].hi.y - P[i].lo.y) * 0.5f + 0.3f;   // the footing below grade
            d.mat = P[i].kind == PK_WALL && spec.matBrick >= 0 ? spec.matBrick : mat;
            const float As = steelRatio(P[i].kind, P[i].kind, spec) * float(4.0 * ax * az);
            const float3 c0 = make_float3(float((P[i].lo.x + P[i].hi.x) * 0.5), 0.0f, float((P[i].lo.z + P[i].hi.z) * 0.5));
            d.fibers = isCut(P[i]) ? sharedFibres(c0, make_float3(1, 0, 0), make_float3(0, 0, 1), float(ax), float(az), P[i], nullptr,
                                                  steelRatio(P[i].kind, P[i].kind, spec))
                                   : fibreGrid(c0, make_float3(1, 0, 0), make_float3(0, 0, 1), float(ax), float(az), As);
            if (!d.fibers.empty()) w.addInterface(d);
        }
        for (size_t j = i + 1; j < P.size(); ++j) {
            const Piece &A = P[i], &B = P[j];
            for (int k = 0; k < 3; ++k) {
                if (A.unglued & B.unglued & (1u << k)) continue;
                const double aLo = (&A.lo.x)[k], aHi = (&A.hi.x)[k], bLo = (&B.lo.x)[k], bHi = (&B.hi.x)[k];
                int dir = 0;
                if (std::fabs(aHi - bLo) < eps) dir = 1;          // B on A's + side
                else if (std::fabs(bHi - aLo) < eps) dir = -1;    // B on A's - side
                if (!dir) continue;
                const int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
                const double lo1 = std::max((&A.lo.x)[k1], (&B.lo.x)[k1]), hi1 = std::min((&A.hi.x)[k1], (&B.hi.x)[k1]);
                const double lo2 = std::max((&A.lo.x)[k2], (&B.lo.x)[k2]), hi2 = std::min((&A.hi.x)[k2], (&B.hi.x)[k2]);
                if (hi1 - lo1 < minSide || hi2 - lo2 < minSide) continue;
                double3 c;
                (&c.x)[k] = dir > 0 ? aHi : aLo;
                (&c.x)[k1] = (lo1 + hi1) * 0.5;
                (&c.x)[k2] = (lo2 + hi2) * 0.5;
                float3 n = make_float3(0, 0, 0), u = make_float3(0, 0, 0), v = make_float3(0, 0, 0);
                (&n.x)[k] = float(dir);
                (&u.x)[k1] = 1.0f;
                (&v.x)[k2] = 1.0f;
                InterfaceDesc d;
                d.a = A.body;
                d.b = B.body;
                d.n = n;
                d.len = float(((aHi - aLo) + (bHi - bLo)) * 0.5);
                const bool brick = (A.kind == PK_WALL || B.kind == PK_WALL) && spec.matBrick >= 0;
                d.mat = brick ? spec.matBrick : mat;
                const float area = float((hi1 - lo1) * (hi2 - lo2));
                // brick carries no bars: a piece's own ratio (the balconies') must not leak into the brick it touches
                // (it did: the balcony parapets got bars of zero strength that still stiffened their base 1.8 times,
                // and with it the tension that cracks it)
                const float rho = brick ? 0.0f : A.rho >= 0 || B.rho >= 0 ? std::max(A.rho, B.rho) : steelRatio(A.kind, B.kind, spec);
                d.fibers = isCut(A) || isCut(B) ? sharedFibres(f3(c), u, v, float(hi1 - lo1) * 0.5f, float(hi2 - lo2) * 0.5f, A, &B, rho)
                                                : fibreGrid(f3(c), u, v, float(hi1 - lo1) * 0.5f, float(hi2 - lo2) * 0.5f, rho * area);
                if (!d.fibers.empty()) w.addInterface(d);
            }
        }
    }
    // cut against cut: two pieces cut by the same plane from opposite sides share it (a roof slab on the sloping top of a
    // gable wall or an eave block). The shared region is found by sampling the plane over the boxes' overlap.
    std::vector<size_t> cut;
    for (size_t i = 0; i < P.size(); ++i)
        if (!P[i].cuts.empty()) cut.push_back(i);
    for (size_t ii = 0; ii < cut.size(); ++ii)
        for (size_t jj = ii + 1; jj < cut.size(); ++jj) {
            const Piece &A = P[cut[ii]], &B = P[cut[jj]];
            for (const PieceCut& ca : A.cuts)
                for (const PieceCut& cb : B.cuts) {
                    if (dlength(ca.n + cb.n) > 1e-6 || std::fabs(ca.d + cb.d) > 1e-6) continue;
                    const double3 lo = d3(std::max(A.lo.x, B.lo.x), std::max(A.lo.y, B.lo.y), std::max(A.lo.z, B.lo.z));
                    const double3 hi = d3(std::min(A.hi.x, B.hi.x), std::min(A.hi.y, B.hi.y), std::min(A.hi.z, B.hi.z));
                    if (hi.x < lo.x - 1e-6 || hi.y < lo.y - 1e-6 || hi.z < lo.z - 1e-6) continue;
                    const double3 n = ca.n;
                    const double3 e = std::fabs(n.y) < 0.9 ? d3(0, 1, 0) : d3(1, 0, 0);
                    const double3 u = dnormalize(dcross(n, e)), v = dcross(n, u), o = n * ca.d;
                    double s0 = 1e30, s1 = -1e30, t0 = 1e30, t1 = -1e30;
                    for (int k = 0; k < 8; ++k) {
                        const double3 c = d3(k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z);
                        s0 = std::min(s0, ddot(c - o, u)); s1 = std::max(s1, ddot(c - o, u));
                        t0 = std::min(t0, ddot(c - o, v)); t1 = std::max(t1, ddot(c - o, v));
                    }
                    if (s1 - s0 < minSide || t1 - t0 < minSide) continue;
                    InterfaceDesc d;
                    d.a = A.body;
                    d.b = B.body;
                    d.n = f3(n);
                    d.len = float(std::max(0.05, ddot(w.pos[B.body] - w.pos[A.body], n)));
                    const bool brick = (A.kind == PK_WALL || B.kind == PK_WALL) && spec.matBrick >= 0;
                    d.mat = brick ? spec.matBrick : mat;
                    const float rho = brick ? 0.0f : A.rho >= 0 || B.rho >= 0 ? std::max(A.rho, B.rho) : steelRatio(A.kind, B.kind, spec);
                    const double3 c = o + u * (0.5 * (s0 + s1)) + v * (0.5 * (t0 + t1));
                    d.fibers = sharedFibres(f3(c), f3(u), f3(v), float(s1 - s0) * 0.5f, float(t1 - t0) * 0.5f, A, &B, rho);
                    if (!d.fibers.empty()) w.addInterface(d);
                }
        }
}

// ---------------------------------------------------------------------------------------------------------
// test rigs
// ---------------------------------------------------------------------------------------------------------
void buildFreeFall(PhysWorld& w, float drop) {
    w.clear();
    w.addBody(polyAabb(d3(-0.5, drop, -0.5), d3(0.5, drop + 1.0, 0.5)), 2500.0f, false, PK_OTHER, 0xc08040);
}

void buildTower(PhysWorld& w, int n, float s) {
    for (int i = 0; i < n; ++i)
        w.addBody(polyAabb(d3(-s * 0.5, i * s, -s * 0.5), d3(s * 0.5, (i + 1) * s, s * 0.5)), 2500.0f, false, PK_OTHER, 0xb0a090);
}

void buildPyramid(PhysWorld& w, int base, float s, float x0) {
    for (int row = 0; row < base; ++row)
        for (int i = 0; i < base - row; ++i) {
            const double x = x0 + (i - (base - row - 1) * 0.5) * s * 1.02;
            w.addBody(polyAabb(d3(x - s * 0.5, row * s, -s * 0.5), d3(x + s * 0.5, (row + 1) * s, s * 0.5)), 2500.0f, false, PK_OTHER, 0x90a0b0);
        }
}

int buildSlope(PhysWorld& w, float angleDeg) {
    w.clear();
    const double a = angleDeg * 3.14159265358979 / 180.0;
    // a 20 m x 1 m thick ramp tilted about z, top surface through (0, 5, 0)
    const double3 ax = d3(std::cos(a), std::sin(a), 0), ay = d3(-std::sin(a), std::cos(a), 0), az = d3(0, 0, 1);
    const double3 top = d3(0, 5, 0);
    w.addBody(polyBox(top - ay * 0.5, d3(10, 0.5, 3), ax, ay, az), 2500.0f, true, PK_OTHER, 0x707070);
    // a 1 m block resting on it (its bottom face on the ramp's top face)
    return w.addBody(polyBox(top + ay * 0.5, d3(0.5, 0.5, 0.5), ax, ay, az), 2500.0f, false, PK_OTHER, 0xc08040);
}

int buildCantilever(PhysWorld& w, int segs, float L, float b, float h, const PhysMaterial& m, float rho, float density) {
    w.clear();
    const int mat = w.addMaterial(m);
    const double y0 = 5.0;
    const int wall = w.addBody(polyAabb(d3(-1.0, y0 - 1.5, -1.0), d3(0.0, y0 + 1.5, 1.0)), 2500.0f, true, PK_OTHER, 0x707070);
    int prev = wall;
    for (int i = 0; i < segs; ++i) {
        const int body = w.addBody(polyAabb(d3(i * L, y0 - h * 0.5, -b * 0.5), d3((i + 1) * L, y0 + h * 0.5, b * 0.5)), density, false, PK_BEAM,
                                   0xa8a49c);
        InterfaceDesc d;
        d.a = prev;
        d.b = body;
        d.n = make_float3(1, 0, 0);
        d.len = i == 0 ? L * 0.5f : L;   // at the wall only the beam's half deforms
        d.mat = mat;
        d.fibers = fibreGrid(make_float3(float(i * L), float(y0), 0.0f), make_float3(0, 1, 0), make_float3(0, 0, 1), h * 0.5f, b * 0.5f, rho * b * h);
        w.addInterface(d);
        prev = body;
    }
    return prev;
}

void buildPile(PhysWorld& w, int count, unsigned seed) {
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0 / 16777216.0); };
    for (int i = 0; i < count; ++i) {
        const double sx = 0.3 + 0.7 * rnd(), sy = 0.2 + 0.5 * rnd(), sz = 0.3 + 0.7 * rnd();
        const double3 c = d3((rnd() - 0.5) * 3.0, 1.0 + i * 0.9, (rnd() - 0.5) * 3.0);
        const double a = rnd() * 6.283, b2 = rnd() * 6.283;
        const double3 ax = d3(std::cos(a), 0, std::sin(a));
        const double3 tmp = d3(-std::sin(a), 0, std::cos(a));
        const double3 ay = d3(0, std::cos(b2), 0) + tmp * std::sin(b2);
        const double3 az = dcross(ax, ay);
        w.addBody(polyBox(c, d3(sx * 0.5, sy * 0.5, sz * 0.5), ax, ay, az), 2500.0f, false, PK_OTHER, 0xa09080 + (i * 37 % 64));
    }
}

// ---------------------------------------------------------------------------------------------------------
// the reinforced-concrete frame
// ---------------------------------------------------------------------------------------------------------
void splitRange(double a, double b, double target, std::vector<double>& cuts) {
    const int n = std::max(1, int(std::round((b - a) / target)));
    cuts.clear();
    for (int i = 0; i <= n; ++i) cuts.push_back(a + (b - a) * i / n);
}

void addFramePieces(const FrameSpec& s, std::vector<Piece>& P) {
    auto X = [&](int i) { return i * s.spanX; };
    auto Z = [&](int j) { return j * s.spanZ; };
    std::vector<double> cuts;
    for (int st = 0; st < s.storeys; ++st) {
        const double y0 = s.level(st), y1 = s.level(st + 1);
        // columns: foot (hinge zone), shaft, joint (beam depth)
        const double ys[4] = {y0, y0 + s.hingeLen, y1 - s.beamD, y1};
        for (int i = 0; i <= s.baysX; ++i)
            for (int j = 0; j <= s.baysZ; ++j)
                for (int k = 0; k < 3; ++k)
                    P.push_back({d3(X(i) - s.colX * 0.5, ys[k], Z(j) - s.colZ * 0.5), d3(X(i) + s.colX * 0.5, ys[k + 1], Z(j) + s.colZ * 0.5), PK_COLUMN});
        // beams along x and along z, between column faces
        for (int j = 0; j <= s.baysZ; ++j)
            for (int i = 0; i < s.baysX; ++i) {
                splitRange(X(i) + s.colX * 0.5, X(i + 1) - s.colX * 0.5, s.beamSeg, cuts);
                for (size_t c = 0; c + 1 < cuts.size(); ++c)
                    P.push_back({d3(cuts[c], y1 - s.beamD, Z(j) - s.beamW * 0.5), d3(cuts[c + 1], y1, Z(j) + s.beamW * 0.5), PK_BEAM});
            }
        for (int i = 0; i <= s.baysX; ++i)
            for (int j = 0; j < s.baysZ; ++j) {
                splitRange(Z(j) + s.colZ * 0.5, Z(j + 1) - s.colZ * 0.5, s.beamSeg, cuts);
                for (size_t c = 0; c + 1 < cuts.size(); ++c)
                    P.push_back({d3(X(i) - s.beamW * 0.5, y1 - s.beamD, cuts[c]), d3(X(i) + s.beamW * 0.5, y1, cuts[c + 1]), PK_BEAM});
            }
        // slab panels inside each bay
        for (int i = 0; i < s.baysX; ++i)
            for (int j = 0; j < s.baysZ; ++j) {
                std::vector<double> cx, cz;
                splitRange(X(i) + s.beamW * 0.5, X(i + 1) - s.beamW * 0.5, s.slabPanel, cx);
                splitRange(Z(j) + s.beamW * 0.5, Z(j + 1) - s.beamW * 0.5, s.slabPanel, cz);
                for (size_t a = 0; a + 1 < cx.size(); ++a)
                    for (size_t b = 0; b + 1 < cz.size(); ++b)
                        P.push_back({d3(cx[a], y1 - s.slabT, cz[b]), d3(cx[a + 1], y1, cz[b + 1]), PK_SLAB});
            }
    }
}

void addPieceBodies(PhysWorld& w, std::vector<Piece>& P, const FrameSpec& s) {
    for (Piece& p : P) {
        const float rho = p.density > 0 ? p.density : p.kind == PK_SLAB ? s.densitySlab : s.densityConcrete;
        p.body = w.addBody(p.cuts.empty() ? polyAabb(p.lo, p.hi) : piecePoly(p), rho, false, p.kind, p.color ? p.color : kindColor(p.kind));
    }
}

std::vector<Piece> buildFrame(PhysWorld& w, const FrameSpec& s, const PhysMaterial& concrete) {
    w.clear();
    const int mat = w.addMaterial(concrete);
    std::vector<Piece> P;
    addFramePieces(s, P);
    addPieceBodies(w, P, s);
    glueTouching(w, P, mat, s, true);
    return P;
}

FrameSpec frameSpecFromConfig(const Config& c, const std::string& pre) {
    FrameSpec s;
    s.baysX = c.i(pre + "bays_x", s.baysX);
    s.baysZ = c.i(pre + "bays_z", s.baysZ);
    s.storeys = c.i(pre + "storeys", s.storeys);
    s.spanX = c.f(pre + "span_x", float(s.spanX));
    s.spanZ = c.f(pre + "span_z", float(s.spanZ));
    s.storeyH = c.f(pre + "storey_h", float(s.storeyH));
    s.colX = c.f(pre + "col_x", float(s.colX));
    s.colZ = c.f(pre + "col_z", float(s.colZ));
    s.beamW = c.f(pre + "beam_w", float(s.beamW));
    s.beamD = c.f(pre + "beam_d", float(s.beamD));
    s.slabT = c.f(pre + "slab_t", float(s.slabT));
    s.hingeLen = c.f(pre + "hinge_len", float(s.hingeLen));
    s.beamSeg = c.f(pre + "beam_seg", float(s.beamSeg));
    s.slabPanel = c.f(pre + "slab_panel", float(s.slabPanel));
    s.densityConcrete = c.f(pre + "density", s.densityConcrete);
    s.densitySlab = c.f(pre + "density_slab", s.densitySlab);
    s.rhoColumn = c.f(pre + "rho_column", s.rhoColumn);
    s.rhoBeam = c.f(pre + "rho_beam", s.rhoBeam);
    s.rhoSlab = c.f(pre + "rho_slab", s.rhoSlab);
    return s;
}

PhysMaterial concreteFromConfig(const Config& c) {
    PhysMaterial m;
    m.E = c.f("concrete.E", m.E);
    m.ft = c.f("concrete.ft", m.ft);
    m.fc = c.f("concrete.fc", m.fc);
    m.cohesion = c.f("concrete.cohesion", m.cohesion);
    m.mu = c.f("concrete.mu", m.mu);
    m.fy = c.f("steel.fy", m.fy);
    m.dowel = c.f("steel.dowel", m.dowel);
    m.steelBreak = c.f("steel.break", m.steelBreak);
    m.openBreak = c.f("concrete.open_break", m.openBreak);
    m.slipBreak = c.f("concrete.slip_break", m.slipBreak);
    m.crushStart = c.f("concrete.crush_start", m.crushStart);
    m.crushBreak = c.f("concrete.crush_break", m.crushBreak);
    m.residual = c.f("concrete.residual", m.residual);
    return m;
}
