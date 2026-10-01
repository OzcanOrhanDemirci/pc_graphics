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

// a wall: axis 0 runs along x at z = c, axis 1 runs along z at x = c; from u0 to u1, y0 to y1, thickness t
void addWall(std::vector<Piece>& P, int axis, double c, double u0, double u1, double y0, double y1, double t, const std::vector<Opening>& open,
             const ApartmentSpec& s) {
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

Apartment buildApartment(PhysWorld& w, const ApartmentSpec& sIn, const PhysMaterial& concrete, const PhysMaterial& brick) {
    ApartmentSpec s = sIn;
    w.clear();
    const int matC = w.addMaterial(concrete);
    s.frame.matBrick = w.addMaterial(brick);
    const FrameSpec& f = s.frame;
    Apartment A;
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
