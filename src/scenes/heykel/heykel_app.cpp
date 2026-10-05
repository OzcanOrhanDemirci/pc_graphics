// Scene 6 "Heykel": the run's state and the command-line tools.
//
//   pc_graphics.exe --scene heykel --headless [--aday 0..5|all] [--spp 128] [--w 1280 --h 720] [--zoom 1,10,100]
//                   [--px 0.5,0.45] [--out captures/heykel_etut] [--set key=value]
//       the candidates' first view and zoom steps, path traced, to PNG (the study's frames, and timings)
#include "scenes/heykel/heykel_app.h"
#include "core/image_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace {

double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const float kDeg = 3.14159265f / 180.0f;

// rotation matrices (rows) from Euler angles (degrees, x then y then z) and from an axis and an angle
hk::M3 rotation(float3 euler) {
    const float cx = std::cos(euler.x * kDeg), sx = std::sin(euler.x * kDeg);
    const float cy = std::cos(euler.y * kDeg), sy = std::sin(euler.y * kDeg);
    const float cz = std::cos(euler.z * kDeg), sz = std::sin(euler.z * kDeg);
    // R = Rz * Ry * Rx
    hk::M3 m;
    m.r0 = make_float3(cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx);
    m.r1 = make_float3(sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx);
    m.r2 = make_float3(-sy, cy * sx, cy * cx);
    return m;
}
hk::M3 transpose(const hk::M3& m) {
    hk::M3 t;
    t.r0 = make_float3(m.r0.x, m.r1.x, m.r2.x);
    t.r1 = make_float3(m.r0.y, m.r1.y, m.r2.y);
    t.r2 = make_float3(m.r0.z, m.r1.z, m.r2.z);
    return t;
}
hk::M3 axisAngle(float3 axis, float angle) {
    const float3 a = normalize(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    hk::M3 m;
    m.r0 = make_float3(t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y);
    m.r1 = make_float3(t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x);
    m.r2 = make_float3(t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c);
    return m;
}
float3 rotateAxis(float3 v, float3 axis, float angle) {   // Rodrigues
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

}   // namespace

void HeykelApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/heykel.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    fovDeg = cfg.f("fov", fovDeg);
    maxLevel = cfg.f("zoom.max", maxLevel);
}

// G0's candidates (PLAN §5). The knobs are first guesses, set by looking at the study's frames; every one can be
// overridden from the settings as aday<i>.<knob> (see applyOverrides).
void HeykelApp::buildCandidates() {
    cands.clear();
    {   // Mandelbox, scale 3: a cube whose faces are carved into blocks of buildings
        HeykelCandidate c;
        c.tr = "Şehir";
        c.en = "City";
        c.id = "sehir";
        c.shape.fractal = hk::kBox;
        c.shape.iters = 14;
        c.shape.a = 3.0f;
        c.shape.b = 0.25f;
        c.shape.c = 1.0f;
        c.shape.relax = 0.85f;
        c.homeYaw = 38.0f;
        c.homePitch = -18.0f;
        cands.push_back(c);
    }
    {   // pseudo-Kleinian cut into a block: rows of arches and hourglass columns
        HeykelCandidate c;
        c.tr = "Katedral";
        c.en = "Cathedral";
        c.id = "katedral";
        c.shape.fractal = hk::kKleinian;
        c.shape.iters = 10;
        c.shape.k = make_float3(0.92436f, 0.90756f, 0.92436f);
        c.shape.a = 1.0f;
        c.shape.b = 0.92784f;
        c.shape.cut = 2;
        c.shape.cutSize = make_float3(2.2f, 2.2f, 2.2f);
        c.shape.relax = 0.8f;
        c.euler = make_float3(-90.0f, 0.0f, 0.0f);   // the structure's long axis (z) upright
        c.homeYaw = 30.0f;
        c.homePitch = -10.0f;
        cands.push_back(c);
    }
    {   // a Menger sponge with its top corner cut off through the centre: the cut shows six-pointed stars
        HeykelCandidate c;
        c.tr = "Yıldız";
        c.en = "Star";
        c.id = "yildiz";
        c.shape.fractal = hk::kCrystal;
        c.shape.iters = 6;
        c.shape.a = 3.0f;
        c.shape.k = make_float3(1, 1, 1);
        c.shape.cut = 4;
        c.shape.cutSize = make_float3(1, 1, 1);
        c.shape.relax = 0.9f;
        c.cutDepth = 0.01f;
        c.homeYaw = -24.0f;   // off the star's axis: its six points are seen at a slant, with the cube's faces
        c.homePitch = -26.0f;
        c.frame = 1.3f;
        cands.push_back(c);
    }
    {   // the Mandelbulb, power 8, leaning
        HeykelCandidate c;
        c.tr = "Tohum";
        c.en = "Seed";
        c.id = "tohum";
        c.shape.fractal = hk::kBulb;
        c.shape.iters = 12;
        c.shape.a = 8.0f;
        c.shape.relax = 0.9f;
        c.euler = make_float3(-60.0f, 20.0f, 0.0f);
        c.cutDepth = 0.06f;
        cands.push_back(c);
    }
    {   // Apollonian packing cut by a sphere: plates, spheres and holes
        HeykelCandidate c;
        c.tr = "Dantel";
        c.en = "Lace";
        c.id = "dantel";
        c.shape.fractal = hk::kApollonian;
        c.shape.iters = 9;
        c.shape.a = 1.15f;
        c.shape.cut = 1;
        c.shape.cutSize = make_float3(2.6f, 0, 0);
        c.shape.relax = 0.85f;
        c.cutDepth = 0.08f;
        c.euler = make_float3(20.0f, 30.0f, 0.0f);
        cands.push_back(c);
    }
    for (int i = 0; i < int(cands.size()); ++i) applyOverrides(i);
    for (HeykelCandidate& c : cands) {
        if (c.turn != 0.0f) c.shape.fold = axisAngle(c.turnAxis, c.turn);
        if (c.turn2 != 0.0f) c.shape.fold2 = axisAngle(c.turnAxis2, c.turn2);
    }
}

void HeykelApp::applyOverrides(int i) {
    HeykelCandidate& c = cands[i];
    const std::string p = "aday" + std::to_string(i) + ".";
    hk::Shape& s = c.shape;
    s.fractal = cfg.i(p + "fractal", s.fractal);
    s.iters = cfg.i(p + "iters", s.iters);
    s.a = cfg.f(p + "a", s.a);
    s.b = cfg.f(p + "b", s.b);
    s.c = cfg.f(p + "c", s.c);
    s.k = cfg.v3(p + "k", s.k);
    s.cut = cfg.i(p + "cut", s.cut);
    s.cutSize = cfg.v3(p + "cutsize", s.cutSize);
    s.relax = cfg.f(p + "relax", s.relax);
    s.cutOffset = cfg.f(p + "cutoffset", s.cutOffset);
    s.julia = cfg.i(p + "julia", s.julia);
    c.euler = cfg.v3(p + "euler", c.euler);
    c.turn = cfg.f(p + "turn", c.turn);
    c.turnAxis = cfg.v3(p + "turnaxis", c.turnAxis);
    c.turn2 = cfg.f(p + "turn2", c.turn2);
    c.turnAxis2 = cfg.v3(p + "turnaxis2", c.turnAxis2);
    c.sizeM = cfg.f(p + "size", c.sizeM);
    c.cutDepth = cfg.f(p + "cutdepth", c.cutDepth);
    c.homeYaw = cfg.f(p + "yaw", c.homeYaw);
    c.homePitch = cfg.f(p + "pitch", c.homePitch);
    c.frame = cfg.f(p + "frame", c.frame);
}

void HeykelApp::init() {
    const size_t n = size_t(width) * height;
    cudaMalloc(reinterpret_cast<void**>(&preview), n * sizeof(float4));
    cudaMalloc(reinterpret_cast<void**>(&accum), n * sizeof(float4));
    cudaMalloc(reinterpret_cast<void**>(&output), n * sizeof(uchar4));
    cudaMalloc(reinterpret_cast<void**>(&devSteps), sizeof(hk::KStep) * hk::kMaxSteps);
    cudaMemset(preview, 0, n * sizeof(float4));
    cudaMemset(accum, 0, n * sizeof(float4));
}

void HeykelApp::release() {
    if (preview) cudaFree(preview);
    if (accum) cudaFree(accum);
    if (output) cudaFree(output);
    if (devSteps) cudaFree(devSteps);
    devSteps = nullptr;
    preview = accum = nullptr;
    output = nullptr;
}

// The fit: rays from far outside towards the centre (Fibonacci directions) find the outer surface; its farthest
// point is the bound, its lowest (in the turned frame) where the sculpture stands.
void HeykelApp::setCandidate(int i) {
    cand = std::clamp(i, 0, int(cands.size()) - 1);
    const HeykelCandidate& c = cands[cand];
    hk::Shape s = c.shape;
    const hk::M3 R = rotation(c.euler);   // fractal -> world (turn)
    s.orient = transpose(R);              // world -> fractal
    const float R0 = 16.0f;
    float maxR = 0.0f, minY = 1e9f, maxY = -1e9f;
    double sumX = 0.0, sumZ = 0.0;
    int hits = 0;
    const int N = 1500;
    for (int k = 0; k < N; ++k) {
        const float y = 1.0f - 2.0f * (k + 0.5f) / N, r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float ph = k * 2.39996323f;
        const float3 d = make_float3(std::cos(ph) * r, y, std::sin(ph) * r);
        const float3 ro = d * R0, rd = -d;
        float t = 0.0f;
        for (int st = 0; st < 2000 && t < R0; ++st) {
            const float3 p = ro + rd * t;
            const float de = hk::deFractal(p, s);
            if (de < 1e-4f) {
                maxR = std::max(maxR, length(p));
                const float3 pw = mul(R, p);   // the turned frame (world axes, fractal units)
                minY = std::min(minY, pw.y);
                maxY = std::max(maxY, pw.y);
                sumX += pw.x;
                sumZ += pw.z;
                ++hits;
                break;
            }
            t += de * s.relax;
        }
    }
    if (maxR <= 0.0f) { maxR = 1.0f; minY = -1.0f; maxY = 1.0f; }
    fitR = maxR;
    fitMinY = minY;
    s.boundR = maxR * 1.04f;
    const float heightF = std::max(maxY - minY, 1e-3f);
    s.scale = c.sizeM / heightF;
    // the plinth's top at y = 0; the bottom cutDepth of the height below it, cut flat
    const float cutF = c.cutDepth * heightF;
    // over the plinth's middle: the outer surface's mean (a half cut away leaves the rest off the centre)
    const float cx = hits ? float(sumX / hits) : 0.0f, cz = hits ? float(sumZ / hits) : 0.0f;
    s.center = make_float3(-cx * s.scale, -(minY + cutF) * s.scale, -cz * s.scale);
    s.floorCut = 0.0f;
    world = hk::World();
    world.s = s;
    world.plinthH = make_float3(cfg.f("plinth.half", 0.19f), 0.5f, cfg.f("plinth.half", 0.19f));
    world.plinthC = make_float3(0.0f, -0.5f, 0.0f);
    world.floorY = -1.0f;
    setAnchor(world, d3(0, 0, 0));   // the host's own float estimates use world positions as offsets, straight
    world.pert = 0;
    std::printf("heykel: aday %d (%s): yaricap %.3f, yukseklik %.3f birim, olcek %.4f m/birim\n", cand, c.id.c_str(), maxR,
                heightF, s.scale);
    home();
    defaultLight();
    aimLight(homeFocus);
}

void HeykelApp::home() {
    const HeykelCandidate& c = cands[cand];
    const float tanHalf = std::tan(0.5f * fovDeg * kDeg);
    // the bounding sphere (world) framed with room around it, a little of the plinth in the picture
    const float rW = world.s.boundR * world.s.scale;
    const float3 sc = world.s.center;
    const float top = std::min(sc.y + rW, c.sizeM * 1.05f);
    homeFocus = d3(0.0, 0.42 * top - 0.03, 0.0);
    homeYaw = c.homeYaw * kDeg;
    homePitch = c.homePitch * kDeg;
    const float half = std::max(0.5f * top + 0.1f, rW * 0.8f);
    const float dist = cfg.f("frame", 1.35f) * c.frame * half / tanHalf;
    yaw = homeYaw;
    pitch = homePitch;
    float3 r, u, f;
    cameraBasis(r, u, f);
    homePos = homeFocus - d3(f) * dist;
    camPos = homePos;
    focus = homeFocus;
    level = 1.0f;
}

void HeykelApp::cameraBasis(float3& right, float3& up, float3& fwd) const {
    fwd = make_float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
    right = make_float3(std::cos(yaw), 0.0f, std::sin(yaw));
    up = cross(right, fwd);
}

namespace {

// the pseudo-Kleinian in doubles, straight (the host's picks and the perturbation's reference)
double deKleinianD(D3 p, const hk::Shape& s) {
    double df = 1.0;
    const double kx = s.k.x, ky = s.k.y, kz = s.k.z;
    for (int i = 0; i < s.iters; ++i) {
        p = d3(2.0 * std::clamp(p.x, -kx, kx) - p.x, 2.0 * std::clamp(p.y, -ky, ky) - p.y, 2.0 * std::clamp(p.z, -kz, kz) - p.z);
        const double r2 = dot(p, p);
        const double k = std::max(double(s.a) / r2, 1.0);
        p = p * k;
        df *= k;
    }
    const double rxy = std::sqrt(p.x * p.x + p.y * p.y);
    return std::max(rxy - s.b, std::fabs(rxy * p.z) / length(p)) / df;
}
double sdBoxD(D3 p, D3 h) {
    const D3 q = d3(std::fabs(p.x) - h.x, std::fabs(p.y) - h.y, std::fabs(p.z) - h.z);
    const D3 m = d3(std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0));
    return length(m) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0);
}
D3 mulD(const hk::M3& m, D3 v) { return d3(dot(d3(m.r0), v), dot(d3(m.r1), v), dot(d3(m.r2), v)); }

}   // namespace

void HeykelApp::setAnchor(hk::World& w, D3 W) const {
    const hk::Shape& s = w.s;
    w.plinthRel = f3(W - d3(w.plinthC));
    w.floorRel = float(W.y - w.floorY);
    w.floorCutRel = float(W.y - s.floorCut);
    w.centerRel = f3(W - d3(s.center));
    w.pert = 0;
    if (s.fractal != hk::kKleinian) return;
    // the anchor's orbit in doubles
    const D3 A0 = mulD(s.orient, W - d3(s.center)) * (1.0 / s.scale);
    w.pert = 1;
    w.n = std::min(s.iters, hk::kMaxSteps);
    w.A0 = f3(A0);
    w.cutSign = make_float3(A0.x < 0 ? -1.0f : 1.0f, A0.y < 0 ? -1.0f : 1.0f, A0.z < 0 ? -1.0f : 1.0f);
    w.cutRel = make_float3(float(std::fabs(A0.x) - s.cutSize.x), float(std::fabs(A0.y) - s.cutSize.y), float(std::fabs(A0.z) - s.cutSize.z));
    D3 A = A0;
    const double C[3] = {s.k.x, s.k.y, s.k.z};
    w.st = hostSteps;
    for (int i = 0; i < w.n; ++i) {
        hk::KStep& k = hostSteps[i];
        double a[3] = {A.x, A.y, A.z}, af[3];
        int b[3];
        float tp[3], tn[3];
        for (int ax = 0; ax < 3; ++ax) {
            tp[ax] = float(C[ax] - a[ax]);
            tn[ax] = float(-C[ax] - a[ax]);
            b[ax] = a[ax] > C[ax] ? 1 : (a[ax] < -C[ax] ? -1 : 0);
            af[ax] = b[ax] == 1 ? 2.0 * C[ax] - a[ax] : (b[ax] == -1 ? -2.0 * C[ax] - a[ax] : a[ax]);
        }
        k.toPos = make_float3(tp[0], tp[1], tp[2]);
        k.toNeg = make_float3(tn[0], tn[1], tn[2]);
        k.bx = b[0];
        k.by = b[1];
        k.bz = b[2];
        const D3 Af = d3(af[0], af[1], af[2]);
        const double r2 = dot(Af, Af);
        k.Af = f3(Af);
        k.r2 = float(r2);
        k.r2m = float(r2 - s.a);
        const double kA = r2 < s.a ? s.a / r2 : 1.0;
        A = Af * kA;
    }
    w.AN = f3(A);
}

// The iterations follow the zoom (the pixel rule for the formula): each one adds structure about a few times finer,
// so one or two more a decade keep the detail from running out (at 10,000 times 10 left rings floating in the dark,
// 14-18 kept the lace, PLAN G1). Below a pixel the extra ones change nothing that is drawn.
int HeykelApp::itersNow() const {
    const int base = cands[cand].shape.iters;
    if (!deep()) return base;
    const float per = cfg.f("iters.per.decade", 2.0f);
    return std::clamp(base + int(std::lround(per * std::log10(std::max(level, 1.0f)))), base, hk::kMaxSteps);
}

double HeykelApp::deHost(D3 p) const {
    if (!deep()) return hk::deScene(f3(p), world);   // world's anchor is the origin
    hk::Shape s = world.s;
    s.iters = itersNow();
    double d;
    const D3 q = p - d3(s.center);
    const double rw = double(s.boundR) * s.scale, bd = length(q) - rw;
    if (bd > 0.05 * rw) {
        d = bd;
    } else {
        const D3 pf = mulD(s.orient, q) * (1.0 / s.scale);
        d = deKleinianD(pf, s);
        if (s.cut == 2) d = std::max(d, sdBoxD(pf, d3(s.cutSize)));
        d *= s.scale;
    }
    d = std::max(d, double(s.floorCut) - p.y);
    d = std::min(d, sdBoxD(p - d3(world.plinthC), d3(world.plinthH)));
    return std::min(d, p.y - world.floorY);
}

HeykelView HeykelApp::view() const {
    HeykelView v;
    v.w = world;
    v.w.s.iters = itersNow();
    setAnchor(v.w, focus);
    // perturbation only where floats fall short (it costs ~3 times the straight estimate): at 200 times the straight one
    // errs by ~0.03 pixel (--test pert), the switch cannot be seen
    if (level < cfg.f("pert.from", 200.0f)) v.w.pert = 0;
    if (v.w.pert && devSteps) {
        cudaMemcpy(devSteps, hostSteps, sizeof(hk::KStep) * v.w.n, cudaMemcpyHostToDevice);
        v.w.st = devSteps;
    }
    v.width = width;
    v.height = height;
    v.camPos = f3(camPos - focus);
    cameraBasis(v.camRight, v.camUp, v.camFwd);
    v.tanHalf = std::tan(0.5f * fovDeg * kDeg);
    v.pixAngle = 2.0f * v.tanHalf / float(height);
    v.detail = cfg.f("detail", 0.5f);
    v.maxSteps = cfg.i("steps", 400);
    v.maxSteps2 = cfg.i("steps2", 200);
    v.shadowSlope = cfg.f("shadow.slope", 2.0f);
    v.bounceSlope = cfg.f("bounce.slope", 4.0f);
    v.filterSigma = cfg.f("filter.sigma", 0.45f);
    v.tmax = 30.0f;
    v.lightPos = f3(lightTarget + d3(lightDir) * lightDist - focus);
    v.lightAxis = -lightDir;
    v.lightR = lightRadius;
    v.lightLe = cfg.f("light.e", 3.14159f) * lightDist * lightDist / (3.14159f * lightRadius * lightRadius);
    const float outer = std::atan(litRadius / lightDist), inner = std::atan(litRadius * 0.55f / lightDist);
    v.coneOuter = std::cos(outer);
    v.coneInner = std::cos(inner);
    v.lightColor = cfg.v3("light.color", make_float3(1.0f, 0.93f, 0.84f));
    v.fillLe = cfg.f("fill", 0.03f);
    v.fillColor = cfg.v3("fill.color", make_float3(0.80f, 0.88f, 1.0f));
    v.albedo[0] = cfg.v3("albedo", make_float3(0.80f, 0.78f, 0.74f));
    v.albedo[1] = cfg.v3("albedo.plinth", make_float3(0.16f, 0.16f, 0.16f));
    v.albedo[2] = cfg.v3("albedo.floor", make_float3(0.035f, 0.035f, 0.035f));
    v.exposure = std::exp2(ev + cfg.f("ev", 0.0f));
    v.sample = samples;
    v.bounces = cfg.i("bounces", 3);
    return v;
}

// the cursor's ray marched on the host in doubles (the same threshold as the pictures: half a pixel's footprint)
bool HeykelApp::pick(float px, float py, D3& hit, float3* normal) const {
    float3 r, u, f;
    cameraBasis(r, u, f);
    const double tanHalf = std::tan(0.5 * fovDeg * kDeg);
    const double aspect = double(width) / height;
    const double sx = (px / width * 2.0 - 1.0) * tanHalf * aspect, sy = (1.0 - py / double(height) * 2.0) * tanHalf;
    D3 rd = d3(f) + d3(r) * sx + d3(u) * sy;
    rd = rd * (1.0 / length(rd));
    const double slope = 2.0 * tanHalf / height * cfg.f("detail", 0.5f);
    const int steps = cfg.i("steps", 400) * 2;
    double t = 0.0;
    for (int i = 0; i < steps; ++i) {
        const D3 p = camPos + rd * t;
        const double d = deHost(p);
        if (d < slope * t) {
            hit = p;
            if (normal) {
                const double e = slope * t;
                const D3 g = d3(deHost(p + d3(e, 0, 0)) - deHost(p - d3(e, 0, 0)), deHost(p + d3(0, e, 0)) - deHost(p - d3(0, e, 0)),
                                deHost(p + d3(0, 0, e)) - deHost(p - d3(0, 0, e)));
                const double l = length(g);
                *normal = l > 0.0 ? f3(g * (1.0 / l)) : make_float3(0, 1, 0);
            }
            return true;
        }
        t += d * world.s.relax;
        if (t > 30.0) return false;
    }
    return false;
}

void HeykelApp::zoomAbout(D3 q, float logAmount) {
    float target = level * std::exp(logAmount);
    const float cap = zoomCap();
    if (target > cap) logAmount = std::log(cap / level);
    if (target < 1.0f) {   // out past the first view: back to it
        home();
        return;
    }
    camPos = q + (camPos - q) * std::exp(-double(logAmount));
    level *= std::exp(logAmount);
    focus = q;
}

bool HeykelApp::orbit(float dyaw, float dpitch) {
    const D3 rel0 = camPos - focus;
    // yaw: the forward's horizontal part turns by dyaw, the offset with it
    const double c = std::cos(dyaw), s = std::sin(dyaw);
    D3 rel = d3(rel0.x * c - rel0.z * s, rel0.y, rel0.x * s + rel0.z * c);
    const float newYaw = yaw + dyaw;
    const float newPitch = std::clamp(pitch + dpitch, -1.35f, 1.2f);
    // pitch: about the new right axis (Rodrigues, doubles)
    const D3 ax = d3(std::cos(newYaw), 0.0, std::sin(newYaw));
    const double ang = newPitch - pitch, ca = std::cos(ang), sa = std::sin(ang);
    const D3 cr = d3(ax.y * rel.z - ax.z * rel.y, ax.z * rel.x - ax.x * rel.z, ax.x * rel.y - ax.y * rel.x);
    rel = rel * ca + cr * sa + ax * (dot(ax, rel) * (1.0 - ca));
    const D3 p = focus + rel;
    if (deHost(p) < length(rel) * 0.02) return false;
    camPos = p;
    yaw = newYaw;
    pitch = newPitch;
    return true;
}

void HeykelApp::defaultLight() {
    float3 r, u, f;
    cameraBasis(r, u, f);
    const float3 rel = cfg.v3("light.rel", make_float3(-0.78f, 0.55f, 0.30f));
    lightDir = normalize(r * rel.x + u * rel.y - f * rel.z);
}

void HeykelApp::aimLight(D3 target, const float3* normal) {
    lightTarget = target;
    const float d = float(length(camPos - target));
    lightDist = cfg.f("light.dist", 1.4f) * d;
    lightRadius = cfg.f("light.size", 0.035f) * lightDist;
    const float tanHalf = std::tan(0.5f * fovDeg * kDeg);
    litRadius = cfg.f("light.spot", 0.6f) * (2.0f * tanHalf * d);
    if (normal) {   // behind the surface it would light nothing: lean it out to a raking angle at least
        const float c = dot(lightDir, *normal), want = 0.25f;
        if (c < want) lightDir = normalize(lightDir + *normal * (want - c) * 1.5f);
    }
}

void HeykelApp::turnLight(float dAzimuth, float dElevation) {
    float3 d = lightDir;
    const float c = std::cos(dAzimuth), s = std::sin(dAzimuth);
    d = make_float3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
    const float3 axis = cross(d, make_float3(0, 1, 0));
    const float al = length(axis);
    if (al > 1e-4f) {
        const float3 d2 = normalize(rotateAxis(d, axis * (1.0f / al), -dElevation));
        if (std::fabs(d2.y) < 0.985f) d = d2;
    }
    lightDir = normalize(d);
}

void HeykelApp::renderPreview() {
    const HeykelView v = view();
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    heykelPreview(v, preview, 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    cudaEventElapsedTime(&previewMs, a, b);
    cudaEventDestroy(a);
    cudaEventDestroy(b);
    cudaMemset(accum, 0, size_t(width) * height * sizeof(float4));
    samples = 0;
}

void HeykelApp::accumulate(int spp) {
    if (spp <= 0) return;
    pathPass(spp, 0, 1);
    samples += spp;
}

void HeykelApp::pathPass(int spp, int stripe, int stripes) {
    const HeykelView v = view();
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    heykelPath(v, accum, spp, stripe, stripes, 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, a, b);
    pathMs = ms * stripes / spp;
    cudaEventDestroy(a);
    cudaEventDestroy(b);
}

void HeykelApp::resolve() { heykelResolve(view(), preview, samples > 0 ? accum : nullptr, output, 0); }

void HeykelApp::save(const std::string& prefix) {
    std::vector<unsigned char> px(size_t(width) * height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    writePNG(prefix + ".png", width, height, px.data());
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::vector<float> floats(const std::string& s) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::stof(part));
    return out;
}

// --test pert: the perturbed estimate (floats, offsets from an anchor, what the GPU runs) against the straight one in
// doubles, at points ever closer to anchors on the surface; the straight float estimate beside it for scale. The
// error is in pixels of the 2560x1440 view at the zoom where those points fill the screen.
int testPerturbation(HeykelApp& app) {
    app.setCandidate(1);
    if (!app.deep()) { std::printf("aday 1 Klein degil\n"); return 1; }
    unsigned int seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0 / 16777216.0); };
    // anchors: picks under random pixels of the first view, walked in twice closer a step (as the wheel does)
    std::vector<D3> anchors;
    for (int k = 0; k < 40 && anchors.size() < 12; ++k) {
        app.home();
        D3 q;
        if (!app.pick(float(0.3 + 0.4 * rnd()) * app.width, float(0.25 + 0.5 * rnd()) * app.height, q)) continue;
        bool ok = true;
        while (app.level < 10000.0f) {
            app.zoomAbout(q, std::log(2.0f));
            if (!app.pick(0.5f * app.width, 0.5f * app.height, q)) { ok = false; break; }
        }
        if (ok) anchors.push_back(q);
    }
    std::printf("cipa: %zu nokta (10.000 katta yuzeyde)\n", anchors.size());
    const double tanHalf = std::tan(0.5 * app.fovDeg * 3.14159265 / 180.0);
    std::printf("%10s %12s %14s %14s\n", "kat", "piksel (m)", "pert hata px", "float hata px");
    for (double zoom : {1.0, 10.0, 100.0, 1000.0, 10000.0}) {
        // the first view's distance over the zoom: the camera's distance there; a pixel's footprint at it
        const double dist = length(app.homePos - app.homeFocus) / zoom;
        const double pixel = dist * 2.0 * tanHalf / app.height;
        double ep = 0.0, ef = 0.0;
        int n = 0;
        for (const D3& W : anchors) {
            hk::World wa = app.world;
            app.setAnchor(wa, W);
            for (int j = 0; j < 64; ++j) {
                // points within ~200 pixels of the anchor
                const D3 o = d3(rnd() - 0.5, rnd() - 0.5, rnd() - 0.5) * (400.0 * pixel);
                const double dD = app.deHost(W + o);
                if (std::fabs(dD) > 50.0 * pixel) continue;   // near the surface only: that is where it decides
                const double dP = hk::deScene(f3(o), wa);
                const double dF = hk::deScene(f3(W + o), app.world);
                ep = std::max(ep, std::fabs(dP - dD) / pixel);
                ef = std::max(ef, std::fabs(dF - dD) / pixel);
                ++n;
            }
        }
        std::printf("%10.0f %12.3g %14.4f %14.4f   (%d nokta)\n", zoom, pixel, ep, ef, n);
    }
    return 0;
}

}   // namespace

int runHeykelCli(int argc, char** argv) {
    HeykelApp app;
    std::string which = "all", out = "captures/heykel_etut";
    int spp = 128;
    std::vector<float> zooms = {1.0f};
    float2 px{-1.0f, -1.0f};
    int w = 1280, h = 720;
    std::string test;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--aday") which = next();
        else if (a == "--spp") spp = std::stoi(next());
        else if (a == "--zoom") zooms = floats(next());
        else if (a == "--px") { const auto v = floats(next()); if (v.size() == 2) px = make_float2(v[0], v[1]); }
        else if (a == "--out") out = next();
        else if (a == "--w") w = std::stoi(next());
        else if (a == "--h") h = std::stoi(next());
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else if (a == "--test") test = next();
    }
    app.loadConfig();
    app.width = w;
    app.height = h;
    app.buildCandidates();
    if (test == "pert") return testPerturbation(app);
    app.init();
    setPNGCompression(1);
    std::filesystem::create_directories(std::filesystem::path(app.root) / out);
    std::vector<int> list;
    if (which == "all") for (int k = 0; k < int(app.cands.size()); ++k) list.push_back(k);
    else for (float f : floats(which)) list.push_back(int(f));
    for (int k : list) {
        app.setCandidate(k);
        const float2 dv = px.x >= 0.0f ? px : app.cands[k].dive;
        for (float z : zooms) {
            app.home();
            app.defaultLight();
            app.aimLight(app.homeFocus);
            if (z > 1.0f) {   // as a hand on the wheel would: twice closer a step, picking again under the same pixel
                D3 q;
                float3 n;
                bool ok = true;
                while (app.level < z * 0.999f) {
                    if (!app.pick(dv.x * w, dv.y * h, q, &n)) { ok = false; break; }
                    app.zoomAbout(q, std::log(std::min(2.0f, z / app.level)));
                }
                if (!ok || !app.pick(dv.x * w, dv.y * h, q, &n)) {
                    std::printf("  x%.0f: secilen noktada yuzey yok (x%.0f'de)\n", z, app.level);
                    continue;
                }
                app.aimLight(q, &n);
            }
            const double t0 = nowS();
            app.renderPreview();
            int left = spp;
            while (left > 0) {
                const int b = std::min(left, 8);
                app.accumulate(b);
                left -= b;
            }
            app.resolve();
            cudaDeviceSynchronize();
            const double t1 = nowS();
            char name[256];
            std::snprintf(name, sizeof(name), "%s/%d_%s_x%g", out.c_str(), k, app.cands[k].id.c_str(), z);
            app.save(app.root + "/" + name);
            const cudaError_t e = cudaGetLastError();
            std::printf("  x%-6g onizleme %.1f ms, yol %.1f ms/ornek, %d ornek %.1f sn%s -> %s.png\n", z, app.previewMs, app.pathMs,
                        spp, t1 - t0, e != cudaSuccess ? cudaGetErrorString(e) : "", name);
        }
    }
    app.release();
    return 0;
}
