#include "scenes/yikim/building.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

struct Opening { double u, w, v, h; };   // centre along the wall and width; bottom above the wall's base and height

// cut positions over [a, b]: the given marks (kept away from the ends and from each other, no slivers), then each gap
// split into equal parts close to target
std::vector<double> cutsOf(double a, double b, std::vector<double> marks, double target) {
    const double minGap = 0.15;
    std::sort(marks.begin(), marks.end());
    std::vector<double> keep = {a};
    for (double x : marks)
        if (x - keep.back() >= minGap && b - x >= minGap) keep.push_back(x);
    keep.push_back(b);
    std::vector<double> out;
    for (size_t k = 0; k + 1 < keep.size(); ++k) {
        const double g0 = keep[k], g1 = keep[k + 1];
        const int n = std::max(1, int(std::round((g1 - g0) / target)));
        for (int s = 0; s < n; ++s) out.push_back(g0 + (g1 - g0) * s / n);
    }
    out.push_back(b);
    return out;
}

struct WallStyle {   // how a building's walls are laid: block size, paint, weight
    double blockW, blockH;
    unsigned paint;
    float densityWall;
};

// a wall: axis 0 runs along x at z = c, axis 1 runs along z at x = c; from u0 to u1, y0 to y1, thickness t
void addWall(std::vector<Piece>& P, int axis, double c, double u0, double u1, double y0, double y1, double t, const std::vector<Opening>& open,
             const WallStyle& s) {
    if (u1 - u0 < 0.2 || y1 - y0 < 0.2) return;
    std::vector<double> um, vm;
    for (const Opening& o : open) {
        um.push_back(o.u - o.w * 0.5);
        um.push_back(o.u + o.w * 0.5);
        vm.push_back(y0 + o.v);
        vm.push_back(y0 + o.v + o.h);
    }
    const std::vector<double> us = cutsOf(u0, u1, um, s.blockW), vs = cutsOf(y0, y1, vm, s.blockH);
    for (size_t a = 0; a + 1 < us.size(); ++a)
        for (size_t b = 0; b + 1 < vs.size(); ++b) {
            const double uc = (us[a] + us[a + 1]) * 0.5, vc = (vs[b] + vs[b + 1]) * 0.5 - y0;
            bool hole = false;
            for (const Opening& o : open)
                if (std::fabs(uc - o.u) < o.w * 0.5 && vc > o.v && vc < o.v + o.h) hole = true;
            if (hole) continue;
            Piece p;
            p.kind = PK_WALL;
            if (axis == 0) { p.lo = d3(us[a], vs[b], c - t * 0.5); p.hi = d3(us[a + 1], vs[b + 1], c + t * 0.5); }
            else { p.lo = d3(c - t * 0.5, vs[b], us[a]); p.hi = d3(c + t * 0.5, vs[b + 1], us[a + 1]); }
            p.color = s.paint;
            p.density = s.densityWall;
            P.push_back(p);
        }
}
void addWall(std::vector<Piece>& P, int axis, double c, double u0, double u1, double y0, double y1, double t, const std::vector<Opening>& open,
             const ApartmentSpec& s) {
    addWall(P, axis, c, u0, u1, y0, y1, t, open, WallStyle{s.blockW, s.blockH, s.paint, s.densityWall});
}

// storey heights: "<pre>heights" (ground first), else the ground storey "<pre>ground_h" and the rest "<pre>storey_h"
void heightsFromConfig(const Config& c, const std::string& pre, FrameSpec& f) {
    const auto it = c.values.find(pre + "heights");
    f.heights.clear();
    if (it != c.values.end()) {
        const char* q = it->second.c_str();
        char* end = nullptr;
        for (double v = std::strtod(q, &end); end != q; v = std::strtod(q, &end)) {
            f.heights.push_back(v);
            q = end;
        }
    } else if (c.values.count(pre + "ground_h")) {
        f.heights.push_back(c.f(pre + "ground_h", float(f.storeyH)));
        for (int k = 1; k < f.storeys; ++k) f.heights.push_back(f.storeyH);
    }
    if (!f.heights.empty()) f.storeys = int(f.heights.size());
}


// A gable wall (along x at z = c, thickness th) under two roof planes meeting over x = xm (heights aL + tL x and
// aR - tL x; cutL, cutR keep below them), with the ridge beam (half width rb, underside yb) bearing on its middle: blocks
// in columns, the middle column under the beam ending at its underside; on either side, in each column full rows up to
// the slope's lowest point over it, then one block from there up to the slope, cut by it (no slivers: a thin wedge
// left between a row and the slope was squeezed out of the wall while the house settled, 32 mm).
void addGable(std::vector<Piece>& P, double c, double u0, double u1, double y0, double xm, double rb, double yb, double th, double tL,
              double aL, double aR, const PieceCut& cutL, const PieceCut& cutR, const WallStyle& s) {
    const std::vector<double> us = cutsOf(u0, u1, {xm - rb, xm + rb}, s.blockW);
    for (size_t a = 0; a + 1 < us.size(); ++a) {
        const double ua = us[a], ub = us[a + 1];
        const bool left = ub <= xm - rb + 1e-6, middle = !left && ua < xm + rb - 1e-6;
        const double low = middle ? yb : left ? aL + tL * ua : aR - tL * ub, high = middle ? yb : left ? aL + tL * ub : aR - tL * ua;
        if (low - y0 < 0.05) continue;
        std::vector<double> ys = {y0};
        while (ys.back() + s.blockH < low - 0.15) ys.push_back(ys.back() + s.blockH);
        ys.push_back(high);
        for (size_t b = 0; b + 1 < ys.size(); ++b) {
            Piece q;
            q.kind = PK_WALL;
            q.lo = d3(ua, ys[b], c - th * 0.5);
            q.hi = d3(ub, ys[b + 1], c + th * 0.5);
            if (b + 2 == ys.size() && !middle) q.cuts.push_back(left ? cutL : cutR);   // the top block meets the slope
            q.color = s.paint;
            q.density = s.densityWall;
            P.push_back(q);
        }
    }
}

// a vertical cylinder as a 12-sided prism: centre of the base, radius, height
Poly polyCylinder(double3 base, double r, double h) {
    Poly p = polyAabb(d3(base.x - r, base.y, base.z - r), d3(base.x + r, base.y + h, base.z + r));
    const int sides = 12;
    for (int k = 0; k < sides; ++k) {
        const double a = 2.0 * 3.14159265358979 * (k + 0.5) / sides;
        const double3 n = d3(std::cos(a), 0.0, std::sin(a));
        polyClip(p, n, ddot(n, base) + r * std::cos(3.14159265358979 / sides), 0);
    }
    return p;
}

}   // namespace

Building buildApartment(PhysWorld& w, const ApartmentSpec& sIn, const PhysMaterial& concrete, const PhysMaterial& brick) {
    ApartmentSpec s = sIn;
    w.clear();
    const int matC = w.addMaterial(concrete);
    s.frame.matBrick = w.addMaterial(brick);
    const FrameSpec& f = s.frame;
    Building A;
    std::vector<Piece>& P = A.pieces;
    addFramePieces(f, P);
    for (Piece& p : P) p.color = s.frameColor;
    auto X = [&](int i) { return i * f.spanX; };
    auto Z = [&](int j) { return j * f.spanZ; };
    const int N = f.storeys;
    const double W = X(f.baysX), D = Z(f.baysZ);
    A.width = W;
    A.depth = D;
    A.height = f.level(N);
    const int midZ = f.baysZ / 2;
    for (int st = 0; st < N; ++st) {
        const double y0 = f.level(st), y1 = f.level(st + 1), top = y1 - f.beamD;
        const bool ground = st == 0;
        // front (z = 0): the shops' glass on the ground storey; above, an opening onto the balcony in every bay
        if (!ground)
            for (int i = 0; i < f.baysX; ++i) {
                const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
                addWall(P, 0, 0.0, u0, u1, y0, top, s.wallExt, {{(u0 + u1) * 0.5, s.doorW, 0.0, s.doorH}}, s);
            }
        // back (z = D): windows
        for (int i = 0; i < f.baysX; ++i) {
            const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
            addWall(P, 0, D, u0, u1, y0, top, s.wallExt, {{(u0 + u1) * 0.5, s.winW, s.sill, s.winH}}, s);
        }
        // sides (x = 0, x = W): windows; the shops leave the front half of the ground storey's sides open
        for (int side = 0; side < 2; ++side)
            for (int j = 0; j < f.baysZ; ++j) {
                if (ground && j < midZ) continue;
                const double u0 = Z(j) + f.colZ * 0.5, u1 = Z(j + 1) - f.colZ * 0.5;
                addWall(P, 1, side ? W : 0.0, u0, u1, y0, top, s.wallExt, {{(u0 + u1) * 0.5, s.winW, s.sill, s.winH}}, s);
            }
        // inside: across the middle (z = Z(midZ)), and along the inner grid lines in alternate halves (rooms)
        for (int i = 0; i < f.baysX; ++i) {
            if (ground && i != 0 && i != f.baysX - 1) continue;   // the shops' back walls only at the ends
            const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
            std::vector<Opening> o;
            if (!ground && i == f.baysX / 2) o.push_back({(u0 + u1) * 0.5, s.inDoorW, 0.0, s.inDoorH});
            addWall(P, 0, Z(midZ), u0, u1, y0, top, s.wallInt, o, s);
        }
        if (!ground)
            for (int i = 1; i < f.baysX; ++i)
                for (int j = 0; j < f.baysZ; ++j) {
                    if ((i % 2 == 1) != (j < midZ)) continue;   // rooms on alternate sides of the corridor
                    const double u0 = Z(j) + f.colZ * 0.5, u1 = Z(j + 1) - f.colZ * 0.5;
                    addWall(P, 1, X(i), u0, u1, y0, top, s.wallInt, {{(u0 + u1) * 0.5, s.inDoorW, 0.0, s.inDoorH}}, s);
                }
        // balconies in front of the end bays of every floor above the shops (the slab at the top of this storey)
        if (st < N - 1 && s.balconies)
            for (int i : {0, f.baysX - 1}) {
                const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
                const double z1 = -f.beamW * 0.5, z0 = z1 - s.balconyDepth;
                std::vector<double> cx;
                splitRange(u0, u1, f.slabPanel, cx);
                for (size_t k = 0; k + 1 < cx.size(); ++k) {
                    Piece p;
                    p.kind = PK_SLAB;
                    p.lo = d3(cx[k], y1 - f.slabT, z0);
                    p.hi = d3(cx[k + 1], y1, z1);
                    p.color = s.frameColor;
                    p.density = s.densityBalcony;
                    p.rho = s.rhoBalcony;
                    P.push_back(p);
                }
                if (s.parapetBalcony > 0.0) addWall(P, 0, z0 + 0.05, u0, u1, y1, y1 + s.parapetBalcony, 0.10, {}, s);
            }
    }
    // the roof's parapet, on the edge beams and the column heads
    {
        const double H = A.height, t = s.wallExt;
        addWall(P, 0, 0.0, -f.colX * 0.5, W + f.colX * 0.5, H, H + s.parapetRoof, t, {}, s);
        addWall(P, 0, D, -f.colX * 0.5, W + f.colX * 0.5, H, H + s.parapetRoof, t, {}, s);
        addWall(P, 1, 0.0, t * 0.5, D - t * 0.5, H, H + s.parapetRoof, t, {}, s);
        addWall(P, 1, W, t * 0.5, D - t * 0.5, H, H + s.parapetRoof, t, {}, s);
    }
    addPieceBodies(w, P, f);
    glueTouching(w, P, matC, s.frame, true);
    // loose on the roof: water tanks (1 m3, full) at the back, solar collectors (tilted to the front) at the front
    const double H = A.height + 0.001;
    for (int k = 0; k < s.tanks; ++k) {
        const double x = W * (k + 1) / (s.tanks + 1), z = D * 0.72;
        A.loose.push_back(w.addBody(polyCylinder(d3(x, H, z), 0.55, 1.15), 1000.0f, false, PK_OTHER, s.tankColor));
    }
    for (int k = 0; k < s.collectors; ++k) {
        const double cx = W * (k + 1) / (s.collectors + 1), cz = D * 0.3;
        Poly p = polyAabb(d3(cx - 1.0, H, cz - 0.5), d3(cx + 1.0, H + 1.1, cz + 0.5));
        // the panel: rises from 0.15 m at the front edge to 1.1 m at the back
        const double3 n = dnormalize(d3(0.0, 1.0, -0.95));
        polyClip(p, n, ddot(n, d3(cx, H + 0.15, cz - 0.5)), 0);
        A.loose.push_back(w.addBody(p, 200.0f, false, PK_OTHER, s.collectorColor));
    }
    return A;
}

ApartmentSpec apartmentSpecFromConfig(const Config& c) {
    ApartmentSpec s;
    FrameSpec& f = s.frame;
    f = frameSpecFromConfig(c, "bina.");
    {
        const auto it = c.values.find("bina.heights");
        if (it != c.values.end()) {
            f.heights.clear();
            const char* p = it->second.c_str();
            char* end = nullptr;
            for (double v = std::strtod(p, &end); end != p; v = std::strtod(p, &end)) {
                f.heights.push_back(v);
                p = end;
            }
            if (!f.heights.empty()) f.storeys = int(f.heights.size());
        }
    }
    s.wallExt = c.f("bina.wall_ext", float(s.wallExt));
    s.wallInt = c.f("bina.wall_int", float(s.wallInt));
    s.blockW = c.f("bina.block_w", float(s.blockW));
    s.blockH = c.f("bina.block_h", float(s.blockH));
    s.densityWall = c.f("bina.density_wall", s.densityWall);
    s.winW = c.f("bina.window_w", float(s.winW));
    s.winH = c.f("bina.window_h", float(s.winH));
    s.sill = c.f("bina.sill", float(s.sill));
    s.doorW = c.f("bina.front_opening_w", float(s.doorW));
    s.doorH = c.f("bina.front_opening_h", float(s.doorH));
    s.balconyDepth = c.f("bina.balcony_depth", float(s.balconyDepth));
    s.balconies = c.i("bina.balconies", 1) != 0;
    s.parapetBalcony = c.f("bina.parapet_balcony", float(s.parapetBalcony));
    s.densityBalcony = c.f("bina.density_balcony", s.densityBalcony);
    s.rhoBalcony = c.f("bina.rho_balcony", s.rhoBalcony);
    s.tanks = c.i("bina.tanks", s.tanks);
    s.collectors = c.i("bina.collectors", s.collectors);
    return s;
}

PhysMaterial brickFromConfig(const Config& c) {
    PhysMaterial m;
    m.E = c.f("brick.E", 3.0e9f);
    m.ft = c.f("brick.ft", 0.25e6f);
    m.fc = c.f("brick.fc", 3.0e6f);
    m.cohesion = c.f("brick.cohesion", 0.35e6f);
    m.mu = c.f("brick.mu", 0.7f);
    m.fy = 0.0f;
    m.dowel = 0.0f;
    m.openBreak = c.f("brick.open_break", 0.01f);
    m.slipBreak = c.f("brick.slip_break", 0.03f);
    m.crushStart = c.f("brick.crush_start", 0.003f);
    m.crushBreak = c.f("brick.crush_break", 0.1f);
    m.residual = c.f("brick.residual", 0.3f);
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
// the tower
Building buildTowerBlock(PhysWorld& w, const TowerSpec& sIn, const PhysMaterial& concrete, const PhysMaterial& brick) {
    TowerSpec s = sIn;
    w.clear();
    const int matC = w.addMaterial(concrete);
    s.frame.matBrick = w.addMaterial(brick);
    const FrameSpec& f = s.frame;
    Building A;
    std::vector<Piece>& P = A.pieces;
    addFramePieces(f, P);
    auto X = [&](int i) { return i * f.spanX; };
    auto Z = [&](int j) { return j * f.spanZ; };
    const int ci = f.baysX / 2, cj = f.baysZ / 2;   // the core's bay
    // no floor inside the core: it is the lift shaft
    P.erase(std::remove_if(P.begin(), P.end(),
                           [&](const Piece& p) {
                               const double cx = 0.5 * (p.lo.x + p.hi.x), cz = 0.5 * (p.lo.z + p.hi.z);
                               return p.kind == PK_SLAB && cx > X(ci) && cx < X(ci + 1) && cz > Z(cj) && cz < Z(cj + 1);
                           }),
            P.end());
    for (Piece& p : P) p.color = s.frameColor;
    const int N = f.storeys;
    const double W = X(f.baysX), D = Z(f.baysZ);
    A.width = W;
    A.depth = D;
    A.height = f.level(N);
    const WallStyle ws{s.blockW, s.blockH, s.paint, s.densityWall};
    for (int st = 0; st < N; ++st) {
        const double y0 = f.level(st), y1 = f.level(st + 1), top = y1 - f.beamD;
        const bool ground = st == 0;
        // the core: shear walls on the four sides of its bay, from the floor to the beams, in segments
        auto core = [&](int axis, double c, double u0, double u1) {
            std::vector<double> cuts;
            splitRange(u0, u1, s.coreSeg, cuts);
            for (size_t k = 0; k + 1 < cuts.size(); ++k) {
                Piece q;
                q.kind = PK_COLUMN;
                if (axis == 0) { q.lo = d3(cuts[k], y0, c - s.coreT * 0.5); q.hi = d3(cuts[k + 1], top, c + s.coreT * 0.5); }
                else { q.lo = d3(c - s.coreT * 0.5, y0, cuts[k]); q.hi = d3(c + s.coreT * 0.5, top, cuts[k + 1]); }
                q.color = s.frameColor;
                // plain concrete: a reinforced core (a column's bars, storey to storey) stood alone, 90 m and bending,
                // once wrecking balls had knocked the frame down around it, then toppled whole (Özcan, 10-02). Without
                // bars it carries its share in compression and through an earthquake as before (magnitudes 5-7 the
                // same) and holds nothing where it is pulled. (Measured, `darbe.soy`: the middle bay alone still stands
                // after a ball, on its four reinforced columns. A brick shaft instead pancaked the tower at magnitude 7
                // into a pile of 57 thousand contacts, 53-64 ms a step.)
                q.rho = s.coreRho;
                P.push_back(q);
            }
        };
        for (int side = 0; side < 2; ++side) {
            core(0, Z(cj + side), X(ci) + f.colX * 0.5, X(ci + 1) - f.colX * 0.5);
            core(1, X(ci + side), Z(cj) + f.colZ * 0.5, Z(cj + 1) - f.colZ * 0.5);
        }
        // the facade: brick with a large window in every bay; at the front of the ground storey the lobby's glass
        for (int i = 0; i < f.baysX; ++i) {
            const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
            const Opening win{(u0 + u1) * 0.5, s.winW, s.sill, s.winH};
            if (!ground || !s.lobby) addWall(P, 0, 0.0, u0, u1, y0, top, s.wallExt, {win}, ws);
            addWall(P, 0, D, u0, u1, y0, top, s.wallExt, {win}, ws);
        }
        for (int side = 0; side < 2; ++side)
            for (int j = 0; j < f.baysZ; ++j) {
                const double u0 = Z(j) + f.colZ * 0.5, u1 = Z(j + 1) - f.colZ * 0.5;
                addWall(P, 1, side ? W : 0.0, u0, u1, y0, top, s.wallExt, {{(u0 + u1) * 0.5, s.winW, s.sill, s.winH}}, ws);
            }
    }
    {   // the parapet, on the edge beams and the column heads
        const double H = A.height, t = s.wallExt;
        const double ex = std::max(f.colX, f.beamW) * 0.5;
        addWall(P, 0, 0.0, -ex, W + ex, H, H + s.parapet, t, {}, ws);
        addWall(P, 0, D, -ex, W + ex, H, H + s.parapet, t, {}, ws);
        addWall(P, 1, 0.0, t * 0.5, D - t * 0.5, H, H + s.parapet, t, {}, ws);
        addWall(P, 1, W, t * 0.5, D - t * 0.5, H, H + s.parapet, t, {}, ws);
    }
    addPieceBodies(w, P, f);
    glueTouching(w, P, matC, f, true);
    return A;
}

TowerSpec towerSpecFromConfig(const Config& c) {
    TowerSpec s;
    s.frame = frameSpecFromConfig(c, "gokdelen.");
    heightsFromConfig(c, "gokdelen.", s.frame);
    s.coreT = c.f("gokdelen.core_t", float(s.coreT));
    s.coreSeg = c.f("gokdelen.core_seg", float(s.coreSeg));
    s.coreRho = c.f("gokdelen.core_rho", s.coreRho);
    s.wallExt = c.f("gokdelen.wall_ext", float(s.wallExt));
    s.blockW = c.f("gokdelen.block_w", float(s.blockW));
    s.blockH = c.f("gokdelen.block_h", float(s.blockH));
    s.densityWall = c.f("gokdelen.density_wall", s.densityWall);
    s.winW = c.f("gokdelen.window_w", float(s.winW));
    s.winH = c.f("gokdelen.window_h", float(s.winH));
    s.sill = c.f("gokdelen.sill", float(s.sill));
    s.parapet = c.f("gokdelen.parapet", float(s.parapet));
    s.lobby = c.i("gokdelen.lobby", 1) != 0;
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// the house
Building buildHouse(PhysWorld& w, const HouseSpec& sIn, const PhysMaterial& concrete, const PhysMaterial& brick) {
    HouseSpec s = sIn;
    w.clear();
    const int matC = w.addMaterial(concrete);
    s.frame.matBrick = w.addMaterial(brick);
    const FrameSpec& f = s.frame;
    Building A;
    std::vector<Piece>& P = A.pieces;
    addFramePieces(f, P);
    for (Piece& p : P) p.color = s.frameColor;
    auto X = [&](int i) { return i * f.spanX; };
    auto Z = [&](int j) { return j * f.spanZ; };
    const double W = X(f.baysX), D = Z(f.baysZ), H = f.level(f.storeys);
    A.width = W;
    A.depth = D;
    const WallStyle ws{s.blockW, s.blockH, s.paint, s.densityWall};
    for (int st = 0; st < f.storeys; ++st) {
        const double y0 = f.level(st), y1 = f.level(st + 1), top = y1 - f.beamD;
        const bool ground = st == 0;
        // front (z = 0): the door in the first bay, windows elsewhere; back and sides: windows
        for (int i = 0; i < f.baysX; ++i) {
            const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5, um = (u0 + u1) * 0.5;
            const Opening front = ground && i == 0 ? Opening{um, s.doorW, 0.0, s.doorH} : Opening{um, s.winW, s.sill, s.winH};
            addWall(P, 0, 0.0, u0, u1, y0, top, s.wallExt, {front}, ws);
            addWall(P, 0, D, u0, u1, y0, top, s.wallExt, {{um, s.winW, s.sill, s.winH}}, ws);
        }
        for (int side = 0; side < 2; ++side)
            for (int j = 0; j < f.baysZ; ++j) {
                const double u0 = Z(j) + f.colZ * 0.5, u1 = Z(j + 1) - f.colZ * 0.5;
                addWall(P, 1, side ? W : 0.0, u0, u1, y0, top, s.wallExt, {{(u0 + u1) * 0.5, s.winW, s.sill, s.winH}}, ws);
            }
        // rooms: a wall down the middle with doors, and two across
        for (int i = 1; i < f.baysX; ++i)
            for (int j = 0; j < f.baysZ; ++j) {
                const double u0 = Z(j) + f.colZ * 0.5, u1 = Z(j + 1) - f.colZ * 0.5;
                std::vector<Opening> o;
                if (j % 2 == 0) o.push_back({(u0 + u1) * 0.5, s.doorW, 0.0, s.doorH});
                addWall(P, 1, X(i), u0, u1, y0, top, s.wallInt, o, ws);
            }
        for (int j = 1; j < f.baysZ; ++j)
            for (int i = 0; i < f.baysX; ++i) {
                if ((i + j) % 2 == 0) continue;
                const double u0 = X(i) + f.colX * 0.5, u1 = X(i + 1) - f.colX * 0.5;
                addWall(P, 0, Z(j), u0, u1, y0, top, s.wallInt, {{(u0 + u1) * 0.5, s.doorW, 0.0, s.doorH}}, ws);
            }
    }
    // the roof's planes. The left slab's underside runs through the eave line (x = -e, y = H + gap) and rises at slope t
    // to the ridge over the middle; the right one mirrors it. Cuts keep dot(n, p) <= d.
    const double t = std::tan(s.pitchDeg * 3.14159265358979 / 180.0), q = std::sqrt(1.0 + t * t), e = f.beamW * 0.5;
    const double aL = H + s.eaveGap + e * t;              // left underside:  y = aL + t x
    const double aR = H + s.eaveGap + (W + e) * t;        // right underside: y = aR - t x
    const double vt = s.roofT * q;                        // the slab's thickness, measured vertically
    const double xm = 0.5 * W, ridgeUnder = aL + t * xm, ridgeTop = ridgeUnder + vt;
    const PieceCut aboveL{d3(t, -1, 0) * (1.0 / q), -aL / q}, belowL{d3(-t, 1, 0) * (1.0 / q), aL / q};
    const PieceCut topL{d3(-t, 1, 0) * (1.0 / q), (aL + vt) / q};
    const PieceCut aboveR{d3(-t, -1, 0) * (1.0 / q), -aR / q}, belowR{d3(t, 1, 0) * (1.0 / q), aR / q};
    const PieceCut topR{d3(t, 1, 0) * (1.0 / q), (aR + vt) / q}, overTopR{d3(-t, -1, 0) * (1.0 / q), -(aR + vt) / q};
    A.height = ridgeTop;
    // eave blocks: concrete wedges on the long sides' edge beams, their tops the slabs' undersides
    if (s.roof) {
        std::vector<double> zs;
        splitRange(-f.colZ * 0.5, D + f.colZ * 0.5, 2.0, zs);
        for (int side = 0; side < 2; ++side)
            for (size_t k = 0; k + 1 < zs.size(); ++k) {
                Piece b;
                b.kind = PK_BEAM;
                const double xc = side ? W : 0.0;
                b.lo = d3(xc - e, H, zs[k]);
                b.hi = d3(xc + e, H + s.eaveGap + 2.0 * e * t + 1e-3, zs[k + 1]);
                b.cuts.push_back(side ? belowR : belowL);
                b.color = s.frameColor;
                P.push_back(b);
            }
    }
    // the ridge beam's section (below): it bears on the gables' middle columns
    const double rb = 0.17, yb = ridgeUnder - 0.45;
    // gable walls at both ends, between the eave blocks, up to the slabs
    for (int end = 0; end < 2 && s.roof; ++end) addGable(P, end ? D : 0.0, e, W - e, H, xm, rb, yb, s.wallExt, t, aL, aR, belowL, belowR, ws);
    // the ridge beam (concrete, its top peaked under both slabs) over the gables and on posts over the middle column
    // line: the slabs rest on it instead of leaning on each other (measured: as an A-frame their thrust spread the eaves
    // and cracked 2174 fibres all over the house while it settled; the house without its roof: none). It bears on the
    // gables' tops: glued to their inner faces instead, its ~30 kN pulled them out of plane by 6-9 mm.
    if (s.roof && s.ridge) {
        std::vector<double> zs;
        splitRange(-s.wallExt * 0.5, D + s.wallExt * 0.5, 2.0, zs);
        for (size_t k = 0; k + 1 < zs.size(); ++k) {
            Piece b;
            b.kind = PK_BEAM;
            b.lo = d3(xm - rb, yb, zs[k]);
            b.hi = d3(xm + rb, ridgeUnder + 1e-3, zs[k + 1]);
            b.cuts = {belowL, belowR};
            b.color = s.frameColor;
            P.push_back(b);
        }
        for (int j = 1; j < f.baysZ; ++j) {   // posts on the columns under the ridge
            Piece c;
            c.kind = PK_COLUMN;
            c.lo = d3(xm - f.colX * 0.5, H, Z(j) - f.colZ * 0.5);
            c.hi = d3(xm + f.colX * 0.5, yb, Z(j) + f.colZ * 0.5);
            c.color = s.frameColor;
            P.push_back(c);
        }
    }
    // the slabs: strips along the ridge, each from past the eave to the ridge
    if (s.roof) {
        std::vector<double> zs;
        splitRange(-s.wallExt * 0.5 - s.gableOverhang, D + s.wallExt * 0.5 + s.gableOverhang, s.roofStrip, zs);
        const double xL = -e - s.overhang, xR = W + e + s.overhang;
        for (size_t k = 0; k + 1 < zs.size(); ++k)
            for (int side = 0; side < 2; ++side) {
                Piece r;
                r.kind = PK_SLAB;
                if (!side) { r.lo = d3(xL, aL + t * xL, zs[k]); r.hi = d3(xm, ridgeTop, zs[k + 1]); r.cuts = {aboveL, topL}; }
                else { r.lo = d3(xm, aR - t * xR, zs[k]); r.hi = d3(xR, ridgeTop, zs[k + 1]); r.cuts = {aboveR, topR}; }
                r.color = s.roofColor;
                r.density = s.densityRoof;
                r.unglued = 4;   // strip to strip along the ridge: touching, not glued (tiles, not a plate)
                P.push_back(r);
            }
    }
    if (s.roof && s.chimney) {   // a brick chimney through the right slope, standing on it
        Piece c;
        c.kind = PK_WALL;
        const double x0 = xm + 0.9, x1 = xm + 1.5, z0 = D * 0.62, z1 = z0 + 0.6;
        c.lo = d3(x0, aR + vt - t * x1, z0);
        c.hi = d3(x1, ridgeTop + 0.9, z1);
        c.cuts.push_back(overTopR);
        c.color = s.chimneyColor;
        c.density = 1800.0f;
        P.push_back(c);
    }
    addPieceBodies(w, P, f);
    glueTouching(w, P, matC, f, true);
    return A;
}

HouseSpec houseSpecFromConfig(const Config& c) {
    HouseSpec s;
    s.frame = frameSpecFromConfig(c, "ev.");
    heightsFromConfig(c, "ev.", s.frame);
    s.wallExt = c.f("ev.wall_ext", float(s.wallExt));
    s.wallInt = c.f("ev.wall_int", float(s.wallInt));
    s.blockW = c.f("ev.block_w", float(s.blockW));
    s.blockH = c.f("ev.block_h", float(s.blockH));
    s.densityWall = c.f("ev.density_wall", s.densityWall);
    s.winW = c.f("ev.window_w", float(s.winW));
    s.winH = c.f("ev.window_h", float(s.winH));
    s.sill = c.f("ev.sill", float(s.sill));
    s.doorW = c.f("ev.door_w", float(s.doorW));
    s.doorH = c.f("ev.door_h", float(s.doorH));
    s.pitchDeg = c.f("ev.roof_pitch", float(s.pitchDeg));
    s.roofT = c.f("ev.roof_t", float(s.roofT));
    s.overhang = c.f("ev.overhang", float(s.overhang));
    s.gableOverhang = c.f("ev.gable_overhang", float(s.gableOverhang));
    s.roofStrip = c.f("ev.roof_strip", float(s.roofStrip));
    s.eaveGap = c.f("ev.eave_gap", float(s.eaveGap));
    s.densityRoof = c.f("ev.density_roof", s.densityRoof);
    s.chimney = c.i("ev.chimney", 1) != 0;
    s.roof = c.i("ev.roof", 1) != 0;
    s.ridge = c.i("ev.ridge", 1) != 0;
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// the seismic tower
// a piece's foot bonded to the ground (body 0) over its whole base: material mat, spring length len
static void bondToGround(PhysWorld& w, const Piece& p, int mat, float len, float steelArea) {
    InterfaceDesc d;
    d.a = p.body;
    d.b = 0;
    d.n = make_float3(0, -1, 0);
    d.len = len;
    d.mat = mat;
    const double ax = (p.hi.x - p.lo.x) * 0.5, az = (p.hi.z - p.lo.z) * 0.5;
    const float3 c0 = make_float3(float((p.lo.x + p.hi.x) * 0.5), 0.0f, float((p.lo.z + p.hi.z) * 0.5));
    d.fibers = fibreGrid(c0, make_float3(1, 0, 0), make_float3(0, 0, 1), float(ax), float(az), steelArea);
    if (!d.fibers.empty()) w.addInterface(d);
}

Building buildSpireTower(PhysWorld& w, const SpireSpec& sIn, const PhysMaterial& concrete, const PhysMaterial& brick) {
    SpireSpec s = sIn;
    w.clear();
    PhysMaterial hc = concrete;
    hc.fc = s.fc;
    hc.ft = s.ft;
    hc.fy = s.fy;
    hc.E = s.E;
    const int matC = w.addMaterial(hc);
    s.frame.matBrick = w.addMaterial(brick);
    const FrameSpec& f = s.frame;
    Building A;
    std::vector<Piece>& P = A.pieces;
    const double span = f.spanX;
    const int base = s.tierBays.empty() ? 1 : s.tierBays[0];
    const double W = base * span;
    A.width = A.depth = W;
    // the mat, in panels, under the base tier and matEdge beyond it
    std::vector<double> mc;
    splitRange(-s.matEdge, W + s.matEdge, s.matPanel, mc);
    for (size_t a = 0; a + 1 < mc.size(); ++a)
        for (size_t b = 0; b + 1 < mc.size(); ++b) {
            Piece p;
            p.kind = PK_SLAB;
            p.lo = d3(mc[a], 0.0, mc[b]);
            p.hi = d3(mc[a + 1], s.matT, mc[b + 1]);
            p.density = f.densityConcrete;
            p.color = s.matColor;
            p.rho = f.rhoColumn;   // the columns' bars are anchored in it
            P.push_back(p);
        }
    const size_t matEnd = P.size();
    // the tiers: a frame each, set on the one below, its edge clad in glass from floor to beam
    const WallStyle gs{s.glassW, 100.0, s.glassColor, s.densityGlass};   // one panel a storey high
    double y = s.matT;
    int st0 = 0;
    for (size_t k = 0; k < s.tierBays.size() && k < s.tierStoreys.size(); ++k) {
        FrameSpec t = f;
        t.baysX = t.baysZ = s.tierBays[k];
        t.storeys = s.tierStoreys[k];
        t.heights.clear();
        for (int q = 0; q < t.storeys; ++q) t.heights.push_back(f.level(st0 + q + 1) - f.level(st0 + q));
        const double off = 0.5 * (base - s.tierBays[k]) * span, Wt = t.baysX * span;
        const size_t from = P.size();
        addFramePieces(t, P);
        for (size_t q = from; q < P.size(); ++q) {
            P[q].lo = P[q].lo + d3(off, y, off);
            P[q].hi = P[q].hi + d3(off, y, off);
            P[q].color = s.frameColor;
        }
        const size_t glassFrom = P.size();
        for (int q = 0; q < t.storeys; ++q) {
            const double y0 = y + t.level(q), top = y + t.level(q + 1) - t.beamD;
            for (int i = 0; i < t.baysX; ++i) {
                const double u0 = off + i * span + t.colX * 0.5, u1 = off + (i + 1) * span - t.colX * 0.5;
                addWall(P, 0, off, u0, u1, y0, top, s.glassT, {}, gs);
                addWall(P, 0, off + Wt, u0, u1, y0, top, s.glassT, {}, gs);
            }
            for (int j = 0; j < t.baysZ; ++j) {
                const double u0 = off + j * span + t.colZ * 0.5, u1 = off + (j + 1) * span - t.colZ * 0.5;
                addWall(P, 1, off, u0, u1, y0, top, s.glassT, {}, gs);
                addWall(P, 1, off + Wt, u0, u1, y0, top, s.glassT, {}, gs);
            }
        }
        for (size_t q = glassFrom; q < P.size(); ++q) P[q].glass = true;
        y += t.level(t.storeys);
        st0 += t.storeys;
    }
    A.height = y;
    // the spire, on the top tier's middle: steel, tapering to a needle
    {
        const double c = 0.5 * W;
        double yy = y;
        for (size_t k = 0; k + 1 < s.spire.size(); k += 2) {
            const double hw = 0.5 * s.spire[k], h = s.spire[k + 1];
            Piece p;
            p.kind = PK_COLUMN;
            p.lo = d3(c - hw, yy, c - hw);
            p.hi = d3(c + hw, yy + h, c + hw);
            p.rho = s.spireRho;
            p.density = s.spireDensity;
            p.color = s.spireColor;
            P.push_back(p);
            yy += h;
        }
    }
    // the curb round the moat, in pieces about the mat's panel long
    const size_t curbFrom = P.size();
    {
        const double i0 = -s.matEdge - s.moat, i1 = W + s.matEdge + s.moat, o0 = i0 - s.curbT, o1 = i1 + s.curbT;
        std::vector<double> cs;
        splitRange(o0, o1, s.matPanel, cs);
        for (size_t k = 0; k + 1 < cs.size(); ++k)
            for (int side = 0; side < 2; ++side) {
                Piece p;
                p.kind = PK_OTHER;
                p.color = s.matColor;
                p.lo = d3(cs[k], 0.0, side ? i1 : o0);
                p.hi = d3(cs[k + 1], s.curbH, side ? o1 : i0);
                P.push_back(p);
            }
        splitRange(i0, i1, s.matPanel, cs);
        for (size_t k = 0; k + 1 < cs.size(); ++k)
            for (int side = 0; side < 2; ++side) {
                Piece p;
                p.kind = PK_OTHER;
                p.color = s.matColor;
                p.lo = d3(side ? i1 : o0, 0.0, cs[k]);
                p.hi = d3(side ? o1 : i0, s.curbH, cs[k + 1]);
                P.push_back(p);
            }
    }
    const size_t curbEnd = P.size();
    addPieceBodies(w, P, f);
    glueTouching(w, P, matC, f, false);   // the frame, the mat and the curb among themselves; nothing to the ground yet
    // the isolators: rubber under every mat panel. Soft across: the stiffness that gives the tower and the mat, on
    // them, the period isoPeriod (k = m (2 pi / T)^2 over the mat's area, a spring isoLen long)
    {
        double mass = 0, area = 0;
        for (size_t q = 0; q < P.size(); ++q)
            if (q < curbFrom || q >= curbEnd) mass += w.info[P[q].body].mass;
        for (size_t q = 0; q < matEnd; ++q) area += (P[q].hi.x - P[q].lo.x) * (P[q].hi.z - P[q].lo.z);
        const double om = 2.0 * 3.14159265358979 / std::max(0.5, s.isoPeriod);
        PhysMaterial iso;
        iso.E = s.isoE;
        iso.G = float(mass * om * om * s.isoLen / std::max(area, 1.0));
        iso.ft = 1e12f;            // anchored: it lifts no more than it shortens (a tall tower rocks on it)
        iso.fc = 1e12f;
        iso.cohesion = 1e12f;      // it never slips: it shears and comes back
        iso.Es = 0.0f;
        iso.crushStart = 1e6f;
        iso.crushBreak = 1e6f;
        iso.slipBreak = 1e6f;
        iso.openBreak = 1e6f;
        iso.steelBreak = 1e6f;
        const int matIso = w.addMaterial(iso);
        for (size_t q = 0; q < matEnd; ++q) bondToGround(w, P[q], matIso, float(s.isoLen), 0.0f);
    }
    for (size_t q = curbFrom; q < curbEnd; ++q) bondToGround(w, P[q], matC, float(s.curbH * 0.5 + 0.3), 0.01f * float((P[q].hi.x - P[q].lo.x) * (P[q].hi.z - P[q].lo.z)));
    return A;
}

// a list of numbers "a b c ..." under key, or fallback
static std::vector<double> numbers(const Config& c, const std::string& key, const std::vector<double>& fallback) {
    const auto it = c.values.find(key);
    if (it == c.values.end()) return fallback;
    std::vector<double> v;
    const char* q = it->second.c_str();
    char* end = nullptr;
    for (double x = std::strtod(q, &end); end != q; x = std::strtod(q, &end)) {
        v.push_back(x);
        q = end;
        while (*q == ',' || *q == ' ') ++q;
    }
    return v.empty() ? fallback : v;
}

SpireSpec spireSpecFromConfig(const Config& c) {
    SpireSpec s;
    s.frame = frameSpecFromConfig(c, "sismik.");
    auto ints = [](const std::vector<double>& v) {
        std::vector<int> r;
        for (double x : v) r.push_back(int(x + 0.5));
        return r;
    };
    s.tierBays = ints(numbers(c, "sismik.tiers", {5, 3, 1}));
    s.tierStoreys = ints(numbers(c, "sismik.tier_storeys", {10, 10, 6}));
    int total = 0;
    for (int n : s.tierStoreys) total += n;
    s.frame.storeys = total;
    heightsFromConfig(c, "sismik.", s.frame);
    s.spire = numbers(c, "sismik.spire", s.spire);
    s.spireRho = c.f("sismik.spire_rho", s.spireRho);
    s.spireDensity = c.f("sismik.spire_density", s.spireDensity);
    s.matT = c.f("sismik.mat_t", float(s.matT));
    s.matEdge = c.f("sismik.mat_edge", float(s.matEdge));
    s.isoPeriod = c.f("sismik.iso_period", float(s.isoPeriod));
    s.isoLen = c.f("sismik.iso_len", float(s.isoLen));
    s.isoE = c.f("sismik.iso_e", s.isoE);
    s.moat = c.f("sismik.moat", float(s.moat));
    s.glassW = c.f("sismik.glass_w", float(s.glassW));
    s.glassT = c.f("sismik.glass_t", float(s.glassT));
    s.densityGlass = c.f("sismik.density_glass", s.densityGlass);
    s.fc = c.f("sismik.fc", s.fc);
    s.ft = c.f("sismik.ft", s.ft);
    s.fy = c.f("sismik.fy", s.fy);
    s.E = c.f("sismik.e", s.E);
    return s;
}
