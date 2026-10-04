// Scene 5 "Koy", Ada: the island and the pier (see koy_island.h).
#include "scenes/koy/koy_island.h"

#include "scenes/koy/koy_noise.h"
#include "scenes/koy/koy_sdf.h"
#include "scenes/koy/koy_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {
constexpr float kPi = 3.14159265f;
int angleBin(float a) {   // a in (-pi, pi] -> 0..179
    int b = int(std::floor((a + kPi) / (2.0f * kPi) * 180.0f));
    return ((b % 180) + 180) % 180;
}
}   // namespace

float KoyIslandShape::waterline(float ux, float uz) const {
    const float o = float(seed) * 7.3f;
    float R = R0 * (1.0f + 0.13f * kn::fbm3(ux * 1.4f + o, uz * 1.4f, 0.5f, 3) + 0.05f * kn::fbm3(ux * 4.5f, uz * 4.5f + o, 1.3f, 2));
    const float c = ux * coveDir.x + uz * coveDir.y;   // cosine of the angle to the cove's axis
    return R - coveDepth * kn::smoothstep(std::cos(coveHalf), 1.0f, c);
}

float KoyIslandShape::sdf(float3 p0) const {
    const float o = float(seed) * 13.7f;
    // a low warp: the whole mass bent by a metre or two of noise (no drawn circle)
    const float f = 0.05f;
    float3 p = p0;
    p.x += 1.6f * kn::fbm3(p0.x * f + o, p0.y * f, p0.z * f, 3);
    p.z += 1.6f * kn::fbm3(p0.x * f, p0.y * f, p0.z * f + o + 3.3f, 3);
    p.y += 0.5f * kn::fbm3(p0.x * f, p0.y * f + o + 7.1f, p0.z * f, 3);
    const float dx = p.x - cx, dz = p.z - cz;
    const float r = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f), ux = dx / r, uz = dz / r;
    const float c = ux * coveDir.x + uz * coveDir.y;
    const float cove = kn::smoothstep(std::cos(coveHalf * 1.4f), 1.0f, c);   // the cove's own shore: low and gentle
    const float Rw = waterline(ux, uz);
    const float t = Rw - r;   // > 0 inland
    const float cl = cliff * (1.0f - cove), sl = slope + (0.35f - slope) * cove;
    float Y, dY;
    if (t >= 0.0f) {   // a low cliff within ~2 m, then a gentle crown
        const float a = std::exp(-t / 1.4f), b = std::exp(-t / 13.0f);
        Y = cl * (1.0f - a) + (H - cl) * (1.0f - b);
        dY = cl * a / 1.4f + (H - cl) * b / 13.0f;
    } else {           // under the water the rock falls away into the sand
        Y = t * sl;
        dY = sl;
    }
    float d = (p.y - Y) / std::sqrt(1.0f + dY * dY);
    for (int k = 0; k < reefCount; ++k) {   // a reef arm: a ridge whose crest falls seaward, its flanks rounded
        const Reef& R = reefs[k];
        const float2 ab = make_float2(R.b.x - R.a.x, R.b.y - R.a.y);
        const float L2 = ab.x * ab.x + ab.y * ab.y;
        const float u = std::min(std::max(((p.x - R.a.x) * ab.x + (p.z - R.a.y) * ab.y) / L2, 0.0f), 1.0f);
        const float qx = p.x - (R.a.x + ab.x * u), qz = p.z - (R.a.y + ab.y * u);
        // the arm breaks up seaward: its width and crest wander, gaps open between outcrops
        const float w = R.halfW * (0.75f + 0.35f * kn::fbm3(u * 6.0f + float(k) * 9.1f + o, 0.3f, 1.7f, 2)) * (1.0f - 0.35f * u);
        const float broken = kn::smoothstep(0.45f, 1.0f, u) * kn::smoothstep(-0.05f, 0.25f, kn::noise3(u * 9.0f + float(k) * 4.3f, 2.2f, o));
        const float top = R.topA + (R.topB - R.topA) * u - 1.6f * broken;
        const float rr = std::sqrt(qx * qx + qz * qz) / std::max(w, 0.3f);
        const float Yr = top - 2.4f * rr * rr;       // a rounded crest, steep flanks
        const float sr = 4.8f * rr / std::max(w, 0.3f);
        const float dr = (p.y - Yr) / std::sqrt(1.0f + sr * sr);
        const float hk = std::max(1.5f - std::fabs(d - dr), 0.0f) / 1.5f;
        d = std::min(d, dr) - hk * hk * 1.5f * 0.25f;
    }
    if (std::fabs(d) < 3.5f) {
        const float relief = 1.0f - 0.7f * cove;
        // blocks split along joints and beds, the joints opened into crevices, every block weathered on its own
        const kn::Cell cb = kn::cellular3(p.x / 5.0f + o, p.y / 2.8f, p.z / 5.0f, seed);
        const float crack = cb.f2 - cb.f1;
        d += relief * 0.14f * (1.0f - kn::smoothstep(0.0f, 0.04f, crack));
        d -= relief * 0.38f * (float(unsigned(cb.id) & 0xFFFu) / 4095.0f - 0.5f) * kn::smoothstep(0.0f, 0.06f, crack);
        // beds: tilted, wavy; some stand out as ledges
        const float by = (p.y + 0.06f * p.x - 0.04f * p.z + 0.5f * kn::noise3(p.x * 0.05f + o, 2.1f, p.z * 0.05f)) / 0.9f;
        const int layer = int(std::floor(by));
        const float fr = by - float(layer);
        const float hl = kn::hash01(layer, int(seed), 11);
        const float step = hl > 0.6f ? 0.25f * (hl - 0.6f) / 0.4f * 2.0f : 0.0f;
        const float patch = kn::smoothstep(-0.1f, 0.45f, kn::fbm3(p.x * 0.14f + o, p.y * 0.05f, p.z * 0.14f, 3));
        d -= relief * patch * step * (1.0f - kn::smoothstep(0.75f, 1.0f, fr)) * kn::smoothstep(0.0f, 0.15f, fr);
        // mid relief and eroded creases
        d += relief * 0.36f * kn::fbm3(p.x * 0.4f + o, p.y * 0.45f, p.z * 0.4f, 5);
        d -= relief * 0.16f * (kn::ridged3(p.x * 0.28f, p.y * 0.28f + o, p.z * 0.28f, 4) - 0.5f);
    }
    // the boulders along the foot (each a squat, lumpy ellipsoid)
    const float a = std::atan2(p0.z - cz, p0.x - cx);
    for (int bi : bins[size_t(angleBin(a))]) {
        const Boulder& B = boulders[size_t(bi)];
        const float3 q = p0 - B.c;
        if (q.x * q.x + q.z * q.z > (B.r * 1.6f + 1.0f) * (B.r * 1.6f + 1.0f)) continue;
        float db = -1e9f;   // the polyhedron: the farthest of its planes (a convex block)
        for (int k = 0; k < 9; ++k) db = std::max(db, dot(B.n[k], q) - B.d[k]);
        if (db < 0.5f) {   // its own relief: lumps, eroded creases, a crack or two
            db += 0.11f * B.r * kn::fbm3(q.x * 2.2f / B.r + B.ang * 9.0f, q.y * 2.2f / B.r, q.z * 2.2f / B.r, 4);
            db -= 0.05f * B.r * (kn::ridged3(q.x * 1.6f / B.r, q.y * 1.6f / B.r + B.ang * 5.0f, q.z * 1.6f / B.r, 3) - 0.5f);
            const kn::Cell cc = kn::cellular3(q.x / (0.9f * B.r) + B.ang * 3.0f, q.y / (0.7f * B.r), q.z / (0.9f * B.r), seed);
            db += 0.035f * B.r * (1.0f - kn::smoothstep(0.0f, 0.05f, cc.f2 - cc.f1));
        }
        // worn edges: the block's corners rounded towards a sphere a little
        db = std::max(db, (length(q) - 1.25f * B.r) * 0.8f);
        d = std::min(d, db);
    }
    return d;
}

KoyIslandShape koyIslandShape(const Config& cfg, const std::function<float(float, float)>& floorY) {
    KoyIslandShape s;
    const float3 c = cfg.v3("island.centre", make_float3(0.0f, 0.0f, 60.0f));
    s.cx = c.x;
    s.cz = c.z;
    s.R0 = cfg.f("island.radius", 35.0f);
    s.H = cfg.f("island.height", 14.0f);
    s.cliff = cfg.f("island.cliff", 3.0f);
    s.slope = cfg.f("island.slope", 1.1f);
    s.coveDepth = cfg.f("island.cove_depth", 11.0f);
    s.coveHalf = cfg.f("island.cove_half_deg", 17.0f) * kPi / 180.0f;
    s.seed = unsigned(cfg.i("island.seed", 5));
    // the reef arms either side of the pier's channel
    {
        const float3 ra = cfg.v3("reef.start", make_float3(9.0f, -0.5f, 30.0f)), rb = cfg.v3("reef.end", make_float3(15.0f, -2.6f, -10.0f));
        const float hw = cfg.f("reef.half_width", 3.2f);
        s.reefCount = cfg.i("reef.use", 1) ? 2 : 0;
        for (int k = 0; k < s.reefCount; ++k) {
            const float sg = k == 0 ? -1.0f : 1.0f;
            s.reefs[k] = {make_float2(sg * ra.x, ra.z), make_float2(sg * rb.x + (k ? 1.5f : -0.5f), rb.z + (k ? 4.0f : 0.0f)), ra.y, rb.y, hw * (k ? 0.9f : 1.1f)};
        }
    }
    // the boulders: fallen from the cliff, rolled down the slope, half in the sand; fewer and smaller in the cove, none
    // in the pier's way
    std::mt19937 rng(s.seed * 977u + 13u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const int n = cfg.i("island.boulders", 320);
    // the pier's footprint (walkway and head, with room): no boulder there
    float3 pm = make_float3(-2.0f, 0.0f, 8.0f), pM = make_float3(2.0f, 0.0f, 31.0f);
    {
        const std::vector<float> w = koyList(cfg, "pier.walk"), h = koyList(cfg, "pier.head");
        if (w.size() >= 4) { pm = make_float3(w[0], 0.0f, std::min(w[1], w[3])); pM = make_float3(w[2], 0.0f, std::max(w[1], w[3])); }
        if (h.size() >= 4) { pm.x = std::min(pm.x, h[0]); pM.x = std::max(pM.x, h[2]); pm.z = std::min(pm.z, h[1]); pM.z = std::max(pM.z, h[3]); }
    }
    for (int k = 0, tries = 0; k < n && tries < n * 20; ++tries) {
        const float ang = (U(rng) * 2.0f - 1.0f) * kPi;
        const float ux = std::cos(ang), uz = std::sin(ang);
        const float cove = kn::smoothstep(std::cos(s.coveHalf * 1.6f), 1.0f, ux * s.coveDir.x + uz * s.coveDir.y);
        if (cove > 0.2f && U(rng) < 0.85f) continue;
        const float u = U(rng);
        const float r = (0.45f + 1.8f * u * u * u) * (1.0f - 0.5f * cove);
        const float out = 0.5f + 7.0f * U(rng) * U(rng);
        const float rad = s.waterline(ux, uz) + out;
        const float x = s.cx + ux * rad, z = s.cz + uz * rad;
        if (x > pm.x - 2.5f && x < pM.x + 2.5f && z > pm.z - 3.0f && z < pM.z + 2.0f) continue;
        const float rock = -out * s.slope;   // the rock's surface out there
        const float base = std::max(floorY(x, z), rock);
        KoyIslandShape::Boulder B;
        B.c = make_float3(x, base + 0.15f * r, z);
        B.r = r;
        B.ang = ang;
        // nine planes: a flat bed below, eight faces about it at random azimuths and tilts, at random distances (a squat,
        // angular block, wider than tall)
        B.n[0] = make_float3(0.0f, -1.0f, 0.0f);
        B.d[0] = 0.45f * r;
        for (int f = 1; f < 9; ++f) {
            const float az = (float(f) + 0.6f * U(rng)) / 8.0f * 2.0f * kPi + ang, el = (U(rng) - 0.25f) * 1.1f;
            B.n[f] = normalize(make_float3(std::cos(az) * std::cos(el), std::sin(el) + (f == 8 ? 1.5f : 0.0f), std::sin(az) * std::cos(el)));
            B.d[f] = r * (0.65f + 0.45f * U(rng)) * (B.n[f].y > 0.5f ? 0.7f : 1.0f);
        }
        s.boulders.push_back(B);
        ++k;
    }
    // blocks along the reef arms' flanks and beyond their seaward ends
    for (int k = 0; k < s.reefCount; ++k)
        for (int b = 0, tries = 0; b < 40 && tries < 400; ++tries) {
            const KoyIslandShape::Reef& R = s.reefs[k];
            const float u = 0.15f + 1.05f * U(rng), side = U(rng) < 0.5f ? -1.0f : 1.0f;
            const float2 ab = make_float2(R.b.x - R.a.x, R.b.y - R.a.y);
            const float L = std::sqrt(ab.x * ab.x + ab.y * ab.y);
            const float off = side * (R.halfW * (u > 1.0f ? 0.3f : 0.9f) + 2.5f * U(rng));
            const float x = R.a.x + ab.x * u - ab.y / L * off, z = R.a.y + ab.y * u + ab.x / L * off;
            if (x > pm.x - 2.5f && x < pM.x + 2.5f) continue;
            const float uu = U(rng);
            const float r = 0.4f + 1.3f * uu * uu;
            KoyIslandShape::Boulder B;
            B.c = make_float3(x, floorY(x, z) + 0.15f * r, z);
            B.r = r;
            B.ang = std::atan2(z - s.cz, x - s.cx);
            B.n[0] = make_float3(0.0f, -1.0f, 0.0f);
            B.d[0] = 0.45f * r;
            for (int f = 1; f < 9; ++f) {
                const float az = (float(f) + 0.6f * U(rng)) / 8.0f * 2.0f * kPi + B.ang, el = (U(rng) - 0.25f) * 1.1f;
                B.n[f] = normalize(make_float3(std::cos(az) * std::cos(el), std::sin(el) + (f == 8 ? 1.5f : 0.0f), std::sin(az) * std::cos(el)));
                B.d[f] = r * (0.65f + 0.45f * U(rng)) * (B.n[f].y > 0.5f ? 0.7f : 1.0f);
            }
            s.boulders.push_back(B);
            ++b;
        }
    s.bins.assign(180, {});
    for (size_t i = 0; i < s.boulders.size(); ++i) {
        const KoyIslandShape::Boulder& B = s.boulders[i];
        const float rad = std::sqrt((B.c.x - s.cx) * (B.c.x - s.cx) + (B.c.z - s.cz) * (B.c.z - s.cz));
        const float reach = std::asin(std::min(1.0f, (B.r * 1.6f + 1.2f) / std::max(rad, 1.0f)));
        const int b0 = int(std::floor((B.ang - reach + kPi) / (2.0f * kPi) * 180.0f)), b1 = int(std::floor((B.ang + reach + kPi) / (2.0f * kPi) * 180.0f));
        for (int b = b0; b <= b1; ++b) s.bins[size_t(((b % 180) + 180) % 180)].push_back(int(i));
    }
    return s;
}

void koyBuildIsland(const KoyIslandShape& s, float cell, unsigned int matWet, unsigned int matDry, unsigned int id, std::vector<KMeshCPU>& out,
                    std::string* log) {
    KoySdfBox box;
    const float ext = s.R0 * 1.25f + 14.0f;
    box.lo = make_float3(s.cx - ext, -9.0f, std::min(s.cz - ext, -16.0f));   // the reef arms reach out past the pier's end
    box.hi = make_float3(s.cx + ext, s.H + 3.0f, s.cz + ext);
    box.cell = cell;
    box.slope = 3.2f;    // the cliff and the warp make the height-field distance steeper than a true one
    box.reach = 1.6f;
    KMeshCPU all = koyBuildSdfMesh([&](float3 p) { return s.sdf(p); }, box, "ada", matWet, id, log);
    // cut at the waterline: a triangle whose every corner is above the splash goes dry
    KMeshCPU wet, dry;
    wet.name = "ada (islak)";
    dry.name = "ada (kuru)";
    wet.material = matWet;
    dry.material = matDry;
    wet.id = dry.id = id;
    std::vector<int> mapW(all.pos.size(), -1), mapD(all.pos.size(), -1);
    auto take = [&](KMeshCPU& m, std::vector<int>& map, unsigned v) {
        if (map[v] < 0) {
            map[v] = int(m.pos.size());
            m.pos.push_back(all.pos[v]);
            m.nrm.push_back(all.nrm[v]);
        }
        return unsigned(map[v]);
    };
    for (const uint3& t : all.idx) {
        const bool isDry = all.pos[t.x].y > 0.35f && all.pos[t.y].y > 0.35f && all.pos[t.z].y > 0.35f;
        KMeshCPU& m = isDry ? dry : wet;
        std::vector<int>& map = isDry ? mapD : mapW;
        m.idx.push_back(make_uint3(take(m, map, t.x), take(m, map, t.y), take(m, map, t.z)));
    }
    out.push_back(std::move(wet));
    out.push_back(std::move(dry));
}

// ---------------------------------------------------------------------------------------------------
// the pier
namespace {
// an oriented box (axes ax, ay, az unit, half sizes h), faces subdivided every `seg` metres (the baked occlusion lives in
// the vertices); uv in texture tiles of `tile` metres, the grain along ax, offset per board
void addBox(KMeshCPU& m, float3 c, float3 ax, float3 ay, float3 az, float3 h, float tile, float2 uvOff, float seg = 0.3f) {
    const float3 A[3] = {ax, ay, az};
    const float H[3] = {h.x, h.y, h.z};
    for (int f = 0; f < 6; ++f) {
        const int n = f / 2;                 // the face's normal axis
        const float sgn = (f & 1) ? 1.0f : -1.0f;
        const int u = n == 0 ? 1 : 0, v = n == 2 ? 1 : 2;   // its two in-plane axes (the grain axis 0 runs along u where it can)
        const float3 N = A[n] * sgn;
        const int nu = std::max(1, int(std::ceil(2.0f * H[u] / seg))), nv = std::max(1, int(std::ceil(2.0f * H[v] / seg)));
        const unsigned base = unsigned(m.pos.size());
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i <= nu; ++i) {
                const float su = -H[u] + 2.0f * H[u] * i / nu, sv = -H[v] + 2.0f * H[v] * j / nv;
                const float3 p = c + N * H[n] + A[u] * su + A[v] * sv;
                m.pos.push_back(p);
                m.nrm.push_back(N);
                // the grain along axis 0: on faces that contain it, u texture = along it
                float tu, tv;
                if (u == 0) { tu = su; tv = sv; } else if (v == 0) { tu = sv; tv = su; } else { tu = su; tv = sv; }
                m.uv.push_back(make_float2(tu / tile + uvOff.x, tv / tile + uvOff.y));
            }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const unsigned a = base + unsigned(j * (nu + 1) + i), b = a + 1, cc = a + unsigned(nu + 1), d = cc + 1;
                // wound so the face looks along N
                if (dot(cross(A[u], A[v]), N) > 0.0f) {
                    m.idx.push_back(make_uint3(a, b, d));
                    m.idx.push_back(make_uint3(a, d, cc));
                } else {
                    m.idx.push_back(make_uint3(a, d, b));
                    m.idx.push_back(make_uint3(a, cc, d));
                }
            }
    }
}
// a cylinder from p0 to p1 (radius r, `around` segments, rings every `seg` metres, capped); uv: u around in tiles, v
// along; `wobble`: the radius roughened by noise (cast concrete is never round)
void addCylinder(KMeshCPU& m, float3 p0, float3 p1, float r, int around, float seg, float tile, float2 uvOff, float wobble = 0.0f) {
    const float3 axis = p1 - p0;
    const float len = length(axis);
    const float3 w = axis / len;
    const float3 helper = std::fabs(w.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
    const float3 e1 = normalize(cross(helper, w)), e2 = cross(w, e1);
    const int rings = std::max(1, int(std::ceil(len / seg)));
    const unsigned base = unsigned(m.pos.size());
    for (int j = 0; j <= rings; ++j)
        for (int i = 0; i <= around; ++i) {
            const float a = 2.0f * kPi * float(i) / around, t = float(j) / rings;
            const float3 dir = e1 * std::cos(a) + e2 * std::sin(a);
            const float3 c = p0 + axis * t;
            const float rr = r * (1.0f + wobble * kn::fbm3(c.x * 3.0f + dir.x * 1.5f, c.y * 3.0f + dir.y * 1.5f, c.z * 3.0f + dir.z * 1.5f, 3));
            m.pos.push_back(c + dir * rr);
            m.nrm.push_back(dir);
            m.uv.push_back(make_float2(a * r / tile + uvOff.x, t * len / tile + uvOff.y));
        }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < around; ++i) {
            const unsigned a = base + unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
            m.idx.push_back(make_uint3(a, b, d));
            m.idx.push_back(make_uint3(a, d, c));
        }
    for (int end = 0; end < 2; ++end) {   // caps
        const float3 c = end ? p1 : p0, n = end ? w : w * -1.0f;
        const unsigned cidx = unsigned(m.pos.size());
        m.pos.push_back(c);
        m.nrm.push_back(n);
        m.uv.push_back(uvOff);
        const unsigned first = unsigned(m.pos.size());
        for (int i = 0; i <= around; ++i) {
            const float a = 2.0f * kPi * float(i) / around;
            const float3 dir = e1 * std::cos(a) + e2 * std::sin(a);
            m.pos.push_back(c + dir * r);
            m.nrm.push_back(n);
            m.uv.push_back(make_float2(std::cos(a) * r / tile + uvOff.x, std::sin(a) * r / tile + uvOff.y));
        }
        for (int i = 0; i < around; ++i) {
            if (end) m.idx.push_back(make_uint3(cidx, first + unsigned(i), first + unsigned(i + 1)));
            else m.idx.push_back(make_uint3(cidx, first + unsigned(i + 1), first + unsigned(i)));
        }
    }
}
float3 rotY(float3 v, float a) { return make_float3(std::cos(a) * v.x + std::sin(a) * v.z, v.y, -std::sin(a) * v.x + std::cos(a) * v.z); }

// A pile's wet part, fouled: the concrete's radius plus a lumpy crust (mussels in a thick band just under the surface,
// algae and sponges lower, thinner by the sand where it scours), normals from the displaced surface itself
void addFouledPile(KMeshCPU& m, float x, float z, float y0, float y1, float r0, unsigned seed, float2 uvOff) {
    const int around = 44;
    const float ring = 0.055f;
    const int rings = std::max(2, int(std::ceil((y1 - y0) / ring)));
    const float o = float(seed % 997) * 3.71f;
    auto crust = [&](float a, float y) {
        float th = 0.05f;                                                        // algae and sponges
        th += 0.06f * kn::smoothstep(0.25f, -0.25f, y) * kn::smoothstep(-1.9f, -0.6f, y);   // the mussel band
        th *= kn::smoothstep(y0 - 0.05f, y0 + 0.5f, y);                          // scoured by the sand
        th *= kn::smoothstep(0.3f, 0.05f, y);                                    // nothing above the splash
        const float cx = std::cos(a) * 1.4f, cz = std::sin(a) * 1.4f;
        const float lumps = 0.55f + 0.9f * kn::fbm3(cx * 4.0f + o, y * 9.0f, cz * 4.0f, 3) + 0.35f * kn::noise3(cx * 14.0f, y * 22.0f + o, cz * 14.0f);
        return th * std::max(lumps, 0.0f);
    };
    const unsigned base = unsigned(m.pos.size());
    for (int j = 0; j <= rings; ++j)
        for (int i = 0; i <= around; ++i) {
            const float a = 2.0f * kPi * float(i % around) / around, y = y0 + (y1 - y0) * float(j) / rings;
            const float r = r0 * (1.0f + 0.02f * kn::noise3(std::cos(a) * 2.0f, y * 3.0f, std::sin(a) * 2.0f + o)) + crust(a, y);
            m.pos.push_back(make_float3(x + std::cos(a) * r, y, z + std::sin(a) * r));
            m.nrm.push_back(make_float3(0, 0, 0));
            m.uv.push_back(make_float2(a * r0 / 2.7f + uvOff.x, (y - y0) / 2.7f + uvOff.y));
        }
    const size_t first = m.idx.size();
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < around; ++i) {
            const unsigned a = base + unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
            m.idx.push_back(make_uint3(a, d, b));
            m.idx.push_back(make_uint3(a, c, d));
        }
    for (size_t t = first; t < m.idx.size(); ++t) {   // area-weighted normals, outward
        const uint3 tr = m.idx[t];
        const float3 n = cross(m.pos[tr.y] - m.pos[tr.x], m.pos[tr.z] - m.pos[tr.x]);
        m.nrm[tr.x] += n;
        m.nrm[tr.y] += n;
        m.nrm[tr.z] += n;
    }
    for (int j = 0; j <= rings; ++j) {   // the seam: both copies the same normal
        const size_t a0 = base + size_t(j) * (around + 1), a1 = a0 + around;
        const float3 n = m.nrm[a0] + m.nrm[a1];
        m.nrm[a0] = m.nrm[a1] = n;
    }
    for (size_t v = base; v < m.pos.size(); ++v) {
        float3 n = m.nrm[v];
        const float L = length(n);
        n = L > 1e-12f ? n / L : make_float3(0, 1, 0);
        const float3 out = make_float3(m.pos[v].x - x, 0.0f, m.pos[v].z - z);
        if (dot(n, out) < 0.0f) n = n * -1.0f;
        m.nrm[v] = n;
    }
}
}   // namespace

KoyPierSpec koyPierSpec(const Config& cfg) {
    KoyPierSpec p;
    p.deck = cfg.f("pier.deck", 1.1f);
    p.pileR = cfg.f("pier.pile_r", 0.25f);
    p.gap = cfg.f("pier.gap", 0.025f);
    p.missing = cfg.f("pier.missing", 0.05f);
    p.ladder = cfg.v3("pier.ladder", make_float3(2.6f, 0.0f, -2.0f));
    p.headHoles = cfg.i("pier.head_holes", 8);
    auto lin = [](float a, float b, int n) {
        std::vector<float> v;
        for (int k = 0; k < n; ++k) v.push_back(n == 1 ? 0.5f * (a + b) : a + (b - a) * float(k) / float(n - 1));
        return v;
    };
    {   // the walkway from the beach
        const std::vector<float> w = koyList(cfg, "pier.walk");   // x0 z0 x1 z1
        KoyPierSection S{-2.0f, 2.0f, 8.0f, 31.0f, {}, {}};
        if (w.size() >= 4) S = {w[0], w[2], std::min(w[1], w[3]), std::max(w[1], w[3]), {}, {}};
        S.pileX = {S.x0 + 0.4f, S.x1 - 0.4f};
        for (float z = S.z0 + 1.0f; z < S.z1 - 2.5f; z += 3.0f) S.pileZ.push_back(z);
        p.sections.push_back(S);
    }
    {   // the head
        const std::vector<float> h = koyList(cfg, "pier.head");
        KoyPierSection S{-8.0f, 8.0f, -2.0f, 8.0f, {}, {}};
        if (h.size() >= 4) S = {h[0], h[2], h[1], h[3], {}, {}};
        S.pileX = lin(S.x0 + 1.0f, S.x1 - 1.0f, cfg.i("pier.head_cols", 7));
        S.pileZ = lin(S.z0 + 0.8f, S.z1 - 0.8f, cfg.i("pier.head_rows", 5));
        S.concrete = cfg.i("pier.head_concrete", 1) != 0;
        p.sections.push_back(S);
    }
    return p;
}

void koyBuildPier(const KoyPierSpec& p, const std::function<float(float, float)>& floorY, unsigned int matWood, unsigned int matWet,
                  unsigned int matDry, unsigned int matSteel, unsigned int matRust, unsigned int id, std::vector<KMeshCPU>& out, std::string* log) {
    KMeshCPU wood, wet, dry, steel, rust;
    rust.name = "iskele (pasli demir)";
    rust.material = matRust;
    rust.id = id;
    wood.name = "iskele (ahsap)";
    wet.name = "iskele (islak beton)";
    dry.name = "iskele (kuru beton)";
    steel.name = "iskele (merdiven)";
    wood.material = matWood;
    wet.material = matWet;
    dry.material = matDry;
    steel.material = matSteel;
    wood.id = wet.id = dry.id = steel.id = id;
    std::mt19937 rng(41u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float3 X = make_float3(1, 0, 0), Y = make_float3(0, 1, 0), Z = make_float3(0, 0, 1);
    const float plankBottom = p.deck - p.plankT, joistH = 0.15f, joistBottom = plankBottom - joistH;
    const float strH = 0.22f, strBottom = joistBottom - strH, capH = 0.3f, capBottom = strBottom - capH;
    const float splash = 0.25f;   // the concrete is fouled below this
    int piles = 0, planks = 0, gone = 0;
    for (size_t si = 0; si < p.sections.size(); ++si) {
        const KoyPierSection& S = p.sections[si];
        // piles: fouled below the splash, clean concrete above to the cap
        const float top = S.concrete ? p.deck - 0.3f - 0.4f + 0.02f : capBottom + 0.02f;   // the slab's beams, or the caps
        for (float pz : S.pileZ)
            for (float px : S.pileX) {
                const float x = px + (U(rng) - 0.5f) * 0.05f, z = pz + (U(rng) - 0.5f) * 0.05f;
                const float ground = floorY(x, z);
                if (ground > top - 0.32f) continue;   // on the beach: the walkway rests on the sand there
                const float bottom = ground - 0.6f;
                const float2 off = make_float2(U(rng) * 7.0f, U(rng) * 7.0f);
                if (splash > bottom) addFouledPile(wet, x, z, bottom, splash + 0.06f, p.pileR, unsigned(piles * 7919 + 13), off);
                if (top > splash)
                    addCylinder(dry, make_float3(x, std::max(splash, bottom), z), make_float3(x, top, z), p.pileR, 32, 0.2f, 2.7f,
                                make_float2(off.x, off.y + (splash - bottom) / 2.7f), 0.02f);
                ++piles;
            }
        if (S.concrete) {
            // beams both ways on the piles (a coffered ceiling), the slab over them
            const float slabT = 0.3f, beamD = 0.4f, slabBottom = p.deck - slabT, beamBottom = slabBottom - beamD;
            for (float pz : S.pileZ)
                addBox(dry, make_float3(0.5f * (S.x0 + S.x1), beamBottom + 0.5f * beamD, pz), X, Y, Z,
                       make_float3(0.5f * (S.x1 - S.x0) - 0.02f, 0.5f * beamD, 0.22f), 2.7f, make_float2(U(rng) * 5.0f, U(rng) * 5.0f), 0.3f);
            for (float px : S.pileX)
                addBox(dry, make_float3(px, beamBottom + 0.5f * beamD - 0.01f, 0.5f * (S.z0 + S.z1)), Z, Y, X * -1.0f,
                       make_float3(0.5f * (S.z1 - S.z0) - 0.02f, 0.5f * beamD - 0.01f, 0.2f), 2.7f, make_float2(U(rng) * 5.0f, U(rng) * 5.0f), 0.3f);
            // the slab: cells of 10 cm; a few holes broken through it (overlapping noisy ellipses, never over a beam's
            // middle), their edges jagged; top and bottom faces per cell, walls where a cell meets a hole or the edge
            const float cell = 0.1f;
            const int nx = int(std::ceil((S.x1 - S.x0) / cell)), nz = int(std::ceil((S.z1 - S.z0) / cell));
            struct Blob { float x, z, a, b, ang; };
            std::vector<Blob> blobs;
            // between beams: a bay's middle, so the slab breaks where it spans. Each hole a bay of its own, the bays taken
            // spread out (Ozcan 2026-10-04: the hall's middle, far from every hole, went pitch black): farthest-first
            // from the ones already broken, the first at random
            const int bnx = int(S.pileX.size()) - 1, bnz = int(S.pileZ.size()) - 1;
            const int nHoles = std::min(p.headHoles, bnx * bnz);
            std::vector<std::pair<int, int>> bays;
            for (int k = 0; k < nHoles; ++k) {
                int bx = int(U(rng) * float(bnx)), bz = int(U(rng) * float(bnz));
                if (k > 0) {
                    float best = -1.0f;
                    for (int ix = 0; ix < bnx; ++ix)
                        for (int iz = 0; iz < bnz; ++iz) {
                            float dmin = 1e9f;
                            for (const auto& b : bays) dmin = std::min(dmin, float((ix - b.first) * (ix - b.first) + (iz - b.second) * (iz - b.second)));
                            const float score = dmin + 0.6f * U(rng);   // a little chance: not a checkerboard
                            if (score > best) { best = score; bx = ix; bz = iz; }
                        }
                }
                bays.push_back({bx, bz});
                const float cx = 0.5f * (S.pileX[size_t(bx)] + S.pileX[size_t(bx) + 1]) + (U(rng) - 0.5f) * 1.0f;
                const float cz = 0.5f * (S.pileZ[size_t(bz)] + S.pileZ[size_t(bz) + 1]) + (U(rng) - 0.5f) * 1.0f;
                const int parts = 2 + int(U(rng) * 2.0f);
                for (int q = 0; q < parts; ++q)
                    blobs.push_back({cx + (U(rng) - 0.5f) * 0.8f, cz + (U(rng) - 0.5f) * 0.8f, 0.25f + 0.45f * U(rng), 0.2f + 0.35f * U(rng), U(rng) * kPi});
                std::printf("koy iskele basligi: delik (%.1f, %.1f)\n", cx, cz);
            }
            auto holeAt = [&](float x, float z) {
                for (const Blob& B : blobs) {
                    const float dx = x - B.x, dz = z - B.z, c = std::cos(B.ang), sn = std::sin(B.ang);
                    const float u = (c * dx + sn * dz) / B.a, v = (-sn * dx + c * dz) / B.b;
                    if (u * u + v * v < 1.0f + 0.35f * kn::noise3(x * 6.0f, 1.3f, z * 6.0f)) return true;
                }
                return false;
            };
            std::vector<unsigned char> solid(size_t(nx) * nz);
            for (int j = 0; j < nz; ++j)
                for (int i = 0; i < nx; ++i) solid[size_t(j) * nx + i] = holeAt(S.x0 + (i + 0.5f) * cell, S.z0 + (j + 0.5f) * cell) ? 0 : 1;
            auto isSolid = [&](int i, int j) { return i >= 0 && j >= 0 && i < nx && j < nz && solid[size_t(j) * nx + i]; };
            auto quad = [&](float3 a, float3 b, float3 c, float3 d, float3 n, float2 ua, float2 ub, float2 uc, float2 ud) {
                const unsigned base = unsigned(dry.pos.size());
                for (const float3& q : {a, b, c, d}) { dry.pos.push_back(q); dry.nrm.push_back(n); }
                for (const float2& q : {ua, ub, uc, ud}) dry.uv.push_back(q);
                if (dot(cross(b - a, c - a), n) > 0.0f) {
                    dry.idx.push_back(make_uint3(base, base + 1, base + 2));
                    dry.idx.push_back(make_uint3(base, base + 2, base + 3));
                } else {
                    dry.idx.push_back(make_uint3(base, base + 2, base + 1));
                    dry.idx.push_back(make_uint3(base, base + 3, base + 2));
                }
            };
            const float T = 1.0f / 2.7f;
            int rebars = 0;
            for (int j = 0; j < nz; ++j)
                for (int i = 0; i < nx; ++i) {
                    if (!isSolid(i, j)) continue;
                    const float xa = S.x0 + i * cell, xb = xa + cell, za = S.z0 + j * cell, zb = za + cell;
                    // a broken edge cell is thinned a little from below (spalled concrete)
                    const bool edge = !isSolid(i - 1, j) || !isSolid(i + 1, j) || !isSolid(i, j - 1) || !isSolid(i, j + 1);
                    const float bot = slabBottom + (edge ? 0.04f + 0.1f * kn::hash01(i, j, 7) : 0.0f);
                    quad(make_float3(xa, p.deck, za), make_float3(xb, p.deck, za), make_float3(xb, p.deck, zb), make_float3(xa, p.deck, zb), Y,
                         make_float2(xa * T, za * T), make_float2(xb * T, za * T), make_float2(xb * T, zb * T), make_float2(xa * T, zb * T));
                    quad(make_float3(xa, bot, za), make_float3(xa, bot, zb), make_float3(xb, bot, zb), make_float3(xb, bot, za), Y * -1.0f,
                         make_float2(xa * T, za * T), make_float2(xa * T, zb * T), make_float2(xb * T, zb * T), make_float2(xb * T, za * T));
                    // walls towards holes and the slab's own edge
                    const int di[4] = {-1, 1, 0, 0}, dj[4] = {0, 0, -1, 1};
                    for (int e = 0; e < 4; ++e) {
                        if (isSolid(i + di[e], j + dj[e])) continue;
                        const float3 n = make_float3(float(di[e]), 0.0f, float(dj[e]));
                        const float x0 = di[e] < 0 ? xa : di[e] > 0 ? xb : xa, x1 = di[e] < 0 ? xa : di[e] > 0 ? xb : xb;
                        const float z0 = dj[e] < 0 ? za : dj[e] > 0 ? zb : za, z1 = dj[e] < 0 ? za : dj[e] > 0 ? zb : zb;
                        quad(make_float3(x0, bot, z0), make_float3(x1, bot, z1), make_float3(x1, p.deck, z1), make_float3(x0, p.deck, z0), n,
                             make_float2((x0 + z0) * T, bot * T), make_float2((x1 + z1) * T, bot * T), make_float2((x1 + z1) * T, p.deck * T),
                             make_float2((x0 + z0) * T, p.deck * T));
                        // a reinforcing bar where the edge is a hole's (not the slab's outer edge), every ~40 cm: rusted,
                        // sticking out into the hole, sagging
                        const bool inner = i + di[e] >= 0 && j + dj[e] >= 0 && i + di[e] < nx && j + dj[e] < nz;
                        if (inner && kn::hash01(i * 3 + e, j, 19) < 0.25f) {
                            const float len = 0.06f + 0.3f * kn::hash01(i, j * 5 + e, 23);
                            const float3 a0 = make_float3(0.5f * (x0 + x1), slabBottom + 0.07f + 0.12f * kn::hash01(i, j, 29), 0.5f * (z0 + z1));
                            const float3 a1 = a0 + n * len + make_float3(0.0f, -0.35f * len * len - 0.02f, 0.0f);
                            addCylinder(rust, a0 - n * 0.03f, a1, 0.006f, 6, 0.1f, 0.5f, make_float2(0, 0));
                            ++rebars;
                        }
                    }
                }
            // what fell through: chunks of the slab on the sand under each hole, half sunk, fouled
            int chunks = 0;
            for (const Blob& B : blobs)
                for (int k = 0; k < 3; ++k) {
                    const float x = B.x + (U(rng) - 0.5f) * 1.4f * B.a, z = B.z + (U(rng) - 0.5f) * 1.4f * B.b;
                    const float sz = 0.08f + 0.22f * U(rng);
                    const float g = floorY(x, z);
                    const float yaw = U(rng) * kPi, tilt = (U(rng) - 0.5f) * 0.8f;
                    const float3 ax = normalize(make_float3(std::cos(yaw), tilt, std::sin(yaw)));
                    const float3 az0 = make_float3(-std::sin(yaw), 0.0f, std::cos(yaw));
                    const float3 ay = normalize(cross(az0, ax));
                    const float3 az = cross(ax, ay);
                    addBox(wet, make_float3(x, g + 0.3f * sz, z), ax, ay, az, make_float3(sz * (1.0f + U(rng)), 0.5f * slabT * (0.6f + 0.4f * U(rng)), sz),
                           2.7f, make_float2(U(rng) * 5.0f, U(rng) * 5.0f), 0.15f);
                    ++chunks;
                }
            std::printf("koy iskele basligi: beton plaka %d x %d hucre, %d delik, %d filiz, %d moloz\n", nx, nz, nHoles, rebars, chunks);
            continue;
        }
        // caps across each row (concrete), stringers along each column (timber)
        for (float pz : S.pileZ)
            addBox(dry, make_float3(0.5f * (S.x0 + S.x1), capBottom + 0.5f * capH, pz), X, Y, Z, make_float3(0.5f * (S.x1 - S.x0) + 0.05f, 0.5f * capH, 0.18f),
                   2.7f, make_float2(U(rng) * 5.0f, U(rng) * 5.0f), 0.25f);
        for (float px : S.pileX)
            addBox(wood, make_float3(px, strBottom + 0.5f * strH, 0.5f * (S.z0 + S.z1)), Z, Y, X * -1.0f,
                   make_float3(0.5f * (S.z1 - S.z0) + 0.05f, 0.5f * strH, 0.09f), 0.5f, make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.4f);
        for (float z = S.z0 + 0.15f; z < S.z1 - 0.05f; z += 0.7f)   // joists across
            addBox(wood, make_float3(0.5f * (S.x0 + S.x1), joistBottom + 0.5f * joistH, z + (U(rng) - 0.5f) * 0.03f), X, Y, Z,
                   make_float3(0.5f * (S.x1 - S.x0) - 0.03f, 0.5f * joistH, 0.04f), 0.5f, make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.3f);
        // a few runs of boards gone (rotted through): their x ranges and z spans
        struct Hole { float xa, xb, za, zb; };
        std::vector<Hole> holes;
        const int nh = si == 0 ? 2 : 4;
        for (int k = 0; k < nh; ++k) {
            const float cx = S.x0 + 0.6f + (S.x1 - S.x0 - 1.2f) * U(rng), cz = S.z0 + 1.0f + (S.z1 - S.z0 - 2.0f) * U(rng);
            const float hw = 0.12f + 0.25f * U(rng), hl = 0.4f + 0.9f * U(rng);
            holes.push_back({cx - hw, cx + hw, cz - hl, cz + hl});
        }
        // the boards along z: lengths of 3.5-4.5 m with staggered butt joints, each a little different
        for (float x = S.x0; x < S.x1 - 0.05f;) {
            const float w = p.plankW * (0.94f + 0.12f * U(rng));
            const float cx = x + 0.5f * w;
            x += w + p.gap * (0.6f + 0.8f * U(rng));
            for (float z = S.z0 - 1.5f * U(rng); z < S.z1;) {
                const float len = 3.5f + U(rng);
                float za = std::max(z, S.z0), zb = std::min(z + len, S.z1);
                z += len + 0.006f;
                if (zb - za < 0.3f) continue;
                if (U(rng) < p.missing) { ++gone; continue; }
                // cut by the holes: what is left either side of a hole stays
                std::vector<float2> parts{make_float2(za, zb)};
                for (const Hole& H : holes) {
                    if (cx < H.xa || cx > H.xb) continue;
                    std::vector<float2> next;
                    for (float2 q : parts) {
                        if (H.zb <= q.x || H.za >= q.y) { next.push_back(q); continue; }
                        if (H.za - q.x > 0.25f) next.push_back(make_float2(q.x, H.za - 0.03f * U(rng)));
                        if (q.y - H.zb > 0.25f) next.push_back(make_float2(H.zb + 0.03f * U(rng), q.y));
                    }
                    parts.swap(next);
                }
                for (float2 q : parts) {
                    const float yaw = (U(rng) - 0.5f) * 0.006f, roll = (U(rng) - 0.5f) * 0.012f;
                    const float3 az0 = rotY(Z, yaw), ax0 = rotY(X, yaw);
                    const float3 ay = normalize(Y + ax0 * roll);
                    const float3 ax = normalize(cross(ay, az0));
                    addBox(wood, make_float3(cx + 0.004f * (U(rng) - 0.5f), plankBottom + 0.5f * p.plankT - 0.003f * U(rng), 0.5f * (q.x + q.y)), az0, ay,
                           ax * -1.0f, make_float3(0.5f * (q.y - q.x), 0.5f * p.plankT, 0.5f * w), 0.5f, make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.35f);
                    ++planks;
                }
            }
        }
    }
    // the railing: posts and two rails along the outer edges (not where the walkway meets the head)
    {
        const KoyPierSection& W = p.sections[0];
        const KoyPierSection& H = p.sections.size() > 1 ? p.sections[1] : p.sections[0];
        auto railZ = [&](float x, float za, float zb) {   // along z at x
            for (float z = za + 0.1f; z < zb - 0.1f; z += 2.0f)
                addBox(wood, make_float3(x, p.deck + 0.5f, z), Y, X, Z, make_float3(0.5f, 0.045f, 0.045f), 0.5f, make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.3f);
            for (float hh : {0.55f, 0.98f})
                addBox(wood, make_float3(x, p.deck + hh, 0.5f * (za + zb)), Z, Y, X, make_float3(0.5f * (zb - za), 0.03f, 0.05f), 0.5f,
                       make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.4f);
        };
        auto railX = [&](float z, float xa, float xb) {   // along x at z
            for (float x = xa + 0.1f; x < xb - 0.1f; x += 2.0f)
                addBox(wood, make_float3(x, p.deck + 0.5f, z), Y, X, Z, make_float3(0.5f, 0.045f, 0.045f), 0.5f, make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.3f);
            for (float hh : {0.55f, 0.98f})
                addBox(wood, make_float3(0.5f * (xa + xb), p.deck + hh, z), X, Y, Z, make_float3(0.5f * (xb - xa), 0.03f, 0.05f), 0.5f,
                       make_float2(U(rng) * 9.0f, U(rng) * 9.0f), 0.4f);
        };
        railZ(W.x0 + 0.05f, std::max(W.z0, H.z1), W.z1 - 0.5f);
        railZ(W.x1 - 0.05f, std::max(W.z0, H.z1), W.z1 - 0.5f);
        if (p.sections.size() > 1) {
            railZ(H.x0 + 0.05f, H.z0, H.z1);
            railZ(H.x1 - 0.05f, H.z0, H.z1);
            railX(H.z1 - 0.05f, H.x0, W.x0);
            railX(H.z1 - 0.05f, W.x1, H.x1);
            railX(H.z0 + 0.05f, H.x0, p.ladder.x - 0.5f);   // open where the ladder comes up
            railX(H.z0 + 0.05f, p.ladder.x + 0.5f, H.x1);
        }
    }
    // the ladder on the head's seaward edge: two stainless rails from over the deck down into the water, rungs every 0.3 m
    {
        const float xc = p.ladder.x, z = (p.sections.size() > 1 ? p.sections[1].z0 : p.ladder.z) - 0.12f, half = 0.24f, top = p.deck + 0.9f, foot = -1.9f;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const float x = xc + sgn * half;
            addCylinder(steel, make_float3(x, foot, z), make_float3(x, top, z), 0.021f, 12, 0.25f, 1.0f, make_float2(0, 0));
            addCylinder(steel, make_float3(x, top, z), make_float3(x, top, z + 0.35f), 0.021f, 12, 0.25f, 1.0f, make_float2(0, 0));
            addCylinder(steel, make_float3(x, top, z + 0.35f), make_float3(x, p.deck, z + 0.35f), 0.021f, 12, 0.25f, 1.0f, make_float2(0, 0));
        }
        for (float y = foot + 0.15f; y < p.deck - 0.1f; y += 0.3f)
            addCylinder(steel, make_float3(xc - half, y, z), make_float3(xc + half, y, z), 0.016f, 10, 0.25f, 1.0f, make_float2(0, 0));
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "iskele: %d ayak, %d tahta (%d eksik); %zu + %zu + %zu + %zu ucgen", piles, planks, gone, wood.idx.size(), wet.idx.size(),
                  dry.idx.size(), steel.idx.size());
    if (log) *log = buf;
    out.push_back(std::move(wood));
    out.push_back(std::move(wet));
    out.push_back(std::move(dry));
    out.push_back(std::move(steel));
    out.push_back(std::move(rust));
}
