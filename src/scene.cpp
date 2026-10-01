// "Altin saatte cam ve tas": the room, its objects and materials, generated from code.
#include "scene.h"
#include "shared/spectrum.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}
float Config::f(const std::string& key, float def) const {
    auto it = values.find(key);
    return it == values.end() ? def : std::stof(it->second);
}
float3 Config::v3(const std::string& key, float3 def) const {
    auto it = values.find(key);
    if (it == values.end()) return def;
    float3 r = def;
    std::sscanf(it->second.c_str(), "%f %f %f", &r.x, &r.y, &r.z);
    return r;
}

// ---------------------------------------------------------------------------------------------------
// Mesh building
// ---------------------------------------------------------------------------------------------------
void Scene::addTriangle(float3 a, float3 b, float3 c, int mat) {
    const float3 n = normalize(cross(b - a, c - a));
    const unsigned int base = unsigned(vertices.size());
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
    normals.push_back(n); normals.push_back(n); normals.push_back(n);
    indices.push_back(make_uint3(base, base + 1, base + 2));
    triMaterial.push_back(mat);
}
void Scene::addQuad(float3 a, float3 b, float3 c, float3 d, int mat) {
    addTriangle(a, b, c, mat);
    addTriangle(a, c, d, mat);
}
// Quad whose front must face n: reorders the corners when needed.
static void quadFacing(Scene& s, float3 a, float3 b, float3 c, float3 d, float3 n, int mat) {
    if (dot(cross(b - a, c - a), n) >= 0.0f) s.addQuad(a, b, c, d, mat);
    else s.addQuad(a, d, c, b, mat);
}
void Scene::addBox(float3 lo, float3 hi, int mat) {
    const float3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    quadFacing(*this, p[0], p[1], p[2], p[3], make_float3(0, 0, -1), mat);
    quadFacing(*this, p[4], p[5], p[6], p[7], make_float3(0, 0, 1), mat);
    quadFacing(*this, p[0], p[4], p[7], p[3], make_float3(-1, 0, 0), mat);
    quadFacing(*this, p[1], p[5], p[6], p[2], make_float3(1, 0, 0), mat);
    quadFacing(*this, p[0], p[1], p[5], p[4], make_float3(0, -1, 0), mat);
    quadFacing(*this, p[3], p[2], p[6], p[7], make_float3(0, 1, 0), mat);
}
void Scene::addSlab(float3 origin, float3 u, float3 v, float3 n, float w, float h, float thickness,
                    const std::vector<float4>& openings, int mat, int revealMat) {
    std::vector<float> us = {0.0f, w}, vs = {0.0f, h};
    for (const float4& o : openings) { us.push_back(o.x); us.push_back(o.z); vs.push_back(o.y); vs.push_back(o.w); }
    std::sort(us.begin(), us.end()); us.erase(std::unique(us.begin(), us.end()), us.end());
    std::sort(vs.begin(), vs.end()); vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
    const float3 back = n * (-thickness);
    auto P = [&](float a, float b) { return origin + u * a + v * b; };
    const int nu = int(us.size()) - 1, nv = int(vs.size()) - 1;
    std::vector<char> holes(size_t(nu) * nv, 0);
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            const float uc = 0.5f * (us[i] + us[i + 1]), vc = 0.5f * (vs[j] + vs[j + 1]);
            bool hole = false;
            for (const float4& o : openings) hole |= uc > o.x && uc < o.z && vc > o.y && vc < o.w;
            holes[size_t(i) * nv + j] = hole;
        }
    auto isHole = [&](int i, int j) { return holes[size_t(i) * nv + j] != 0; };
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            const float3 a = P(us[i], vs[j]), b = P(us[i + 1], vs[j]), c = P(us[i + 1], vs[j + 1]), d = P(us[i], vs[j + 1]);
            if (!isHole(i, j)) {
                quadFacing(*this, a, b, c, d, n, mat);
                quadFacing(*this, a + back, b + back, c + back, d + back, -n, mat);
                continue;
            }
            // reveals: where this hole cell borders solid slab, facing into the hole; none at the slab's edge
            if (j > 0 && !isHole(i, j - 1)) quadFacing(*this, a, b, b + back, a + back, v, revealMat);
            if (j + 1 < nv && !isHole(i, j + 1)) quadFacing(*this, d, c, c + back, d + back, -v, revealMat);
            if (i > 0 && !isHole(i - 1, j)) quadFacing(*this, a, d, d + back, a + back, u, revealMat);
            if (i + 1 < nu && !isHole(i + 1, j)) quadFacing(*this, b, c, c + back, b + back, -u, revealMat);
        }
    // perimeter
    const float3 c00 = P(0, 0), c10 = P(w, 0), c11 = P(w, h), c01 = P(0, h);
    quadFacing(*this, c00, c10, c10 + back, c00 + back, -v, mat);
    quadFacing(*this, c01, c11, c11 + back, c01 + back, v, mat);
    quadFacing(*this, c00, c01, c01 + back, c00 + back, -u, mat);
    quadFacing(*this, c10, c11, c11 + back, c10 + back, u, mat);
}

static void addPortal(Scene& s, float3 origin, float3 u, float3 v, float3 n, const float4& o, float depth) {
    Portal p{};
    p.corner = origin + u * o.x + v * o.y;
    p.edgeU = u * (o.z - o.x);
    p.edgeV = v * (o.w - o.y);
    p.normal = n;
    p.area = length(cross(p.edgeU, p.edgeV));
    s.portals.push_back(p);
    s.portalDepth.push_back(depth);
}

// Triangular prism, vertical axis, equilateral cross-section of side `side`.
static void addPrism(Scene& s, float3 base, float side, float height, float yawDeg, int mat) {
    const float r = side / std::sqrt(3.0f);
    float3 b[3], t[3];
    for (int k = 0; k < 3; ++k) {
        const float a = (yawDeg + 90.0f + 120.0f * k) * 3.14159265f / 180.0f;
        b[k] = base + make_float3(r * std::cos(a), 0.0f, r * std::sin(a));
        t[k] = b[k] + make_float3(0.0f, height, 0.0f);
    }
    const float3 c = base + make_float3(0, height * 0.5f, 0);
    auto face = [&](float3 p0, float3 p1, float3 p2, float3 p3) {
        const float3 m = (p0 + p1 + p2 + p3) * 0.25f;
        quadFacing(s, p0, p1, p2, p3, m - c, mat);
    };
    for (int k = 0; k < 3; ++k) face(b[k], b[(k + 1) % 3], t[(k + 1) % 3], t[k]);
    if (dot(cross(b[1] - b[0], b[2] - b[0]), make_float3(0, -1, 0)) >= 0) s.addTriangle(b[0], b[1], b[2], mat);
    else s.addTriangle(b[0], b[2], b[1], mat);
    if (dot(cross(t[1] - t[0], t[2] - t[0]), make_float3(0, 1, 0)) >= 0) s.addTriangle(t[0], t[1], t[2], mat);
    else s.addTriangle(t[0], t[2], t[1], mat);
}

// ---------------------------------------------------------------------------------------------------
// The room
// ---------------------------------------------------------------------------------------------------
static Material makeMat(int type, float3 albedoRGB, float roughness = 1.0f, float coat = 0.0f) {
    Material m{};
    m.type = type;
    m.sigA = fitSigmoid(albedoRGB);
    m.sigB = m.sigA;
    m.roughness = roughness;
    m.coat = coat;
    return m;
}

Scene buildScene(const Config& cfg) {
    Scene s;
    const float L = 6 * 1.82f, W = 4 * 1.82f, H = 6 * 0.91f, T = cfg.f("wall.thickness", 0.25f);

    // ---- materials
    Material plaster = makeMat(MAT_DIFFUSE, cfg.v3("mat.plaster", make_float3(0.80f, 0.795f, 0.78f)));
    plaster.p.x = 1.0f;
    const int mPlaster = s.addMaterial(plaster);
    Material concrete = makeMat(MAT_CONCRETE, cfg.v3("mat.concrete", make_float3(0.40f, 0.395f, 0.375f)), 0.35f, 0.25f);
    concrete.sigB = fitSigmoid(cfg.v3("mat.concrete2", make_float3(0.34f, 0.35f, 0.35f)));
    concrete.p = make_float4(1.82f, 0.91f, 0.0125f, 1.0f);
    const int mConcrete = s.addMaterial(concrete);
    Material stone = makeMat(MAT_STONE, cfg.v3("mat.floor", make_float3(0.52f, 0.51f, 0.49f)), cfg.f("mat.floor.rough", 0.045f), 1.0f);
    stone.sigB = fitSigmoid(cfg.v3("mat.chips", make_float3(0.74f, 0.73f, 0.70f)));
    stone.p = make_float4(0.75f, 0.018f, 0, 0);
    stone.flags = 1;   // brass divider strips
    const int mStone = s.addMaterial(stone);
    Material pool = makeMat(MAT_POOL, cfg.v3("mat.pool", make_float3(0.33f, 0.34f, 0.34f)), 0.25f, 0.4f);
    pool.sigB = fitSigmoid(make_float3(0.55f, 0.55f, 0.53f));
    pool.p = make_float4(0.6f, 0.012f, 0, 0);
    const int mPool = s.addMaterial(pool);
    Material water{};
    water.type = MAT_WATER;
    water.iorInside = IOR_WATER;
    water.iorOutside = IOR_AIR;
    water.p.x = cfg.f("water.ripple", 1.0f);
    const int mWater = s.addMaterial(water);
    Material sf11{};
    sf11.type = MAT_DIELECTRIC;
    sf11.iorInside = IOR_SF11;
    sf11.iorOutside = IOR_AIR;
    const int mPrism = s.addMaterial(sf11);
    Material globe = sf11;
    globe.iorInside = IOR_BK7;
    const int mGlobe = s.addMaterial(globe);
    Material globeWater = sf11;
    globeWater.iorInside = IOR_WATER;
    globeWater.iorOutside = IOR_BK7;
    const int mGlobeWater = s.addMaterial(globeWater);
    Material film{};
    film.type = MAT_FILM;
    film.p = make_float4(cfg.f("film.top", 60.0f), cfg.f("film.bottom", 900.0f), cfg.f("film.swirl", 260.0f), 0);
    const int mFilm = s.addMaterial(film);
    Material brass = makeMat(MAT_METAL, make_float3(0.91f, 0.78f, 0.42f), 0.08f);
    const int mBrass = s.addMaterial(brass);
    (void)mBrass;

    // ---- shell: floor with the pool cut out, ceiling with a skylight slot, four walls
    const float poolX0 = cfg.f("pool.x0", 1.82f), poolX1 = cfg.f("pool.x1", 9.10f);
    const float poolZ0 = cfg.f("pool.z0", 5.46f), poolZ1 = cfg.f("pool.z1", 7.28f - 0.30f);
    const float poolDepth = 0.24f, waterLevel = -0.035f;
    s.addSlab(make_float3(-T, 0, -T), make_float3(1, 0, 0), make_float3(0, 0, 1), make_float3(0, 1, 0), L + 2 * T, W + 2 * T, 0.3f,
              {make_float4(poolX0 + T, poolZ0 + T, poolX1 + T, poolZ1 + T)}, mStone, mPool);
    s.addQuad(make_float3(poolX0, -poolDepth, poolZ0), make_float3(poolX0, -poolDepth, poolZ1),
              make_float3(poolX1, -poolDepth, poolZ1), make_float3(poolX1, -poolDepth, poolZ0), mPool);
    s.addQuad(make_float3(poolX0, waterLevel, poolZ0), make_float3(poolX0, waterLevel, poolZ1),
              make_float3(poolX1, waterLevel, poolZ1), make_float3(poolX1, waterLevel, poolZ0), mWater);

    // ceiling: slot along the right wall
    const float slotW = cfg.f("skylight.width", 0.30f);
    const std::vector<float4> ceilOpen = {make_float4(T + 0.91f, T + W - slotW, T + L - 0.91f, T + W)};
    s.addSlab(make_float3(-T, H, -T), make_float3(1, 0, 0), make_float3(0, 0, 1), make_float3(0, -1, 0), L + 2 * T, W + 2 * T, 0.35f,
              ceilOpen, mConcrete, mConcrete);
    for (const float4& o : ceilOpen)
        addPortal(s, make_float3(-T, H, -T), make_float3(1, 0, 0), make_float3(0, 0, 1), make_float3(0, -1, 0), o, 0.35f);

    // left wall (z = 0, faces +z): the sun's wall. Full-height vertical slits, optionally a low horizontal slit.
    const float slitX = cfg.f("slit.x", 6.37f), slitW = cfg.f("slit.width", 0.20f);
    const int slitCount = cfg.i("slit.count", 1);
    const float slitStep = cfg.f("slit.spacing", 1.82f);
    std::vector<float4> leftOpen;
    for (int k = 0; k < slitCount; ++k) leftOpen.push_back(make_float4(slitX + k * slitStep, 0.0f, slitX + k * slitStep + slitW, H));
    const float lowH = cfg.f("lowslit.height", 0.0f);
    if (lowH > 0.0f) leftOpen.push_back(make_float4(cfg.f("lowslit.x0", 0.91f), 0.0f, cfg.f("lowslit.x1", L - 0.91f), lowH));
    s.addSlab(make_float3(0, 0, 0), make_float3(1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, 1), L, H, T, leftOpen, mConcrete, mConcrete);
    for (const float4& o : leftOpen) addPortal(s, make_float3(0, 0, 0), make_float3(1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, 1), o, T);
    // caustic photons aimed at the pool: only where a slit's sunbeam crosses it (the footprint of the opening,
    // projected along the sun onto the water plane, clipped to the pool)
    {
        const float el0 = cfg.f("sun.elevation", 10.0f) * 3.14159265f / 180.0f, az0 = cfg.f("sun.azimuth", 20.0f) * 3.14159265f / 180.0f;
        const float3 sd = normalize(make_float3(std::sin(az0) * std::cos(el0), std::sin(el0), -std::cos(az0) * std::cos(el0)));
        for (const float4& o : leftOpen) {
            float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (int k = 0; k < 4; ++k) {
                const float3 c = make_float3(k & 1 ? o.z : o.x, k & 2 ? o.w : o.y, -T);   // outer face corners
                const float t = (c.y - waterLevel) / sd.y;
                const float3 q = c - sd * t;
                x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); z0 = std::min(z0, q.z); z1 = std::max(z1, q.z);
            }
            x0 = std::max(x0, poolX0); x1 = std::min(x1, poolX1); z0 = std::max(z0, poolZ0); z1 = std::min(z1, poolZ1);
            if (x1 > x0 && z1 > z0)
                s.emitters.push_back({make_float3(x0, waterLevel - 0.01f, z0), make_float3(x1, waterLevel + 0.01f, z1)});
        }
    }
    // horizontal slit low in the left wall, optional
    // right wall (z = W, faces -z): white plaster. An optional tall opening looks away from the sun: cool sky fill.
    {
        std::vector<float4> rightOpen;
        const float rw0 = cfg.f("rightwindow.x0", 0.0f), rw1 = cfg.f("rightwindow.x1", 0.0f);
        if (rw1 > rw0) rightOpen.push_back(make_float4(L - rw1, cfg.f("rightwindow.y0", 0.6f), L - rw0, cfg.f("rightwindow.y1", 4.5f)));
        // the slab runs along -x so that its front faces -z: u = -x from x = L
        s.addSlab(make_float3(L, 0, W), make_float3(-1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, -1), L, H, T, rightOpen, mPlaster, mPlaster);
        for (const float4& o : rightOpen)
            addPortal(s, make_float3(L, 0, W), make_float3(-1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, -1), o, T);
    }
    // end walls (x = 0 faces +x, x = L faces -x)
    s.addSlab(make_float3(0, 0, -T), make_float3(0, 0, 1), make_float3(0, 1, 0), make_float3(1, 0, 0), W + 2 * T, H, T, {}, mConcrete, mConcrete);
    s.addSlab(make_float3(L, 0, -T), make_float3(0, 0, 1), make_float3(0, 1, 0), make_float3(-1, 0, 0), W + 2 * T, H, T, {}, mPlaster, mPlaster);

    // ---- the objects
    // objects sit in the sunbeams: the centre line of slit k's beam at depth z into the room
    const float tanAz = std::tan(cfg.f("sun.azimuth", 20.0f) * 3.14159265f / 180.0f);
    auto beamX = [&](int k, float z) {
        k = std::clamp(k, 0, slitCount - 1);
        return slitX + k * slitStep + 0.5f * (slitW - T * tanAz) - z * tanAz;
    };
    const float prismZ = cfg.f("prism.z", 2.4f);
    const float3 prismPos = cfg.v3("prism.pos", make_float3(beamX(cfg.i("prism.slit", 1), prismZ), 0.0f, prismZ));
    const float pedestalH = cfg.f("pedestal.height", 0.95f);
    // a slender column with a thin top plate: a small shadow in the beam (a block cast a dark wedge through the haze)
    const float colHalf = cfg.f("pedestal.column", 0.06f), plateHalf = cfg.f("pedestal.plate", 0.17f), plateT = 0.035f;
    s.addBox(make_float3(prismPos.x - colHalf, 0.0f, prismPos.z - colHalf), make_float3(prismPos.x + colHalf, pedestalH - plateT, prismPos.z + colHalf), mConcrete);
    s.addBox(make_float3(prismPos.x - plateHalf, pedestalH - plateT, prismPos.z - plateHalf), make_float3(prismPos.x + plateHalf, pedestalH, prismPos.z + plateHalf), mConcrete);
    const float prismSide = cfg.f("prism.side", 0.20f), prismH = cfg.f("prism.height", 0.40f);
    addPrism(s, make_float3(prismPos.x, pedestalH, prismPos.z), prismSide, prismH, cfg.f("prism.yaw", 20.0f), mPrism);
    // crystal prisms hanging in the sunbeams (like window sun-catchers): each throws its own spectrum on the walls.
    // Yaws alternate between the two windows where every colour leaves the glass (~78 and ~82 degrees, see
    // tools/prism_plan.py), so the rainbows land on both end walls.
    {
        const int nc = cfg.i("crystal.count", 0);
        const float side = cfg.f("crystal.side", 0.09f), height = cfg.f("crystal.height", 0.22f);
        unsigned int seed = 777u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0f / 16777216.0f); };
        for (int k = 0; k < nc; ++k) {
            const int slit = k % slitCount;
            const float z = 0.6f + 3.4f * rnd();
            const float y = 1.0f + 2.6f * rnd();
            const float x = beamX(slit, z);
            const float yaw = (k & 1 ? 78.0f : 82.0f) + (rnd() - 0.5f) * 2.0f + 120.0f * float(k % 3);
            addPrism(s, make_float3(x, y, z), side, height, yaw, mPrism);
            if (int(s.emitters.size()) < MAX_EMITTERS)
                s.emitters.push_back({make_float3(x - side, y, z - side), make_float3(x + side, y + height, z + side)});
        }
    }

    // a freestanding white plaster screen catching the spectrum close to the prism (an installation piece):
    // centred screen.dist metres along screen.dir (degrees in the floor plane, 0 = +x, 90 = +z), facing back
    if (cfg.i("screen.enable", 1)) {
        const float ang = cfg.f("screen.dir", 44.0f) * 3.14159265f / 180.0f, dist = cfg.f("screen.dist", 1.6f);
        const float3 dir = make_float3(std::cos(ang), 0.0f, std::sin(ang));
        const float3 c = make_float3(prismPos.x, 0.0f, prismPos.z) + dir * dist;
        const float3 side = make_float3(-dir.z, 0.0f, dir.x);
        const float hw = 0.5f * cfg.f("screen.width", 1.3f), sh = cfg.f("screen.height", 2.2f), th = 0.12f;
        const float3 a0 = c - side * hw, a1 = c + side * hw;
        // front face towards the prism, back face, two ends, top (the slab of a thin wall)
        const float3 f0 = a0, f1 = a1, b0 = a0 + dir * th, b1 = a1 + dir * th, up = make_float3(0, sh, 0);
        quadFacing(s, f0, f1, f1 + up, f0 + up, -dir, mPlaster);
        quadFacing(s, b0, b1, b1 + up, b0 + up, dir, mPlaster);
        quadFacing(s, f0, b0, b0 + up, f0 + up, -side, mPlaster);
        quadFacing(s, f1, b1, b1 + up, f1 + up, side, mPlaster);
        quadFacing(s, f0 + up, f1 + up, b1 + up, b0 + up, make_float3(0, 1, 0), mPlaster);
    }
    s.emitters.push_back({make_float3(prismPos.x - prismSide, pedestalH, prismPos.z - prismSide),
                          make_float3(prismPos.x + prismSide, pedestalH + prismH, prismPos.z + prismSide)});

    const float globeZ = cfg.f("globe.z", 1.6f);
    const float3 globePos = cfg.v3("globe.pos", make_float3(beamX(cfg.i("globe.slit", 0), globeZ), 0.0f, globeZ));
    const float globeR = cfg.f("globe.radius", 0.14f);
    const float3 gc = make_float3(globePos.x, globeR + 0.001f, globePos.z);
    s.spheres.push_back(make_float4(gc.x, gc.y, gc.z, globeR));
    s.sphereMaterial.push_back(mGlobe);
    s.spheres.push_back(make_float4(gc.x, gc.y, gc.z, globeR - 0.004f));
    s.sphereMaterial.push_back(mGlobeWater);
    s.emitters.push_back({gc - make_float3(globeR, globeR, globeR), gc + make_float3(globeR, globeR, globeR)});

    // soap bubbles drifting in the light sheet
    const int nb = cfg.i("bubbles", 5);
    s.bubbleBegin = int(s.spheres.size());
    for (int k = 0; k < nb; ++k) {
        const float fz = std::fmod(0.754878f * (k + 1), 1.0f), fy = std::fmod(0.569840f * (k + 1), 1.0f);
        const float z = 0.9f + 3.6f * fz;
        const float x = beamX(k % slitCount, z) + 0.12f * (std::fmod(0.618034f * (k + 1), 1.0f) - 0.5f);
        const float3 c = make_float3(x, 1.3f + 1.8f * fy, z);
        s.spheres.push_back(make_float4(c.x, c.y, c.z, 0.035f + 0.045f * std::fmod(0.43f * (k + 2), 1.0f)));
        s.sphereMaterial.push_back(mFilm);
    }
    // a few larger bubbles drifting through the prism's sunbeam, near the screen: the still life's sparkle
    const int nh = cfg.i("bubbles.hero", 3);
    for (int k = 0; k < nh; ++k) {
        const float z = prismZ + 0.35f + 0.55f * k;
        const float x = beamX(cfg.i("prism.slit", 1), z) + 0.06f * (k - 1);
        const float3 c = make_float3(x, pedestalH + 0.05f + 0.32f * k - 0.12f * (k == 2), z);
        s.spheres.push_back(make_float4(c.x, c.y, c.z, 0.055f + 0.02f * k));
        s.sphereMaterial.push_back(mFilm);
    }

    s.bubbleEnd = int(s.spheres.size());

    // dust motes floating in the sunbeams: points of each slit's beam (a random point of the opening, carried
    // inwards along the sun), sparse, sub-millimetre; out of focus they become the lens's nine-bladed bokeh
    {
        Material dust = makeMat(MAT_DUST, make_float3(0.75f, 0.72f, 0.66f));
        const int mDust = s.addMaterial(dust);
        const int perSlit = cfg.i("dust.per_slit", 1500);
        s.dustBegin = int(s.spheres.size());
        const float el0 = cfg.f("sun.elevation", 10.0f) * 3.14159265f / 180.0f, az0 = cfg.f("sun.azimuth", 20.0f) * 3.14159265f / 180.0f;
        const float3 sd = normalize(make_float3(std::sin(az0) * std::cos(el0), std::sin(el0), -std::cos(az0) * std::cos(el0)));
        unsigned int seed = 12345u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0f / 16777216.0f); };
        for (int k = 0; k < slitCount; ++k)
            for (int n = 0; n < perSlit; ++n) {
                const float3 q = make_float3(slitX + k * slitStep + rnd() * slitW, rnd() * H, 0.0f);
                const float3 c = q - sd * (0.3f + 7.5f * rnd());
                if (c.x < 0.05f || c.x > L - 0.05f || c.y < 0.05f || c.y > H - 0.05f || c.z < 0.05f || c.z > W - 0.05f) continue;
                const float r = 0.00025f + 0.0006f * rnd() * rnd();
                s.spheres.push_back(make_float4(c.x, c.y, c.z, r));
                s.sphereMaterial.push_back(mDust);
            }
        s.dustEnd = int(s.spheres.size());
    }

    s.restSpheres = s.spheres;

    // ---- sun
    const float el = cfg.f("sun.elevation", 10.0f) * 3.14159265f / 180.0f;
    const float az = cfg.f("sun.azimuth", 20.0f) * 3.14159265f / 180.0f;
    s.sunDir = normalize(make_float3(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)));

    s.hazeMin = make_float3(0, 0, 0);
    s.hazeMax = make_float3(L, H, W);

    // ---- cameras
    s.cameras.push_back({"gokkusagi", make_float3(6.5f, 1.4f, 5.4f), make_float3(0.0f, 1.7f, 2.6f), 24.0f, 4.0f, 0.0f});
    s.cameras.push_back({"prizma", make_float3(4.4f, 1.40f, 2.2f), make_float3(8.1f, 1.42f, 1.55f), 45.0f, 2.8f, 0.0f});
    s.cameras.push_back({"alcak", make_float3(2.2f, 0.55f, 6.4f), make_float3(9.5f, 1.2f, 2.0f), 28.0f, 5.6f, 0.0f});
    s.cameras.push_back({"orta", make_float3(3.4f, 1.45f, 6.2f), make_float3(9.8f, 1.05f, 2.6f), 32.0f, 4.0f, 0.0f});
    s.cameras.push_back({"kose", make_float3(1.3f, 1.5f, 6.6f), make_float3(9.0f, 1.1f, 0.5f), 24.0f, 5.6f, 0.0f});
    s.cameras.push_back({"havuz", make_float3(10.3f, 1.6f, 6.4f), make_float3(4.0f, 1.0f, 2.6f), 28.0f, 5.6f, 0.0f});
    s.cameras.push_back({"plan", make_float3(L * 0.5f, H - 0.05f, W * 0.5f), make_float3(L * 0.5f, 0.0f, W * 0.5f - 0.01f), 9.0f, 0.0f, 0.0f});
    return s;
}

void cameraFromPreset(const CameraPreset& p, unsigned int width, unsigned int height, Camera& cam) {
    cam.pos = p.pos;
    cam.fwd = normalize(p.target - p.pos);
    cam.right = normalize(cross(cam.fwd, make_float3(0, 1, 0)));
    cam.up = cross(cam.right, cam.fwd);
    cam.tanHalfX = 18.0f / p.focalMm;
    cam.tanHalfY = cam.tanHalfX * float(height) / float(width);
    cam.lensRadius = p.fNumber > 0.0f ? p.focalMm / (2.0f * p.fNumber) * 1e-3f : 0.0f;
    cam.focusDist = p.focusDist > 0.0f ? p.focusDist : dot(p.target - p.pos, cam.fwd);
    cam.blades = 9;
    cam.bladeRot = 0.3f;
}

// ---------------------------------------------------------------------------------------------------
// Spectral helpers (host)
// ---------------------------------------------------------------------------------------------------
static const float kD65[48] = {  // CIE D65, 360..830 nm, 10 nm
    46.6383f, 52.0891f, 49.9755f, 54.6482f, 82.7549f, 91.486f, 93.4318f, 86.6823f, 104.865f, 117.008f,
    117.812f, 114.861f, 115.923f, 108.811f, 109.354f, 107.802f, 104.79f, 107.689f, 104.405f, 104.046f,
    100.0f, 96.3342f, 95.788f, 88.6856f, 90.0062f, 89.5991f, 87.6987f, 83.2886f, 83.6992f, 80.0268f,
    80.2146f, 82.2778f, 78.2842f, 69.7213f, 71.6091f, 74.349f, 61.604f, 69.8856f, 75.087f, 63.5927f,
    46.4182f, 66.8054f, 63.3828f, 64.304f, 59.4519f, 51.959f, 57.4406f, 50.7f};
static float d65(float l) {
    const float x = std::clamp((l - 360.0f) / 10.0f, 0.0f, 46.999f);
    const int i = int(x);
    return kD65[i] + (kD65[i + 1] - kD65[i]) * (x - i);
}
// XYZ of a reflectance spectrum under D65, normalised so that a perfect white has Y = 1.
template <class F> static float3 reflectanceXYZ(F refl) {
    double X = 0, Y = 0, Z = 0, N = 0;
    for (float l = 360.0f; l <= 830.0f; l += 1.0f) {
        const float3 c = cieXYZ(l);
        const double e = d65(l), r = refl(l);
        X += e * r * c.x; Y += e * r * c.y; Z += e * r * c.z; N += e * c.y;
    }
    return make_float3(float(X / N), float(Y / N), float(Z / N));
}

float3 fitSigmoid(float3 target) {
    target = make_float3(std::clamp(target.x, 0.001f, 0.999f), std::clamp(target.y, 0.001f, 0.999f), std::clamp(target.z, 0.001f, 0.999f));
    auto residual = [&](float3 c) {
        const float3 rgb = xyzToLinearSRGB(reflectanceXYZ([&](float l) { return sigmoidSpectrum(c, l); }));
        return rgb - target;
    };
    const float lum = 0.2126f * target.x + 0.7152f * target.y + 0.0722f * target.z;
    const float s = 2.0f * lum - 1.0f;
    float3 c = make_float3(0.0f, 0.0f, s / std::sqrt(std::max(1e-6f, 1.0f - s * s)));
    for (int it = 0; it < 40; ++it) {
        const float3 r = residual(c);
        if (dot(r, r) < 1e-10f) break;
        // numeric Jacobian
        float J[3][3];
        const float h = 1e-3f;
        for (int k = 0; k < 3; ++k) {
            float3 cp = c;
            (&cp.x)[k] += h;
            const float3 d = (residual(cp) - r) * (1.0f / h);
            J[0][k] = d.x; J[1][k] = d.y; J[2][k] = d.z;
        }
        // solve J dx = -r (Cramer)
        const float det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0]) +
                          J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (std::fabs(det) < 1e-12f) break;
        float3 dx;
        const float b[3] = {-r.x, -r.y, -r.z};
        for (int k = 0; k < 3; ++k) {
            float M[3][3];
            for (int a = 0; a < 3; ++a)
                for (int bb = 0; bb < 3; ++bb) M[a][bb] = bb == k ? b[a] : J[a][bb];
            const float dk = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                             M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
            (&dx.x)[k] = dk / det;
        }
        // damped step
        float step = 1.0f;
        const float r0 = dot(r, r);
        for (int t = 0; t < 10; ++t) {
            const float3 cn = c + dx * step;
            const float3 rn = residual(cn);
            if (dot(rn, rn) < r0) { c = cn; break; }
            step *= 0.5f;
        }
    }
    return c;
}

bool spectralSelfTest(std::string& report) {
    std::ostringstream o;
    bool ok = true;
    auto check = [&](const char* name, double got, double want, double tol) {
        const bool pass = std::fabs(got - want) <= tol;
        ok &= pass;
        char line[256];
        std::snprintf(line, sizeof(line), "  %-34s %10.5f  (beklenen %10.5f +-%g)  %s\n", name, got, want, tol, pass ? "OK" : "HATA");
        o << line;
    };
    // 1. D65 white point through the CMF fit
    double X = 0, Y = 0, Z = 0, Yint = 0;
    for (float l = 360.0f; l <= 830.0f; l += 1.0f) {
        const float3 c = cieXYZ(l);
        X += d65(l) * c.x; Y += d65(l) * c.y; Z += d65(l) * c.z; Yint += c.y;
    }
    check("D65 x", X / (X + Y + Z), 0.31271, 0.0015);
    check("D65 y", Y / (X + Y + Z), 0.32902, 0.0015);
    check("CMF ybar integrali (nm)", Yint, 106.857, 0.2);
    const float3 w = xyzToLinearSRGB(make_float3(float(X / Y), 1.0f, float(Z / Y)));
    check("D65 -> sRGB R", w.x, 1.0, 0.01);
    check("D65 -> sRGB G", w.y, 1.0, 0.01);
    check("D65 -> sRGB B", w.z, 1.0, 0.01);
    // 2. Sellmeier at Fraunhofer F (486.13), d (587.56), C (656.27): catalogue values
    check("BK7 nF", iorOf(IOR_BK7, 486.13f), 1.52238, 2e-4);
    check("BK7 nd", iorOf(IOR_BK7, 587.56f), 1.51680, 2e-4);
    check("BK7 nC", iorOf(IOR_BK7, 656.27f), 1.51432, 2e-4);
    check("SF11 nF", iorOf(IOR_SF11, 486.13f), 1.80645, 3e-4);
    check("SF11 nd", iorOf(IOR_SF11, 587.56f), 1.78472, 3e-4);
    check("SF11 nC", iorOf(IOR_SF11, 656.27f), 1.77599, 3e-4);
    check("su nD (589.3)", iorOf(IOR_WATER, 589.3f), 1.3330, 1.5e-3);
    // 3. Fresnel and thin film
    check("Fresnel n=1.5 dik gelis", fresnelDielectric(1.0f, 1.5f), 0.04, 1e-5);
    check("ince film d=0 -> R", thinFilmReflectance(550.0f, 0.0f, 1.333f, 1.0f), 0.0, 1e-6);
    check("ince film d=lambda/2n -> R", thinFilmReflectance(550.0f, 550.0f / (2 * 1.333f), 1.333f, 1.0f), 0.0, 1e-5);
    check("ince film d=lambda/4n -> R", thinFilmReflectance(550.0f, 550.0f / (4 * 1.333f), 1.333f, 1.0f), 0.0784, 2e-3);
    // 3b. soap film colours follow Newton's sequence: hue of the reflected D65 light at a few thicknesses
    auto filmHue = [&](float d) {
        const float3 xyz = reflectanceXYZ([&](float l) { return thinFilmReflectance(l, d, iorOf(IOR_WATER, l), 1.0f); });
        const float3 c = xyzToLinearSRGB(xyz);
        const float mx = std::max(c.x, std::max(c.y, c.z)), mn = std::min(c.x, std::min(c.y, c.z));
        if (mx - mn <= 1e-9f) return 0.0f;
        float h = mx == c.x ? (c.y - c.z) / (mx - mn) : (mx == c.y ? 2.0f + (c.z - c.x) / (mx - mn) : 4.0f + (c.x - c.y) / (mx - mn));
        h *= 60.0f;
        return h < 0 ? h + 360.0f : h;
    };
    check("film 150 nm tonu (sari-turuncu)", filmHue(150.0f), 40.0, 25.0);
    check("film 200 nm tonu (mor)", filmHue(200.0f), 285.0, 30.0);
    check("film 250 nm tonu (mavi)", filmHue(250.0f), 215.0, 25.0);
    check("film 300 nm tonu (yesil)", filmHue(300.0f), 115.0, 30.0);
    check("film 360 nm tonu (kirmizi)", filmHue(360.0f), 15.0, 20.0);
    // 4. sigmoid fit round trip
    const float3 tgt = make_float3(0.8f, 0.25f, 0.1f);
    const float3 c = fitSigmoid(tgt);
    const float3 got = xyzToLinearSRGB(reflectanceXYZ([&](float l) { return sigmoidSpectrum(c, l); }));
    check("sigmoid uydurma R", got.x, tgt.x, 0.01);
    check("sigmoid uydurma G", got.y, tgt.y, 0.01);
    check("sigmoid uydurma B", got.z, tgt.z, 0.01);
    report = o.str();
    return ok;
}

void whiteBalanceMatrix(float kelvin, float out[9]) {
    const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (kelvin <= 0.0f) { std::copy(id, id + 9, out); return; }
    double X = 0, Y = 0, Z = 0;
    for (float l = 360.0f; l <= 830.0f; l += 1.0f) {
        const float3 c = cieXYZ(l);
        const double b = blackbody(l, kelvin);
        X += b * c.x; Y += b * c.y; Z += b * c.z;
    }
    const double src[3] = {X / Y, 1.0, Z / Y}, dst[3] = {0.95047, 1.0, 1.08883};
    const double B[3][3] = {{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}};
    const double Bi[3][3] = {{0.9869929, -0.1470543, 0.1599627}, {0.4323053, 0.5183603, 0.0492912}, {-0.0085287, 0.0400428, 0.9684867}};
    double ls[3], ld[3];
    for (int i = 0; i < 3; ++i) {
        ls[i] = B[i][0] * src[0] + B[i][1] * src[1] + B[i][2] * src[2];
        ld[i] = B[i][0] * dst[0] + B[i][1] * dst[1] + B[i][2] * dst[2];
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double v = 0;
            for (int k = 0; k < 3; ++k) v += Bi[r][k] * (ld[k] / ls[k]) * B[k][c];
            out[r * 3 + c] = float(v);
        }
}

void animateScene(Scene& s, float t) {
    // dust: slow, smooth wandering of a few centimetres (sums of incommensurate sines), a faint settling drift
    for (int i = s.dustBegin; i < s.dustEnd; ++i) {
        const float4 r = s.restSpheres[i];
        const float p = float(i) * 1.618034f;
        s.spheres[i] = make_float4(r.x + 0.025f * std::sin(0.31f * t + p) + 0.012f * std::sin(0.83f * t + 2.1f * p),
                                   r.y + 0.020f * std::sin(0.23f * t + 1.7f * p) - 0.004f * t + 0.3f * std::floor(0.004f * t / 0.3f),
                                   r.z + 0.025f * std::sin(0.27f * t + 0.6f * p) + 0.010f * std::sin(0.71f * t + 3.3f * p), r.w);
    }
    // bubbles: lazy loops through the light
    for (int i = s.bubbleBegin; i < s.bubbleEnd; ++i) {
        const float4 r = s.restSpheres[i];
        const float p = float(i - s.bubbleBegin) * 2.39996f;
        s.spheres[i] = make_float4(r.x + 0.18f * std::sin(0.071f * t + p), r.y + 0.10f * std::sin(0.053f * t + 1.3f * p),
                                   r.z + 0.15f * std::sin(0.047f * t + 0.7f * p), r.w);
    }
}
