// Scene 9 "Ayna": the maze (see ayna_maze.h).
#include "scenes/ayna/ayna_maze.h"
#include "core/image_io.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ayna {

namespace {

struct Rng {
    unsigned s;
    unsigned next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return float(next() >> 8) * (1.0f / 16777216.0f); }
};

int hexDistance(int i, int j) { return std::max({std::abs(i), std::abs(j), std::abs(i + j)}); }

float len2(float2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

}   // namespace

void Maze::build(const Lattice& lattice, int R, unsigned seed, int loops, int layout) {
    g = lattice;
    g.h = g.L * kSqrt3Half;
    radius = R;
    g.i0 = -R - 1;
    g.j0 = -R - 1;
    g.ni = 2 * R + 2;
    g.nj = 2 * R + 2;
    edges.assign(edgeCount(g), kEdgeOut);
    cells.assign(cellCount(g), 0);
    columns.assign(vertexCount(g), 0);

    // the hexagon: a cell is inside if its three corners are
    for (int j = g.j0; j < g.j0 + g.nj; ++j)
        for (int i = g.i0; i < g.i0 + g.ni; ++i)
            for (int up = 0; up < 2; ++up) {
                int ci[3], cj[3];
                cellCornerIdx(i, j, up, ci, cj);
                bool in = true;
                for (int k = 0; k < 3; ++k) in = in && hexDistance(ci[k], cj[k]) <= R;
                if (!in) continue;
                cells[cellIndex(g, i, j, up)] = 1;
                for (int k = 0; k < 3; ++k) {
                    const EdgeRef e = cellEdge(i, j, up, k);
                    edges[edgeIndex(g, e.fam, e.i, e.j)] = kEdgeMirror;   // every window a mirror until the paths are cut
                    columns[vertexIndex(g, ci[k], cj[k])] = 1;
                }
            }

    // the hall in the middle: the six cells round V(0,0), open to each other
    struct C {
        int i, j, up;
    };
    const C hall[6] = {{0, 0, 1}, {-1, 0, 1}, {0, -1, 1}, {-1, 0, 0}, {-1, -1, 0}, {0, -1, 0}};
    auto visitedIdx = [&](int i, int j, int up) { return cellIndex(g, i, j, up); };
    std::vector<unsigned char> visited(cells.size(), 0);
    for (const C& c : hall) visited[visitedIdx(c.i, c.j, c.up)] = 1;
    for (const C& c : hall)
        for (int k = 0; k < 3; ++k) {
            int ni, nj, nup;
            cellAcross(c.i, c.j, c.up, k, ni, nj, nup);
            for (const C& d : hall)
                if (d.i == ni && d.j == nj && d.up == nup) {
                    const EdgeRef e = cellEdge(c.i, c.j, c.up, k);
                    edges[edgeIndex(g, e.fam, e.i, e.j)] = kEdgeOpen;
                }
        }

    // two long straight corridors (Özcan: "yansımanın yansımasının yansıması"): the strips j = 1 and j = -2, open
    // along their length, their two walls parallel mirrors; doors to the hall and to the outer strip beyond
    std::vector<unsigned char> locked(edges.size(), 0);
    auto openEdge = [&](int fam, int i, int j) { edges[edgeIndex(g, fam, i, j)] = kEdgeOpen; };
    if (layout == 1) {
        for (int strip : {1, -2}) {
            for (int i = g.i0; i < g.i0 + g.ni; ++i) {
                for (int up = 1; up >= 0; --up) {
                    if (!inside(i, strip, up)) continue;
                    visited[cellIndex(g, i, strip, up)] = 1;
                    // walls: the cell's family-0 edge; along: the other two, where the next cell is inside
                    const EdgeRef w = cellEdge(i, strip, up, 0);
                    locked[edgeIndex(g, w.fam, w.i, w.j)] = 1;
                    for (int k = 1; k < 3; ++k) {
                        int ni, nj, nup;
                        cellAcross(i, strip, up, k, ni, nj, nup);
                        if (!inside(ni, nj, nup)) continue;
                        const EdgeRef e = cellEdge(i, strip, up, k);
                        openEdge(e.fam, e.i, e.j);
                    }
                }
            }
        }
        // the doors: to the hall, and on to the outer strips
        openEdge(0, -1, 1);
        openEdge(0, 0, 2);
        openEdge(0, 0, -1);
        openEdge(0, -1, -2);
    }

    // the paths: a depth-first walk from the hall that likes to go on straight (a straight run on this lattice is a
    // strip between two parallel lines, crossed by alternating edge families: its two walls are parallel mirrors)
    Rng rng{seed * 2654435761u + 12345u};
    struct S {
        int i, j, up, fam1, fam2;   // the families of the last two edges crossed
    };
    std::vector<S> stack;
    for (int h = 0; h < 6; ++h) stack.push_back({hall[h].i, hall[h].j, hall[h].up, -1, -1});
    // the corridors' cells and the outer strips' first cells beyond the doors grow the walk too
    for (int j = g.j0; j < g.j0 + g.nj; ++j)
        for (int i = g.i0; i < g.i0 + g.ni; ++i)
            for (int up = 0; up < 2; ++up) {
                const int ci = cellIndex(g, i, j, up);
                if (ci >= 0 && visited[ci] && (j == 1 || j == -2)) stack.push_back({i, j, up, -1, -1});
            }
    std::reverse(stack.begin(), stack.end());
    while (!stack.empty()) {
        const S cur = stack.back();
        int opts[3], n = 0;
        for (int k = 0; k < 3; ++k) {
            int ni, nj, nup;
            cellAcross(cur.i, cur.j, cur.up, k, ni, nj, nup);
            const int ci = cellIndex(g, ni, nj, nup);
            const EdgeRef e = cellEdge(cur.i, cur.j, cur.up, k);
            if (ci >= 0 && cells[ci] && !visited[ci] && !locked[edgeIndex(g, e.fam, e.i, e.j)]) opts[n++] = k;
        }
        if (n == 0) {
            stack.pop_back();
            continue;
        }
        int pick = opts[rng.next() % n];
        for (int q = 0; q < n; ++q)
            if (cellEdge(cur.i, cur.j, cur.up, opts[q]).fam == cur.fam2 && rng.f() < 0.72f) pick = opts[q];
        const EdgeRef e = cellEdge(cur.i, cur.j, cur.up, pick);
        edges[edgeIndex(g, e.fam, e.i, e.j)] = kEdgeOpen;
        int ni, nj, nup;
        cellAcross(cur.i, cur.j, cur.up, pick, ni, nj, nup);
        visited[cellIndex(g, ni, nj, nup)] = 1;
        stack.push_back({ni, nj, nup, e.fam, cur.fam1});
    }

    // a few loops: interior mirrors opened
    std::vector<int> interior;
    for (int fam = 0; fam < 3; ++fam)
        for (int j = g.j0; j <= g.j0 + g.nj; ++j)
            for (int i = g.i0; i <= g.i0 + g.ni; ++i) {
                const int k = edgeIndex(g, fam, i, j);
                if (k < 0 || edges[k] != kEdgeMirror) continue;
                // both sides inside?
                int pi = i, pj = j, pu, mi = i, mj = j, mu;
                if (fam == 0) { pu = 1; mj = j - 1; mu = 0; }
                else if (fam == 1) { pu = 1; mi = i - 1; mu = 0; }
                else { pu = 0; mu = 1; }
                if (inside(pi, pj, pu) && inside(mi, mj, mu) && !locked[k]) interior.push_back(k);
            }
    for (int l = 0; l < loops && !interior.empty(); ++l) {
        const int q = int(rng.next() % interior.size());
        edges[interior[q]] = kEdgeOpen;
        interior.erase(interior.begin() + q);
    }
}

float2 Maze::cellCentre(int i, int j, int up) const {
    float2 c[3];
    cellCorners(g, i, j, up, c);
    return make_float2((c[0].x + c[1].x + c[2].x) / 3.0f, (c[0].y + c[1].y + c[2].y) / 3.0f);
}

float Maze::windowDistance(float du, float y) const {
    // the jambs' box from the floor to their top, and the arch's disk above the jambs' top
    const float qx = std::fabs(du) - g.wHalf, qy = std::fabs(y - 0.5f * g.yk) - 0.5f * g.yk;
    const float box = std::sqrt(sqr(std::max(qx, 0.0f)) + sqr(std::max(qy, 0.0f))) + std::min(std::max(qx, qy), 0.0f);
    const float disk = std::max(std::sqrt(du * du + sqr(y - g.yc)) - g.ra, g.yk - y);
    return std::min(box, disk);
}

int Maze::mirrorCount() const { return int(std::count(edges.begin(), edges.end(), (unsigned char)kEdgeMirror)); }
int Maze::openCount() const { return int(std::count(edges.begin(), edges.end(), (unsigned char)kEdgeOpen)); }

bool Maze::collide(float3& p, float r) const {
    // the distance from p to the nearest solid: columns, the slabs outside their windows, a mirror's glass
    auto solid = [&](float3 q) {
        float d = std::min(q.y, g.Hc - q.y);
        int i, j, up;
        locate(g, q.x, q.z, i, j, up);
        // this cell and its three neighbours: their edges and corners
        int cs[4][3] = {{i, j, up}};
        for (int k = 0; k < 3; ++k) cellAcross(i, j, up, k, cs[k + 1][0], cs[k + 1][1], cs[k + 1][2]);
        for (int c = 0; c < 4; ++c) {
            int ci[3], cj[3];
            cellCornerIdx(cs[c][0], cs[c][1], cs[c][2], ci, cj);
            for (int k = 0; k < 3; ++k) {
                const int vi = vertexIndex(g, ci[k], cj[k]);
                if (vi >= 0 && columns[vi]) {
                    const float2 v = vtx(g, ci[k], cj[k]);
                    d = std::min(d, len2(make_float2(q.x - v.x, q.z - v.y)) - g.rc);
                }
                const EdgeRef e = cellEdge(cs[c][0], cs[c][1], cs[c][2], k);
                const int type = edge(e.fam, e.i, e.j);
                if (type == kEdgeOut) continue;
                float2 A, B;
                edgeEnds(g, e.fam, e.i, e.j, A, B);
                const float2 ed = famDir(e.fam), n = famNormal(e.fam);
                const float u = (q.x - A.x) * ed.x + (q.z - A.y) * ed.y, v = (q.x - A.x) * n.x + (q.z - A.y) * n.y;
                if (u < -0.05f || u > g.L + 0.05f) continue;
                const float dw = windowDistance(u - 0.5f * g.L, q.y), av = std::fabs(v);
                float ds;
                if (dw >= 0.0f) ds = av - 0.5f * g.t;   // the slab is solid here (negative: inside it)
                else if (av <= 0.5f * g.t) ds = -dw;    // in the window's reveal: its rim
                else ds = std::sqrt(sqr(av - 0.5f * g.t) + dw * dw);
                if (type == kEdgeMirror && dw < 0.0f) ds = std::min(ds, av - g.glass);
                d = std::min(d, ds);
            }
        }
        return d;
    };
    bool touched = false;
    for (int it = 0; it < 8; ++it) {
        const float d = solid(p);
        if (d >= r) break;
        touched = true;
        const float e = 0.002f;
        float3 grad = make_float3(solid(make_float3(p.x + e, p.y, p.z)) - solid(make_float3(p.x - e, p.y, p.z)),
                                  solid(make_float3(p.x, p.y + e, p.z)) - solid(make_float3(p.x, p.y - e, p.z)),
                                  solid(make_float3(p.x, p.y, p.z + e)) - solid(make_float3(p.x, p.y, p.z - e)));
        const float gl = length(grad);
        if (gl < 1e-6f) break;
        p += grad * ((r - d + 0.0005f) / gl);
    }
    return touched;
}

bool Maze::savePlan(const std::string& path, int ppm) const {
    const float x0 = (-float(radius) - 0.6f) * g.L, x1 = (float(radius) + 0.6f) * g.L;
    const float z0 = (-float(radius) - 0.6f) * g.h, z1 = (float(radius) + 0.6f) * g.h;
    const int W = int((x1 - x0) * ppm), H = int((z1 - z0) * ppm);
    std::vector<unsigned char> px(size_t(W) * H * 4, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float px_ = x0 + (x + 0.5f) / ppm, pz = z1 - (y + 0.5f) / ppm;
            int i, j, up;
            locate(g, px_, pz, i, j, up);
            unsigned char c[3] = {12, 12, 14};
            if (inside(i, j, up)) {
                c[0] = 46; c[1] = 44; c[2] = 40;
                for (int k = 0; k < 3; ++k) {
                    const EdgeRef e = cellEdge(i, j, up, k);
                    float2 A, B;
                    edgeEnds(g, e.fam, e.i, e.j, A, B);
                    const float2 n = famNormal(e.fam);
                    const float v = std::fabs((px_ - A.x) * n.x + (pz - A.y) * n.y);
                    const int type = edge(e.fam, e.i, e.j);
                    if (v < 0.5f * g.t) {
                        if (type == kEdgeMirror) { c[0] = 120; c[1] = 220; c[2] = 230; }
                        else { c[0] = 90; c[1] = 70; c[2] = 50; }
                    }
                }
                float2 cc[3];
                cellCorners(g, i, j, up, cc);
                for (int k = 0; k < 3; ++k)
                    if (len2(make_float2(px_ - cc[k].x, pz - cc[k].y)) < g.rc) { c[0] = 230; c[1] = 220; c[2] = 200; }
            }
            unsigned char* o = &px[(size_t(y) * W + x) * 4];
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255;
        }
    return writePNG(path, W, H, px.data());
}

}   // namespace ayna
