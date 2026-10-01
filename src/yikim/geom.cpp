#include "yikim/geom.h"

#include <algorithm>
#include <cmath>

std::vector<double3> Poly::vertices(double eps) const {
    std::vector<double3> out;
    for (const PolyFace& f : faces)
        for (const double3& p : f.v) {
            bool dup = false;
            for (const double3& q : out)
                if (std::fabs(p.x - q.x) < eps && std::fabs(p.y - q.y) < eps && std::fabs(p.z - q.z) < eps) { dup = true; break; }
            if (!dup) out.push_back(p);
        }
    return out;
}

void Poly::bounds(double3& lo, double3& hi) const {
    lo = d3(1e30, 1e30, 1e30);
    hi = d3(-1e30, -1e30, -1e30);
    for (const PolyFace& f : faces)
        for (const double3& p : f.v) {
            lo = make_double3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi = make_double3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
}

Poly polyBox(double3 c, double3 h, double3 ax, double3 ay, double3 az) {
    Poly p;
    auto corner = [&](int sx, int sy, int sz) { return c + ax * (sx * h.x) + ay * (sy * h.y) + az * (sz * h.z); };
    auto face = [&](double3 n, double3 a, double3 b, double3 cc, double3 dd) {
        PolyFace f;
        f.v = {a, b, cc, dd};
        f.n = n;
        f.d = ddot(n, a);
        f.tag = 0;
        p.faces.push_back(f);
    };
    // counter-clockwise seen from outside
    face(ax, corner(1, -1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(1, -1, 1));
    face(ax * -1.0, corner(-1, -1, -1), corner(-1, -1, 1), corner(-1, 1, 1), corner(-1, 1, -1));
    face(ay, corner(-1, 1, -1), corner(-1, 1, 1), corner(1, 1, 1), corner(1, 1, -1));
    face(ay * -1.0, corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1));
    face(az, corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1));
    face(az * -1.0, corner(-1, -1, -1), corner(-1, 1, -1), corner(1, 1, -1), corner(1, -1, -1));
    return p;
}

bool polyClip(Poly& p, double3 n, double d, int tag) {
    const double eps = 1e-9;
    std::vector<PolyFace> out;
    std::vector<std::pair<double3, double3>> cutEdges;   // segments on the cutting plane
    bool clipped = false;
    for (const PolyFace& f : p.faces) {
        const size_t m = f.v.size();
        std::vector<double> s(m);
        bool anyIn = false, anyOut = false;
        for (size_t i = 0; i < m; ++i) {
            s[i] = ddot(n, f.v[i]) - d;
            if (s[i] <= eps) anyIn = true;
            if (s[i] > eps) anyOut = true;
        }
        if (!anyOut) { out.push_back(f); continue; }
        clipped = true;
        if (!anyIn) continue;
        PolyFace g = f;
        g.v.clear();
        std::vector<double3> onPlane;
        for (size_t i = 0; i < m; ++i) {
            const size_t j = (i + 1) % m;
            const double3 a = f.v[i], b = f.v[j];
            const bool ain = s[i] <= eps, bin = s[j] <= eps;
            if (ain) g.v.push_back(a);
            if (ain != bin) {
                const double t = s[i] / (s[i] - s[j]);
                const double3 q = a + (b - a) * t;
                g.v.push_back(q);
                onPlane.push_back(q);
            } else if (ain && std::fabs(s[i]) <= eps) {
                onPlane.push_back(a);
            }
        }
        if (g.v.size() >= 3) out.push_back(g);
        if (onPlane.size() >= 2) cutEdges.push_back({onPlane[0], onPlane[1]});
    }
    if (!clipped) return !p.empty();
    // cap face: the points on the plane, ordered around their centre
    std::vector<double3> pts;
    for (auto& e : cutEdges) { pts.push_back(e.first); pts.push_back(e.second); }
    std::vector<double3> uniq;
    for (const double3& q : pts) {
        bool dup = false;
        for (const double3& u : uniq)
            if (dlength(q - u) < 1e-7) { dup = true; break; }
        if (!dup) uniq.push_back(q);
    }
    if (uniq.size() >= 3) {
        double3 c = d3(0, 0, 0);
        for (const double3& q : uniq) c = c + q;
        c = c * (1.0 / uniq.size());
        // basis on the plane
        double3 t = std::fabs(n.x) < 0.9 ? dnormalize(dcross(n, d3(1, 0, 0))) : dnormalize(dcross(n, d3(0, 1, 0)));
        double3 b = dcross(n, t);
        std::sort(uniq.begin(), uniq.end(), [&](const double3& u, const double3& v) {
            const double3 du = u - c, dv = v - c;
            return std::atan2(ddot(du, b), ddot(du, t)) < std::atan2(ddot(dv, b), ddot(dv, t));
        });
        // atan2 ordering is counter-clockwise about +n (t x b = n): seen from outside (n points out) that is CCW
        PolyFace cap;
        cap.v = uniq;
        cap.n = n;
        cap.d = d;
        cap.tag = tag;
        out.push_back(cap);
    }
    p.faces = out;
    return !p.empty();
}

MassProps polyMass(const Poly& p) {
    // tetrahedra from a reference point to each face triangle (fan); exact for a closed polyhedron
    MassProps m;
    double3 ref = d3(0, 0, 0);
    size_t cnt = 0;
    for (const PolyFace& f : p.faces)
        for (const double3& q : f.v) { ref = ref + q; ++cnt; }
    if (cnt) ref = ref * (1.0 / cnt);
    double vol = 0;
    double3 cen = d3(0, 0, 0);
    double C[3][3] = {};   // covariance about ref, integral of x x^T
    for (const PolyFace& f : p.faces) {
        for (size_t i = 1; i + 1 < f.v.size(); ++i) {
            const double3 a = f.v[0] - ref, b = f.v[i] - ref, c = f.v[i + 1] - ref;
            const double v = ddot(a, dcross(b, c)) / 6.0;
            vol += v;
            cen = cen + (a + b + c) * (v / 4.0);
            // canonical tetrahedron covariance: v/20 * (sum_i x_i x_i^T + (sum x)(sum x)^T)
            const double3 s = a + b + c;
            const double3 P[3] = {a, b, c};
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) {
                    double acc = 0;
                    for (int q = 0; q < 3; ++q) acc += (&P[q].x)[r] * (&P[q].x)[k];
                    acc += (&s.x)[r] * (&s.x)[k];
                    C[r][k] += v / 20.0 * acc;
                }
        }
    }
    m.volume = vol;
    const double3 cl = vol != 0 ? cen * (1.0 / vol) : d3(0, 0, 0);   // relative to ref
    m.centroid = ref + cl;
    // covariance about the centroid
    double Cc[3][3];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) Cc[r][k] = C[r][k] - vol * (&cl.x)[r] * (&cl.x)[k];
    const double tr = Cc[0][0] + Cc[1][1] + Cc[2][2];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) m.inertia[r][k] = (r == k ? tr : 0.0) - Cc[r][k];
    return m;
}

void eigenSym3(const double a_[3][3], double vals[3], double v[3][3]) {
    double a[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) { a[i][j] = a_[i][j]; v[i][j] = i == j ? 1.0 : 0.0; }
    for (int sweep = 0; sweep < 50; ++sweep) {
        const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
        if (off < 1e-18) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-30) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 3; ++k) {   // A = J^T A J
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < 3; ++i) vals[i] = a[i][i];
    // right-handed: third column = first x second
    const double3 c0 = d3(v[0][0], v[1][0], v[2][0]), c1 = d3(v[0][1], v[1][1], v[2][1]);
    const double3 c2 = dcross(c0, c1);
    v[0][2] = c2.x; v[1][2] = c2.y; v[2][2] = c2.z;
}

float4 quatFromMatrixColumns(const double m[3][3]) {
    const double tr = m[0][0] + m[1][1] + m[2][2];
    double x, y, z, w;
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        w = 0.25 * s; x = (m[2][1] - m[1][2]) / s; y = (m[0][2] - m[2][0]) / s; z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        w = (m[2][1] - m[1][2]) / s; x = 0.25 * s; y = (m[0][1] + m[1][0]) / s; z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        w = (m[0][2] - m[2][0]) / s; x = (m[0][1] + m[1][0]) / s; y = 0.25 * s; z = (m[1][2] + m[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        w = (m[1][0] - m[0][1]) / s; x = (m[0][2] + m[2][0]) / s; y = (m[1][2] + m[2][1]) / s; z = 0.25 * s;
    }
    return qnormalize(make_float4(float(x), float(y), float(z), float(w)));
}
