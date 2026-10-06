#include "scenes/denizalti/dz_geom.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace dz {

namespace {
const float kPi = 3.14159265358979f;
float3 f3(float x, float y, float z) { return make_float3(x, y, z); }
}   // namespace

// ---- placements
Xform Xform::operator*(const Xform& b) const {
    Xform r;
    r.o = point(b.o);
    r.x = dir(b.x);
    r.y = dir(b.y);
    r.z = dir(b.z);
    return r;
}
Xform Xform::rotX(float a) {
    Xform t;
    t.y = f3(0, std::cos(a), std::sin(a));
    t.z = f3(0, -std::sin(a), std::cos(a));
    return t;
}
Xform Xform::rotY(float a) {
    Xform t;
    t.x = f3(std::cos(a), 0, -std::sin(a));
    t.z = f3(std::sin(a), 0, std::cos(a));
    return t;
}
Xform Xform::rotZ(float a) {
    Xform t;
    t.x = f3(std::cos(a), std::sin(a), 0);
    t.y = f3(-std::sin(a), std::cos(a), 0);
    return t;
}
Xform Xform::frame(float3 o, float3 z, float3 up) {
    Xform t;
    t.o = o;
    t.z = normalize(z);
    float3 x = cross(up, t.z);
    if (dot(x, x) < 1e-8f) x = cross(f3(1, 0, 0), t.z);
    if (dot(x, x) < 1e-8f) x = cross(f3(0, 0, 1), t.z);
    t.x = normalize(x);
    t.y = cross(t.z, t.x);
    return t;
}

void Mesh::append(const Mesh& m, const Xform& t) {
    const uint32_t base = uint32_t(v.size());
    v.reserve(v.size() + m.v.size());
    for (Vtx a : m.v) {
        a.p = t.point(a.p);
        a.n = normalize(t.dir(a.n));
        a.dn = normalize(t.dir(a.dn));
        const float3 tt = normalize(t.dir(make_float3(a.t.x, a.t.y, a.t.z)));
        a.t = make_float4(tt.x, tt.y, tt.z, a.t.w);
        a.chart += charts;
        v.push_back(a);
    }
    for (uint32_t i : m.idx) idx.push_back(base + i);
    charts += m.charts;
}

// ---- the kit
int Kit::segs(float length, int lo) const { return std::max(lo, int(std::ceil(std::fabs(length) / maxEdge - 1e-4f))); }
int Kit::arcSegs(float r, float angle, int lo) const {
    // the sagitta r (1 - cos(phi / 2)) ~ r phi^2 / 8 <= 5e-5 m: phi <= sqrt(4e-4 / r)
    const float a = std::fabs(angle);
    return std::max(segs(r * a, lo), int(std::ceil(a * std::sqrt(std::max(r, 0.0f) / 4e-4f) - 1e-4f)));
}

void Kit::tube(float r, float z0, float z1, float uOffset) {
    const float C = 2.0f * kPi * r;
    const int nu = std::max(16, segs(C)), nv = segs(z1 - z0);   // radial normals all round: Phong rounds it
    const float2 keep = fp;
    if (fp.y == 0.0f) fp.y = C;   // a closed surface's features need its circumference
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float a = 2.0f * kPi * float(i % nu) / nu;   // the seam's last column is the first's position exactly
        const float z = z0 + (z1 - z0) * float(j) / nv;
        g.n = f3(std::cos(a), std::sin(a), 0);
        g.p = f3(r * g.n.x, r * g.n.y, z);
        g.s = make_float2(C * float(i) / nu + uOffset, z);
        return g;
    });
    fp = keep;
}

void Kit::bend(float r, float R, float a, float vOffset) {
    const float C = 2.0f * kPi * r;
    const int nu = std::max(16, segs(C)), nv = std::max(4, segs(R * a));
    const float2 keep = fp;
    if (fp.y == 0.0f) fp.y = C;
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float th = 2.0f * kPi * float(i % nu) / nu;
        const float b = a * float(j) / nv;
        // the centre line: starts at origin going +z, turns towards +x about the point (R, 0, 0)
        const float3 c = f3(R - R * std::cos(b), 0, R * std::sin(b));
        const float3 ez = f3(std::sin(b), 0, std::cos(b));   // along the line
        const float3 ex = f3(std::cos(b), 0, -std::sin(b));   // towards... the outside of the bend is -ex
        const float3 ey = f3(0, 1, 0);
        g.n = ex * std::cos(th) + ey * std::sin(th);
        g.p = c + g.n * r;
        g.s = make_float2(C * float(i) / nu, vOffset + R * b);
        (void)ez;
        return g;
    });
    fp = keep;
}

void Kit::ring(float r0, float r1, float z, bool up) {
    const int nu = arcSegs(r1, 2.0f * kPi, 16), nv = segs(r1 - r0);
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float a = 2.0f * kPi * float(i % nu) / nu * (up ? -1.0f : 1.0f);   // the winding faces +z when up
        const float r = r0 + (r1 - r0) * float(j) / nv;
        g.p = f3(r * std::cos(a), r * std::sin(a), z);
        g.n = f3(0, 0, up ? 1.0f : -1.0f);
        g.s = make_float2(g.p.x, g.p.y);
        return g;
    });
}

void Kit::roundedCylinder(float r, float z0, float z1, float e, bool cap0, bool cap1) {
    e = std::min(e, std::min(r * 0.5f, 0.5f * (z1 - z0)));
    const float C = 2.0f * kPi * r;
    const int nu = arcSegs(r, 2.0f * kPi, 20);
    const int nb = std::max(3, int(std::ceil(0.5f * kPi * e / (0.25f * maxEdge))));   // rounding: fine (it catches the light)
    // one grid over the profile: bottom cap centre -> rounding -> side -> rounding -> top cap centre
    struct P { float rr, z, nr, nz, s, edge, fm = 0.0f; };
    std::vector<P> prof;
    auto capRows = [&](bool top) {
        const int nc = std::max(1, segs(r - e));
        std::vector<P> rows;
        for (int k = 0; k <= nc; ++k) {
            const float rr = (r - e) * float(k) / nc;
            rows.push_back({rr, top ? z1 : z0, 0.0f, top ? 1.0f : -1.0f, 0, 0});
        }
        return rows;
    };
    if (cap0) {
        auto rows = capRows(false);
        prof.insert(prof.end(), rows.begin(), rows.end());
    }
    for (int k = cap0 ? 1 : 0; k <= nb; ++k) {   // bottom rounding: from facing -z to facing out
        const float th = 0.5f * kPi * float(k) / nb;
        prof.push_back({r - e + e * std::sin(th), z0 + e - e * std::cos(th), std::sin(th), -std::cos(th), 0, std::sin(2.0f * th)});
    }
    const int ns = segs(z1 - z0 - 2.0f * e);
    for (int k = 1; k < ns; ++k) prof.push_back({r, z0 + e + (z1 - z0 - 2.0f * e) * float(k) / ns, 1, 0, 0, 0, 1.0f});
    for (int k = 0; k <= nb; ++k) {   // top rounding
        const float th = 0.5f * kPi * float(k) / nb;
        prof.push_back({r - e + e * std::cos(th), z1 - e + e * std::sin(th), std::cos(th), std::sin(th), 0, std::sin(2.0f * th)});
    }
    if (cap1) {
        auto rows = capRows(true);
        std::reverse(rows.begin(), rows.end());
        prof.insert(prof.end(), rows.begin() + 1, rows.end());
    }
    // the surface coordinate along the profile: its arc length
    for (size_t k = 1; k < prof.size(); ++k)
        prof[k].s = prof[k - 1].s + std::hypot(prof[k].rr - prof[k - 1].rr, prof[k].z - prof[k - 1].z);
    const float2 keep = fp;
    if (fp.y == 0.0f) fp.y = C;
    grid(nu, int(prof.size()) - 1, [&](int i, int j) {
        GridPt g;
        const float a = 2.0f * kPi * float(i % nu) / nu;
        const P& q = prof[j];
        const float ca = std::cos(a), sa = std::sin(a);
        g.p = f3(q.rr * ca, q.rr * sa, q.z);
        g.n = normalize(f3(q.nr * ca, q.nr * sa, q.nz));
        g.s = make_float2(C * float(i) / nu, q.s);
        g.edge = q.edge;
        g.fmask = q.fm;
        return g;
    });
    fp = keep;
}

void Kit::hexPrism(float s, float z0, float h, float e) {
    // the outline: a hexagon across flats s, corners rounded; swept with rounded top and bottom edges
    const float R = s / std::sqrt(3.0f);   // across corners / 2
    std::vector<float2> hex;
    for (int k = 0; k < 6; ++k) {
        const float a = kPi / 6.0f + k * kPi / 3.0f;
        hex.push_back(make_float2(R * std::cos(a), R * std::sin(a)));
    }
    const std::vector<float2> out = roundOutline(hex, std::min(e * 1.5f, 0.15f * s), 3);
    // a chamfered top (as nuts have): a profile of insets, each a scaled copy of the outline
    struct Row { float scale, z, nz, edge; };
    const float c = std::min(e, 0.25f * h);
    std::vector<Row> rows = {{1.0f - 2.0f * c / s, z0, -1, 0.3f}, {1.0f, z0 + c, 0, 1.0f}, {1.0f, z0 + h - c, 0, 1.0f},
                             {1.0f - 2.0f * c / s, z0 + h, 1, 0.3f}};
    const int n = int(out.size());
    // side: rows of the outline
    {
        float2 centroid = make_float2(0, 0);
        std::vector<float> arc(n + 1, 0.0f);
        for (int k = 1; k <= n; ++k) arc[k] = arc[k - 1] + std::hypot(out[k % n].x - out[k - 1].x, out[k % n].y - out[k - 1].y);
        grid(n, int(rows.size()) - 1, [&](int i, int j) {
            GridPt g;
            const float2 q = out[i % n];
            const float2 qp = out[(i + 1) % n], qm = out[(i + n - 1) % n];
            float2 tn = make_float2(qp.x - qm.x, qp.y - qm.y);
            const float tl = std::hypot(tn.x, tn.y);
            const float3 side = f3(tn.y / tl, -tn.x / tl, 0);   // outward for a counter-clockwise outline
            const Row& r = rows[j];
            g.p = f3(centroid.x + q.x * r.scale, centroid.y + q.y * r.scale, r.z);
            g.n = normalize(side * (1.0f - std::fabs(r.nz) * 0.6f) + f3(0, 0, r.nz * 0.8f));
            g.s = make_float2(arc[i], r.z);
            g.edge = r.edge;
            return g;
        });
    }
    // caps: fans as grids (centre to the inset outline)
    for (int top = 0; top < 2; ++top) {
        const Row& r = rows[top ? 3 : 0];
        grid(n, 1, [&](int i, int j) {
            GridPt g;
            const float2 q = out[(top ? n - i : i) % n];   // the winding faces out
            const float k = j ? r.scale : 0.0f;
            g.p = f3(q.x * k, q.y * k, r.z);
            g.n = f3(0, 0, top ? 1.0f : -1.0f);
            g.s = make_float2(g.p.x, g.p.y);
            return g;
        });
    }
}

void Kit::roundedBox(float3 h, float e) {
    e = std::min(e, 0.49f * std::min(h.x, std::min(h.y, h.z)));
    const int nb = std::max(2, int(std::ceil(0.25f * kPi * e / (0.25f * maxEdge))));
    // coordinates along an axis of half size hh: the rounding's 45 degrees at each end, flat between
    auto axis = [&](float hh) {
        std::vector<float> c;
        for (int k = nb; k >= 1; --k) c.push_back(-(hh - e) - e * std::tan(0.25f * kPi * float(k) / nb));
        const int nf = std::max(1, segs(2.0f * (hh - e)));
        for (int k = 0; k <= nf; ++k) c.push_back(-(hh - e) + 2.0f * (hh - e) * float(k) / nf);
        for (int k = 1; k <= nb; ++k) c.push_back((hh - e) + e * std::tan(0.25f * kPi * float(k) / nb));
        return c;
    };
    const float hs[3] = {h.x, h.y, h.z};
    for (int f = 0; f < 6; ++f) {
        const int ax = f / 2;                 // the face's normal axis
        const float sgn = (f % 2) ? 1.0f : -1.0f;
        const int ua = (ax + 1) % 3, va = (ax + 2) % 3;
        std::vector<float> cu = axis(hs[ua]), cv = axis(hs[va]);
        if (sgn < 0) std::reverse(cu.begin(), cu.end());   // keep the winding outward
        grid(int(cu.size()) - 1, int(cv.size()) - 1, [&](int i, int j) {
            GridPt g;
            float c[3];
            c[ax] = sgn * hs[ax];
            c[ua] = cu[i];
            c[va] = cv[j];
            float in[3], d[3];
            for (int k = 0; k < 3; ++k) {
                in[k] = std::max(-(hs[k] - e), std::min(hs[k] - e, c[k]));
                d[k] = c[k] - in[k];
            }
            // on the face d = (0..,e,..): the rounding's direction
            float3 dv = f3(d[0], d[1], d[2]);
            const float dl = length(dv);
            const float3 n = dl > 1e-9f ? dv / dl : f3(ax == 0 ? sgn : 0, ax == 1 ? sgn : 0, ax == 2 ? sgn : 0);
            g.n = n;
            g.p = f3(in[0], in[1], in[2]) + n * e;
            g.s = make_float2(c[ua], c[va]);
            float3 fn = f3(ax == 0 ? sgn : 0, ax == 1 ? sgn : 0, ax == 2 ? sgn : 0);
            g.edge = smoothstepf(0.0f, 0.25f, 1.0f - dot(n, fn));
            return g;
        });
    }
}

void Kit::torus(float R, float r, float a0, float a1) {
    const float C = 2.0f * kPi * r;
    const int nu = std::max(16, segs(C)), nv = std::max(12, segs(R * (a1 - a0)));
    const bool closed = std::fabs(a1 - a0 - 2.0f * kPi) < 1e-4f;
    const float2 keep = fp;
    if (fp.y == 0.0f) fp.y = C;
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float th = -2.0f * kPi * float(i % nu) / nu;   // the winding faces out
        const float b = closed ? a0 + 2.0f * kPi * float(j % nv) / nv : a0 + (a1 - a0) * float(j) / nv;
        const float3 rad = f3(std::cos(b), std::sin(b), 0);
        g.n = rad * std::cos(th) + f3(0, 0, std::sin(th));
        g.p = rad * R + g.n * r;
        g.s = make_float2(C * float(i) / nu, R * (a1 - a0) * float(j) / nv);   // past the seam, never back to 0
        return g;
    });
    fp = keep;
}

void Kit::dome(float r, float hb) {
    // a spherical cap of base radius r and height hb, sitting on z = 0
    const float Rs = (r * r + hb * hb) / (2.0f * hb);   // the sphere's radius
    const float amax = std::asin(std::min(1.0f, r / Rs));
    const int nu = arcSegs(r, 2.0f * kPi, 12), nv = std::max(4, arcSegs(Rs, amax) * 2);
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float a = 2.0f * kPi * float(i % nu) / nu;
        const float b = amax * (1.0f - float(j) / nv);   // from the rim to the top
        g.n = f3(std::sin(b) * std::cos(a), std::sin(b) * std::sin(a), std::cos(b));
        g.p = f3(0, 0, hb - Rs) + g.n * Rs;
        g.s = make_float2(g.p.x, g.p.y);
        return g;
    });
}

void Kit::sphere(float r) {
    const int nu = std::max(16, segs(2.0f * kPi * r)), nv = std::max(8, segs(kPi * r));
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const float a = 2.0f * kPi * float(i % nu) / nu;
        const float b = kPi * float(j) / nv;
        g.n = f3(std::sin(b) * std::cos(a), std::sin(b) * std::sin(a), -std::cos(b));
        g.p = g.n * r;
        g.s = make_float2(2.0f * kPi * r * float(i) / nu, kPi * r * float(j) / nv);
        return g;
    });
}

void Kit::cylinderWall(float r, float a0, float a1, float z0, float z1, bool inside) {
    const int nu = std::max(2, segs(r * (a1 - a0))), nv = segs(z1 - z0);
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        const int ii = inside ? nu - i : i;   // the winding faces the normal
        const float a = a0 + (a1 - a0) * float(ii) / nu;
        const float z = z0 + (z1 - z0) * float(j) / nv;
        const float3 rad = f3(std::cos(a), std::sin(a), 0);
        g.p = rad * r + f3(0, 0, z);
        g.n = inside ? -rad : rad;
        g.s = make_float2(r * (a - a0), z);
        return g;
    });
}

void Kit::quad(float w, float h) {
    const int nu = segs(w), nv = segs(h);
    grid(nu, nv, [&](int i, int j) {
        GridPt g;
        g.p = f3(-0.5f * w + w * float(i) / nu, -0.5f * h + h * float(j) / nv, 0);
        g.n = f3(0, 0, 1);
        g.s = make_float2(g.p.x, g.p.y);
        return g;
    });
}

std::vector<float2> Kit::roundOutline(const std::vector<float2>& poly, float e, int n) {
    std::vector<float2> out;
    const int m = int(poly.size());
    for (int k = 0; k < m; ++k) {
        const float2 p = poly[k], a = poly[(k + m - 1) % m], b = poly[(k + 1) % m];
        float2 da = make_float2(a.x - p.x, a.y - p.y), db = make_float2(b.x - p.x, b.y - p.y);
        const float la = std::hypot(da.x, da.y), lb = std::hypot(db.x, db.y);
        da = make_float2(da.x / la, da.y / la);
        db = make_float2(db.x / lb, db.y / lb);
        const float cosT = std::max(-0.9999f, std::min(0.9999f, da.x * db.x + da.y * db.y));
        const float half = 0.5f * std::acos(cosT);   // half the corner's inner angle
        const float ee = std::min(e, 0.45f * std::min(la, lb) * std::tan(half));
        const float dcut = ee / std::tan(half);        // how far from the corner the rounding starts
        float2 bis = make_float2(da.x + db.x, da.y + db.y);
        const float bl = std::hypot(bis.x, bis.y);
        bis = make_float2(bis.x / bl, bis.y / bl);
        const float2 c = make_float2(p.x + bis.x * ee / std::sin(half), p.y + bis.y * ee / std::sin(half));
        const float2 s0 = make_float2(p.x + da.x * dcut, p.y + da.y * dcut), s1 = make_float2(p.x + db.x * dcut, p.y + db.y * dcut);
        float a0 = std::atan2(s0.y - c.y, s0.x - c.x), a1 = std::atan2(s1.y - c.y, s1.x - c.x);
        // go the short way round
        float da1 = a1 - a0;
        while (da1 > kPi) da1 -= 2.0f * kPi;
        while (da1 < -kPi) da1 += 2.0f * kPi;
        for (int i = 0; i <= n; ++i) {
            const float a = a0 + da1 * float(i) / n;
            out.push_back(make_float2(c.x + ee * std::cos(a), c.y + ee * std::sin(a)));
        }
    }
    return out;
}

void Kit::extrude(const std::vector<float2>& outline, float z0, float z1, bool caps) {
    const int n = int(outline.size());
    std::vector<float> arc(n + 1, 0.0f);
    for (int k = 1; k <= n; ++k) arc[k] = arc[k - 1] + std::hypot(outline[k % n].x - outline[k - 1].x, outline[k % n].y - outline[k - 1].y);
    // normals: the neighbouring edge normals weighted by their lengths (rounded corners come out smooth, straight runs
    // flat: where an arc meets a straight its end takes the straight's normal, so the run does not bow under Phong)
    std::vector<float3> nrm(n);
    for (int k = 0; k < n; ++k) {
        const float2 a = outline[(k + n - 1) % n], p = outline[k], b = outline[(k + 1) % n];
        nrm[k] = normalize(f3(p.y - a.y, -(p.x - a.x), 0) + f3(b.y - p.y, -(b.x - p.x), 0));
    }
    // split the side into strips at sharp corners would need vertex duplication; the outline is rounded, so one grid
    const int nv = segs(z1 - z0);
    grid(n, nv, [&](int i, int j) {
        GridPt g;
        const float2 q = outline[i % n];
        g.p = f3(q.x, q.y, z0 + (z1 - z0) * float(j) / nv);
        g.n = nrm[i % n];
        g.s = make_float2(arc[i], g.p.z);
        return g;
    });
    if (!caps) return;
    // caps: a fan from the outline's centroid (convex enough for the profiles used: T, L, plates)
    float2 cen = make_float2(0, 0);
    for (const float2& q : outline) cen = make_float2(cen.x + q.x / n, cen.y + q.y / n);
    for (int top = 0; top < 2; ++top) {
        const float z = top ? z1 : z0;
        grid(n, 1, [&](int i, int j) {
            GridPt g;
            const float2 q = outline[(top ? n - i : i) % n];   // the winding faces out
            g.p = j ? f3(q.x, q.y, z) : f3(cen.x, cen.y, z);
            g.n = f3(0, 0, top ? 1.0f : -1.0f);
            g.s = make_float2(g.p.x, g.p.y);
            return g;
        });
    }
}

void Kit::lathe(const std::vector<float2>& rz0, float a0, float a1, const std::vector<float>& edgeW0) {
    if (rz0.size() < 2) return;
    // a sharp corner in the profile becomes a small round (0.6 mm, at most a quarter of the shorter side): one normal
    // at a corner made Phong bulge both faces (a lamp's flat collar dished down into its globe). No edge is a knife's.
    std::vector<float2> rz;
    std::vector<float> edgeW;
    for (size_t k = 0; k < rz0.size(); ++k) {
        const float w = edgeW0.empty() ? 0.0f : edgeW0[k];
        if (k > 0 && k + 1 < rz0.size()) {
            const float2 a = rz0[k - 1], p = rz0[k], b = rz0[k + 1];
            const float2 d0 = make_float2(p.x - a.x, p.y - a.y), d1 = make_float2(b.x - p.x, b.y - p.y);
            const float l0 = std::hypot(d0.x, d0.y), l1 = std::hypot(d1.x, d1.y);
            if (l0 > 1e-6f && l1 > 1e-6f && (d0.x * d1.x + d0.y * d1.y) / (l0 * l1) < 0.906f) {   // turns over 25 degrees
                const float f = std::min(0.0006f, 0.25f * std::min(l0, l1));
                const float2 s0 = make_float2(p.x - d0.x / l0 * f, p.y - d0.y / l0 * f), s1 = make_float2(p.x + d1.x / l1 * f, p.y + d1.y / l1 * f);
                for (const float2& q : {s0, make_float2(0.25f * s0.x + 0.5f * p.x + 0.25f * s1.x, 0.25f * s0.y + 0.5f * p.y + 0.25f * s1.y), s1}) {
                    rz.push_back(q);
                    edgeW.push_back(w);
                }
                continue;
            }
        }
        rz.push_back(rz0[k]);
        edgeW.push_back(w);
    }
    if (edgeW0.empty()) edgeW.clear();
    const int n = int(rz.size());
    float rmax = 0.0f;
    for (const float2& q : rz) rmax = std::max(rmax, q.x);
    const bool closed = std::fabs(a1 - a0 - 2.0f * kPi) < 1e-4f;
    const int nu = arcSegs(rmax, a1 - a0, closed ? 20 : 4);
    // profile normals: perpendicular to the averaged neighbouring segments, pointing away from the solid (the profile
    // runs bottom to top along the outside: the outward normal is (dz, -dr) normalised)
    std::vector<float2> nrm(n);
    std::vector<float> arc(n, 0.0f);
    for (int k = 0; k < n; ++k) {
        const float2 a = rz[std::max(0, k - 1)], b = rz[std::min(n - 1, k + 1)];
        float2 d = make_float2(b.x - a.x, b.y - a.y);
        const float l = std::hypot(d.x, d.y);
        nrm[k] = l > 1e-12f ? make_float2(d.y / l, -d.x / l) : make_float2(1, 0);
        if (k) arc[k] = arc[k - 1] + std::hypot(rz[k].x - rz[k - 1].x, rz[k].y - rz[k - 1].y);
    }
    // an end on the axis faces straight along it
    if (rz[0].x < 1e-6f) nrm[0] = make_float2(0, nrm[0].y < 0 ? -1.0f : (nrm[0].y > 0 ? 1.0f : -1.0f));
    if (rz[n - 1].x < 1e-6f) nrm[n - 1] = make_float2(0, nrm[n - 1].y > 0 ? 1.0f : (nrm[n - 1].y < 0 ? -1.0f : 1.0f));
    const float2 keep = fp;
    if (fp.y == 0.0f) fp.y = 2.0f * kPi * std::max(rmax, 1e-4f);
    grid(nu, n - 1, [&](int i, int j) {
        GridPt g;
        const float a = closed ? a0 + 2.0f * kPi * float(i % nu) / nu : a0 + (a1 - a0) * float(i) / nu;
        const float ca = std::cos(a), sa = std::sin(a);
        g.p = f3(rz[j].x * ca, rz[j].x * sa, rz[j].y);
        g.n = normalize(f3(nrm[j].x * ca, nrm[j].x * sa, nrm[j].y));
        // the surface coordinate keeps running past the seam (the chart must not fold back onto itself)
        g.s = make_float2(rmax * (a1 - a0) * float(i) / nu, arc[j]);
        g.edge = edgeW.empty() ? 0.0f : edgeW[j];
        return g;
    });
    fp = keep;
}

void Kit::slab(const std::vector<float2>& outline, float t, float e) {
    // rows of the outline: the back face's rim (inset e), the side's two rows, the front face's rim (inset e); a
    // quarter-round from face to side made of the inset rows (e: the rounding)
    const int n = int(outline.size());
    float2 cen = make_float2(0, 0);
    for (const float2& q : outline) cen = make_float2(cen.x + q.x / n, cen.y + q.y / n);
    std::vector<float3> side(n);
    for (int k = 0; k < n; ++k) {
        const float2 a = outline[(k + n - 1) % n], p = outline[k], b = outline[(k + 1) % n];
        // weighted by the edges' lengths (as extrude): a straight run's ends keep its normal, the plate does not pillow
        side[k] = normalize(make_float3(p.y - a.y, -(p.x - a.x), 0) + make_float3(b.y - p.y, -(b.x - p.x), 0));
    }
    e = std::min(e, 0.45f * t);
    const int nb = 3;
    struct Row { float inset, z; float nz; float edge; };
    std::vector<Row> rows;
    for (int i = 0; i <= nb; ++i) {   // back rounding: from facing -z to facing out
        const float th = 0.5f * 3.14159265f * float(i) / nb;
        rows.push_back({e - e * std::sin(th), e - e * std::cos(th), -std::cos(th), std::sin(2.0f * th)});
    }
    for (int i = 0; i <= nb; ++i) {   // front rounding
        const float th = 0.5f * 3.14159265f * float(i) / nb;
        rows.push_back({e - e * std::cos(th), t - e + e * std::sin(th), std::sin(th), std::sin(2.0f * th)});
    }
    std::vector<float> arc(n + 1, 0.0f);
    for (int k = 1; k <= n; ++k) arc[k] = arc[k - 1] + std::hypot(outline[k % n].x - outline[k - 1].x, outline[k % n].y - outline[k - 1].y);
    grid(n, int(rows.size()) - 1, [&](int i, int j) {
        GridPt g;
        const int kk = i % n;
        const float2 q = outline[kk];
        const Row& r = rows[j];
        g.p = make_float3(q.x - side[kk].x * r.inset, q.y - side[kk].y * r.inset, r.z);
        g.n = normalize(side[kk] * std::sqrt(std::max(0.0f, 1.0f - r.nz * r.nz)) + make_float3(0, 0, r.nz));
        g.s = make_float2(arc[i], r.z);
        g.edge = r.edge;
        return g;
    });
    // the two faces: fans from the centre to the inset outline
    for (int top = 0; top < 2; ++top) {
        const float z = top ? t : 0.0f;
        const int nr = std::max(1, segs(0.3f * t + 0.0f, 1));
        (void)nr;
        grid(n, 1, [&](int i, int j) {
            GridPt g;
            const int kk = (top ? n - i : i) % n;
            const float2 q = outline[kk];
            const float2 qi = make_float2(q.x - side[kk].x * e, q.y - side[kk].y * e);
            g.p = j ? make_float3(qi.x, qi.y, z) : make_float3(cen.x, cen.y, z);
            g.n = make_float3(0, 0, top ? 1.0f : -1.0f);
            g.s = make_float2(g.p.x, g.p.y);
            return g;
        });
    }
}

std::vector<float2> Kit::superellipse(float a, float b, float p, int n) {
    std::vector<float2> o;
    for (int i = 0; i < n; ++i) {
        const float th = 2.0f * 3.14159265f * float(i) / n;
        const float c = std::cos(th), s = std::sin(th);
        const float r = std::pow(std::pow(std::fabs(c) / a, p) + std::pow(std::fabs(s) / b, p), -1.0f / p);
        o.push_back(make_float2(r * c, r * s));
    }
    return o;
}

void mirrorX(Mesh& m) {
    for (Vtx& v : m.v) {
        v.p.x = -v.p.x;
        v.n.x = -v.n.x;
        v.dn.x = -v.dn.x;
        v.t.x = -v.t.x;
        v.uv.x = -v.uv.x;   // the surface's own coordinates too: printing reads forwards, threads stay right-handed
    }
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) std::swap(m.idx[k + 1], m.idx[k + 2]);
}

// ---- finishing
void finishMesh(Mesh& m) {
    // displacement normals: the average over every vertex at one position (quantised to 10 µm)
    struct Key {
        int64_t x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct H {
        size_t operator()(const Key& k) const { return size_t(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL); }
    };
    std::unordered_map<Key, float3, H> acc;
    acc.reserve(m.v.size());
    auto key = [](float3 p) { return Key{int64_t(std::llround(p.x * 1e5)), int64_t(std::llround(p.y * 1e5)), int64_t(std::llround(p.z * 1e5))}; };
    for (const Vtx& a : m.v) {
        auto it = acc.find(key(a.p));
        if (it == acc.end()) acc.emplace(key(a.p), a.n);
        else it->second += a.n;
    }
    for (Vtx& a : m.v) {
        const float3 s = acc[key(a.p)];
        const float l = length(s);
        a.dn = l > 1e-6f ? s / l : a.n;
    }
    // tangents along the surface coordinate u (per triangle, accumulated)
    std::vector<float3> tu(m.v.size(), f3(0, 0, 0)), tv(m.v.size(), f3(0, 0, 0));
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
        const uint32_t i0 = m.idx[k], i1 = m.idx[k + 1], i2 = m.idx[k + 2];
        const Vtx &a = m.v[i0], &b = m.v[i1], &c = m.v[i2];
        const float3 e1 = b.p - a.p, e2 = c.p - a.p;
        const float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y, du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
        const float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-14f) continue;
        const float r = 1.0f / det;
        const float3 sd = (e1 * dv2 - e2 * dv1) * r, td = (e2 * du1 - e1 * du2) * r;
        for (uint32_t i : {i0, i1, i2}) {
            tu[i] += sd;
            tv[i] += td;
        }
    }
    for (size_t i = 0; i < m.v.size(); ++i) {
        Vtx& a = m.v[i];
        float3 t = tu[i] - a.n * dot(a.n, tu[i]);
        if (dot(t, t) < 1e-16f) {
            t = cross(std::fabs(a.n.y) < 0.9f ? f3(0, 1, 0) : f3(1, 0, 0), a.n);
        }
        t = normalize(t);
        const float w = dot(cross(a.n, t), tv[i]) < 0.0f ? -1.0f : 1.0f;
        a.t = make_float4(t.x, t.y, t.z, w);
    }
}

}   // namespace dz
