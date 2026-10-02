#include "scenes/laboratuvar/lab_geom.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr float kPi = 3.14159265358979f;

unsigned hashU(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hashF(unsigned x) { return (hashU(x) >> 8) * (1.0f / 16777216.0f); }

void arc(Profile& p, float cr, float cy, float rad, float a0, float a1, int n, bool smooth) {
    for (int k = 0; k <= n; ++k) {
        const float a = a0 + (a1 - a0) * k / n;
        p.push_back({cr + rad * std::cos(a), cy + rad * std::sin(a), smooth});
    }
}

// the outward normal of a profile segment (the solid on its left: the normal is its right)
float2 segNormal(const ProfilePoint& a, const ProfilePoint& b) {
    float tr = b.r - a.r, ty = b.y - a.y;
    const float l = std::sqrt(tr * tr + ty * ty);
    if (l < 1e-9f) return make_float2(0, 0);
    tr /= l;
    ty /= l;
    return make_float2(ty, -tr);
}

}   // namespace

Profile glassProfile(float ri, float H, float wall, float base, float taper) {
    Profile p;
    const float ro = ri + wall;
    const float rob = ro * taper, rib = std::max(0.002f, ri * taper - (1.0f - taper) * 0.0f);
    const float cb = std::min(0.0025f, 0.4f * base);   // the foot's rounded edge
    const float ci = std::min(0.004f, 0.5f * rib);    // the inside's rounded bottom
    p.push_back({0.0f, 0.0f, false});
    arc(p, rob - cb, cb, cb, -0.5f * kPi, 0.0f, 6, true);
    p.push_back({ro, H - 0.5f * wall, true});
    arc(p, ri + 0.5f * wall, H - 0.5f * wall, 0.5f * wall, 0.0f, kPi, 10, true);
    p.push_back({rib, base + ci, true});
    arc(p, rib - ci, base + ci, ci, 0.0f, -0.5f * kPi, 6, true);
    p.push_back({0.0f, base, false});
    // the first and last points of the arcs repeat their neighbours' ends: drop exact repeats
    Profile q;
    for (const ProfilePoint& a : p)
        if (q.empty() || std::fabs(q.back().r - a.r) + std::fabs(q.back().y - a.y) > 1e-7f) q.push_back(a);
    return q;
}

Profile funnelProfile(float rTop, float rNeck, float coneHeight, float stemLength, float wall) {
    // the solid of a thin funnel: outer surface down, then the inner surface back up (a closed sliver)
    Profile p;
    const float yTop = stemLength + coneHeight;
    p.push_back({rNeck, 0.0f, false});
    p.push_back({rNeck + wall, 0.0f, false});
    p.push_back({rNeck + wall, stemLength, true});
    p.push_back({rTop + wall, yTop, false});
    p.push_back({rTop, yTop, false});
    p.push_back({rNeck, stemLength + 0.3f * wall, true});
    return p;
}

HostMesh revolve(const Profile& p, int segs) {
    HostMesh m;
    const int n = int(p.size());
    for (int i = 0; i < n; ++i) {
        const ProfilePoint& a = p[size_t(i)];
        const ProfilePoint& b = p[size_t((i + 1) % n)];
        if (a.r < 1e-6f && b.r < 1e-6f) continue;   // along the axis
        const float2 ns = segNormal(a, b);
        if (ns.x == 0.0f && ns.y == 0.0f) continue;
        auto vnormal = [&](int k, const ProfilePoint& v) {
            if (!v.smooth) return ns;
            const ProfilePoint& pr = p[size_t((k - 1 + n) % n)];
            const ProfilePoint& nx = p[size_t((k + 1) % n)];
            const float2 n0 = segNormal(pr, v), n1 = segNormal(v, nx);
            float2 s = make_float2(n0.x + n1.x, n0.y + n1.y);
            const float l = std::sqrt(s.x * s.x + s.y * s.y);
            return l > 1e-6f ? make_float2(s.x / l, s.y / l) : ns;
        };
        const float2 na = vnormal(i, a), nb = vnormal((i + 1) % n, b);
        const unsigned base = unsigned(m.pos.size());
        for (int s = 0; s <= segs; ++s) {
            const float ang = 2.0f * kPi * s / segs, c = std::cos(ang), sn = std::sin(ang);
            m.pos.push_back(make_float3(a.r * c, a.y, a.r * sn));
            m.nrm.push_back(make_float3(na.x * c, na.y, na.x * sn));
            m.pos.push_back(make_float3(b.r * c, b.y, b.r * sn));
            m.nrm.push_back(make_float3(nb.x * c, nb.y, nb.x * sn));
        }
        for (int s = 0; s < segs; ++s) {
            const unsigned a0 = base + 2 * s, b0 = a0 + 1, a1 = a0 + 2, b1 = a0 + 3;
            // outward: (b - a) x (around) has the profile normal's sign
            m.idx.push_back(make_uint3(a0, a1, b0));
            m.idx.push_back(make_uint3(b0, a1, b1));
        }
    }
    // (around x along-the-profile) is the inward normal for a counter-clockwise profile: turn every face outward
    for (uint3& q : m.idx) std::swap(q.y, q.z);
    return m;
}

std::vector<float3> sampleProfileWalls(const Profile& p, float spacing, float step) {
    // points along the outline, moved inwards by half a spacing (at most to the middle of a thin wall: the wall's
    // thickness is measured as the distance to the outline's other side straight across)
    std::vector<float2> pts;
    const int n = int(p.size());
    auto thicknessAt = [&](float2 q, float2 inward) {
        // the nearest crossing of the ray q + inward * t with another segment
        float best = 1e9f;
        for (int j = 0; j < n; ++j) {
            const ProfilePoint& a = p[size_t(j)];
            const ProfilePoint& b = p[size_t((j + 1) % n)];
            const float ex = b.r - a.r, ey = b.y - a.y;
            const float den = inward.x * (-ey) - inward.y * (-ex);
            if (std::fabs(den) < 1e-12f) continue;
            const float qx = a.r - q.x, qy = a.y - q.y;
            const float t = (qx * (-ey) - qy * (-ex)) / den;
            const float u = (inward.x * qy - inward.y * qx) / den;
            if (t > 1e-6f && u >= 0.0f && u <= 1.0f) best = std::min(best, t);
        }
        return best;
    };
    for (int i = 0; i < n; ++i) {
        const ProfilePoint& a = p[size_t(i)];
        const ProfilePoint& b = p[size_t((i + 1) % n)];
        const float2 nr = segNormal(a, b);
        const float L = std::sqrt((b.r - a.r) * (b.r - a.r) + (b.y - a.y) * (b.y - a.y));
        if (L < 1e-7f) continue;
        const int k = std::max(1, int(std::ceil(L / step)));
        for (int s = 0; s < k; ++s) {
            const float t = (s + 0.5f) / k;
            const float2 q = make_float2(a.r + (b.r - a.r) * t, a.y + (b.y - a.y) * t);
            const float2 in = make_float2(-nr.x, -nr.y);
            const float th = thicknessAt(q, in);
            const float off = std::min(0.5f * spacing, 0.5f * th);
            const float2 c = make_float2(q.x + in.x * off, q.y + in.y * off);
            if (c.x < -1e-6f) continue;
            pts.push_back(make_float2(std::max(0.0f, c.x), c.y));
        }
    }
    // turned about the axis, then thinned so no two are closer than 0.6 step
    std::vector<float3> out;
    const float cell = 0.6f * step;
    std::unordered_map<long long, std::vector<int>> grid;
    auto key = [&](int x, int y, int z) { return (long long)(x + 4096) * 67108864LL + (long long)(y + 4096) * 8192LL + (long long)(z + 4096); };
    for (const float2& q : pts) {
        const int around = std::max(1, int(std::round(2.0f * kPi * q.x / step)));
        for (int s = 0; s < around; ++s) {
            const float ang = 2.0f * kPi * (s + 0.5f * (int(q.y / step) & 1)) / around;
            const float3 v = make_float3(q.x * std::cos(ang), q.y, q.x * std::sin(ang));
            const int cx = int(std::floor(v.x / cell)), cy = int(std::floor(v.y / cell)), cz = int(std::floor(v.z / cell));
            bool close = false;
            for (int dz = -1; dz <= 1 && !close; ++dz)
                for (int dy = -1; dy <= 1 && !close; ++dy)
                    for (int dx = -1; dx <= 1 && !close; ++dx) {
                        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                        if (it == grid.end()) continue;
                        for (int j : it->second) {
                            const float3 w = out[size_t(j)] - v;
                            if (dot(w, w) < cell * cell) { close = true; break; }
                        }
                    }
            if (close) continue;
            grid[key(cx, cy, cz)].push_back(int(out.size()));
            out.push_back(v);
        }
    }
    return out;
}

std::vector<float3> fillCylinder(float radius, float y0, float y1, float s, unsigned seed) {
    std::vector<float3> out;
    const int nr = int(std::floor(radius / s));
    int k = 0;
    for (float y = y0 + 0.5f * s; y <= y1 - 0.5f * s + 1e-6f; y += s)
        for (int z = -nr; z <= nr; ++z)
            for (int x = -nr; x <= nr; ++x, ++k) {
                const float px = x * s, pz = z * s;
                if (px * px + pz * pz > (radius - 0.5f * s) * (radius - 0.5f * s)) continue;
                const unsigned h = seed * 7919u + unsigned(k) * 2654435761u;
                out.push_back(make_float3(px + (hashF(h) - 0.5f) * 0.1f * s, y + (hashF(h + 1) - 0.5f) * 0.1f * s, pz + (hashF(h + 2) - 0.5f) * 0.1f * s));
            }
    return out;
}

HostMesh boxMesh(float3 lo, float3 hi) {
    HostMesh m;
    const float3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    const float3 nn[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (int i = 0; i < 6; ++i) {
        const unsigned b = unsigned(m.pos.size());
        for (int k = 0; k < 4; ++k) {
            m.pos.push_back(c[f[i][k]]);
            m.nrm.push_back(nn[i]);
        }
        m.idx.push_back(make_uint3(b, b + 1, b + 2));
        m.idx.push_back(make_uint3(b, b + 2, b + 3));
    }
    return m;
}

HostMesh tableMesh(float hw, float zFront, float zBack, float thick) {
    // the top a grid (its reflections and the slate's pattern need no detail: two triangles would do, but a front
    // edge rounded by a quarter cylinder catches the key light as a real table's does)
    HostMesh m = boxMesh(make_float3(-hw, -thick, zBack), make_float3(hw, 0.0f, zFront - 0.01f));
    HostMesh e;
    const int segs = 12;
    const float rr = 0.01f;
    for (int s = 0; s <= segs; ++s) {
        const float a = 0.5f * kPi * s / segs;   // from the top (a = 0) round to the front face
        const float3 nrm = make_float3(0, std::cos(a), std::sin(a));
        const float y = -rr + rr * std::cos(a), z = zFront - 0.01f + rr * std::sin(a);
        e.pos.push_back(make_float3(-hw, y, z));
        e.nrm.push_back(nrm);
        e.pos.push_back(make_float3(hw, y, z));
        e.nrm.push_back(nrm);
    }
    for (int s = 0; s < segs; ++s) {
        const unsigned a = 2 * s;
        e.idx.push_back(make_uint3(a, a + 2, a + 1));
        e.idx.push_back(make_uint3(a + 1, a + 2, a + 3));
    }
    m.append(e);
    HostMesh front = boxMesh(make_float3(-hw, -thick, zFront - 0.01f), make_float3(hw, -rr, zFront));
    m.append(front);
    return m;
}

HostMesh backdropMesh(float hw, float zWall, float sweep, float height, float floorFront) {
    HostMesh m;
    // a profile in (z, y): along the floor from floorFront back to the sweep, round the sweep, up the wall
    std::vector<float2> pr;   // (z, y)
    std::vector<float2> nr;
    pr.push_back(make_float2(floorFront, -0.0005f));
    nr.push_back(make_float2(0, 1));
    const int segs = 16;
    for (int s = 0; s <= segs; ++s) {
        const float a = 0.5f * kPi * s / segs;
        pr.push_back(make_float2(zWall + sweep - sweep * std::sin(a), -0.0005f + sweep - sweep * std::cos(a)));
        nr.push_back(make_float2(std::sin(a), std::cos(a)));
    }
    pr.push_back(make_float2(zWall, height));
    nr.push_back(make_float2(1, 0));
    for (size_t i = 0; i < pr.size(); ++i) {
        const float3 n3 = make_float3(0, nr[i].y, nr[i].x);
        m.pos.push_back(make_float3(-hw, pr[i].y, pr[i].x));
        m.nrm.push_back(n3);
        m.pos.push_back(make_float3(hw, pr[i].y, pr[i].x));
        m.nrm.push_back(n3);
    }
    for (unsigned i = 0; i + 1 < pr.size(); ++i) {
        const unsigned a = 2 * i;
        m.idx.push_back(make_uint3(a, a + 1, a + 2));
        m.idx.push_back(make_uint3(a + 1, a + 3, a + 2));
    }
    // face the room
    const uint3 t = m.idx[0];
    const float3 fn = cross(m.pos[t.y] - m.pos[t.x], m.pos[t.z] - m.pos[t.x]);
    if (dot(fn, m.nrm[t.x]) < 0.0f)
        for (uint3& q : m.idx) std::swap(q.y, q.z);
    return m;
}

HostMesh sphereMesh(float radius, int rings, int segs) {
    HostMesh m;
    for (int r = 0; r <= rings; ++r) {
        const float th = kPi * r / rings;
        for (int s = 0; s <= segs; ++s) {
            const float ph = 2.0f * kPi * s / segs;
            const float3 n = make_float3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.pos.push_back(n * radius);
            m.nrm.push_back(n);
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segs; ++s) {
            const unsigned a = r * (segs + 1) + s, b = a + segs + 1;
            m.idx.push_back(make_uint3(a, a + 1, b));
            m.idx.push_back(make_uint3(b, a + 1, b + 1));
        }
    const uint3 t = m.idx[size_t(segs)];
    const float3 fn = cross(m.pos[t.y] - m.pos[t.x], m.pos[t.z] - m.pos[t.x]);
    if (dot(fn, m.nrm[t.x]) < 0.0f)
        for (uint3& q : m.idx) std::swap(q.y, q.z);
    return m;
}

HostMesh dipperMesh(float headR, float headL, float handleR, float handleL) {
    // a profile turned about y: the head (ridges) from y = 0 to headL, a neck, the handle up to handleL
    Profile p;
    p.push_back({0.0f, 0.0f, false});
    p.push_back({0.55f * headR, 0.0f, true});
    const int ridges = 5;
    for (int k = 0; k < ridges; ++k) {
        const float y0 = headL * (0.08f + 0.84f * k / ridges), y1 = headL * (0.08f + 0.84f * (k + 0.5f) / ridges);
        p.push_back({headR, y0, true});
        p.push_back({headR * 0.72f, y1, true});
    }
    p.push_back({headR * 0.8f, headL, true});
    p.push_back({handleR, headL + 0.01f, true});
    p.push_back({handleR, handleL - handleR, true});
    arc(p, 0.0f, handleL - handleR, handleR, 0.0f, 0.5f * kPi, 6, true);
    // back down the axis (the profile closes along it)
    Profile q;
    for (const ProfilePoint& a : p)
        if (q.empty() || std::fabs(q.back().r - a.r) + std::fabs(q.back().y - a.y) > 1e-7f) q.push_back(a);
    // revolve() wants the solid on the left (counter-clockwise): this one runs up the outside, which is clockwise
    std::reverse(q.begin(), q.end());
    return revolve(q, 48);
}

void watermelonFill(float3 R, float rind, float s, unsigned seed, std::vector<float3>& pos, std::vector<uint8_t>& isRind) {
    const int nx = int(std::ceil(R.x / s)), ny = int(std::ceil(R.y / s)), nz = int(std::ceil(R.z / s));
    int k = 0;
    for (int z = -nz; z <= nz; ++z)
        for (int y = -ny; y <= ny; ++y)
            for (int x = -nx; x <= nx; ++x, ++k) {
                const unsigned h = seed * 7919u + unsigned(k) * 2654435761u;
                const float3 p = make_float3(x * s + (hashF(h) - 0.5f) * 0.1f * s, y * s + (hashF(h + 1) - 0.5f) * 0.1f * s,
                                             z * s + (hashF(h + 2) - 0.5f) * 0.1f * s);
                const float3 q = make_float3(p.x / R.x, p.y / R.y, p.z / R.z);
                const float e = std::sqrt(dot(q, q));
                // distance to the surface, roughly: (1 - e) times the local radius
                const float rl = std::sqrt(dot(p, p)) / std::max(e, 1e-6f);
                const float d = (1.0f - e) * rl;
                if (d < 0.5f * s) continue;
                pos.push_back(p);
                isRind.push_back(d < rind ? 1 : 0);
            }
}

void xfRotateZ(float m[12], float a, float3 pv, float3 t) {
    const float c = std::cos(a), s = std::sin(a);
    // x' = R (x - pv) + pv + t
    m[0] = c; m[1] = -s; m[2] = 0;
    m[4] = s; m[5] = c;  m[6] = 0;
    m[8] = 0; m[9] = 0;  m[10] = 1;
    const float3 rp = make_float3(c * pv.x - s * pv.y, s * pv.x + c * pv.y, pv.z);
    m[3] = pv.x - rp.x + t.x;
    m[7] = pv.y - rp.y + t.y;
    m[11] = pv.z - rp.z + t.z;
}

float3 xfApply(const float m[12], float3 p) {
    return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
}
