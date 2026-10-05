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
        c.shape.relax = 0.9f;   // 0.8 at first; 0.9: no holes, no specks at 10,000 times, 15% faster (measured)
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
        // stood up as a medallion: the cut (the hexagon of stars) faces the viewer, its flat lower edge on the plinth
        // (fractal (1,1,-2), along the cut plane towards an edge's middle, turned up); seen at a slant
        c.alignUp = make_float3(1, 1, -2);
        c.cutDepth = 0.02f;
        c.homeYaw = -30.0f;
        c.homePitch = -10.0f;
        c.frame = 0.85f;
        c.shape.iters = 16;   // the orbit's most (the pixel rule picks per point, hk::itersAt)
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
    s.split = cfg.i(p + "split", s.split);
    s.k2 = cfg.v3(p + "k2", s.k);
    s.a2 = cfg.f(p + "a2", s.a);
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
    c.alignUp = cfg.v3(p + "up", c.alignUp);
    c.frame = cfg.f(p + "frame", c.frame);
}

void HeykelApp::init() {
    const size_t n = size_t(width) * height;
    cudaMalloc(reinterpret_cast<void**>(&accum), n * sizeof(float4));
    cudaMalloc(reinterpret_cast<void**>(&output), n * sizeof(uchar4));
    cudaMalloc(reinterpret_cast<void**>(&devSteps), sizeof(hk::KStep) * hk::kMaxSteps);
    cudaMalloc(reinterpret_cast<void**>(&devMSteps), sizeof(hk::MStep) * hk::kMaxSteps);
    cudaMemset(accum, 0, n * sizeof(float4));
}

void HeykelApp::release() {
    if (accum) cudaFree(accum);
    if (output) cudaFree(output);
    if (devSteps) cudaFree(devSteps);
    devSteps = nullptr;
    if (devMSteps) cudaFree(devMSteps);
    devMSteps = nullptr;
    accum = nullptr;
    output = nullptr;
}

// The fit: rays from far outside towards the centre (Fibonacci directions) find the outer surface; its farthest
// point is the bound, its lowest (in the turned frame) where the sculpture stands.
void HeykelApp::setCandidate(int i) {
    cand = std::clamp(i, 0, int(cands.size()) - 1);
    const HeykelCandidate& c = cands[cand];
    hk::Shape s = c.shape;
    hk::M3 R = rotation(c.euler);   // fractal -> world (turn)
    if (length(c.alignUp) > 0.0f) {   // first the given direction up, then the Euler turn about the world's axes
        const float3 u = normalize(c.alignUp), up = make_float3(0, 1, 0);
        const float3 ax = cross(u, up);
        const hk::M3 A = length(ax) > 1e-6f ? axisAngle(ax, std::acos(std::clamp(dot(u, up), -1.0f, 1.0f))) : hk::M3();
        hk::M3 m;   // R * A
        const hk::M3 At = transpose(A);
        m.r0 = make_float3(dot(R.r0, At.r0), dot(R.r0, At.r1), dot(R.r0, At.r2));
        m.r1 = make_float3(dot(R.r1, At.r0), dot(R.r1, At.r1), dot(R.r1, At.r2));
        m.r2 = make_float3(dot(R.r2, At.r0), dot(R.r2, At.r1), dot(R.r2, At.r2));
        R = m;
    }
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

int HeykelApp::sculpture() const {
    const std::string want = cfg.values.count("heykel") ? cfg.values.at("heykel") : std::string("yildiz");
    for (int k = 0; k < int(cands.size()); ++k)
        if (cands[k].id == want) return k;
    return 0;
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
    trail.assign(1, TrailPoint{camPos, focus, yaw, pitch, level});
    rewinding = false;
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
    for (int i = 0; i < s.iters; ++i) {
        const float3 c = i < s.split ? s.k : s.k2;
        const double kx = c.x, ky = c.y, kz = c.z;
        p = d3(2.0 * std::clamp(p.x, -kx, kx) - p.x, 2.0 * std::clamp(p.y, -ky, ky) - p.y, 2.0 * std::clamp(p.z, -kz, kz) - p.z);
        const double r2 = dot(p, p);
        const double k = std::max(double(i < s.split ? s.a : s.a2) / r2, 1.0);
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
    if (s.fractal == hk::kCrystal && deep()) {   // the Menger's orbit in doubles
        D3 A = mulD(s.orient, W - d3(s.center)) * (1.0 / s.scale);
        w.pert = 1;
        w.n = std::min(s.iters, hk::kMaxSteps);
        w.A0 = f3(A);
        const D3 nrm = d3(s.cutSize) * (1.0 / length(d3(s.cutSize)));
        w.cutPlaneRel = float(dot(A, nrm) - s.cutOffset);
        w.mst = hostMSteps;
        for (int i = 0; i < w.n; ++i) {
            hk::MStep& k = hostMSteps[i];
            k.A = f3(A);
            int flags = 0;
            if (A.x < 0) flags |= 1;
            if (A.y < 0) flags |= 2;
            if (A.z < 0) flags |= 4;
            A = d3(std::fabs(A.x), std::fabs(A.y), std::fabs(A.z));
            k.D1 = float(A.x - A.y);
            if (A.x < A.y) { std::swap(A.x, A.y); flags |= 8; }
            k.D2 = float(A.x - A.z);
            if (A.x < A.z) { std::swap(A.x, A.z); flags |= 16; }
            k.D3 = float(A.y - A.z);
            if (A.y < A.z) { std::swap(A.y, A.z); flags |= 32; }
            A = A * 3.0 - d3(2.0, 2.0, 2.0);
            k.E = float(A.z + 1.0);
            if (A.z < -1.0) { A.z += 2.0; flags |= 64; }
            k.flags = flags;
            k.An = f3(A);
        }
        w.AN = f3(A);
        return;
    }
    if (s.fractal != hk::kKleinian) return;
    // the anchor's orbit in doubles
    const D3 A0 = mulD(s.orient, W - d3(s.center)) * (1.0 / s.scale);
    w.pert = 1;
    w.n = std::min(s.iters, hk::kMaxSteps);
    w.A0 = f3(A0);
    w.cutSign = make_float3(A0.x < 0 ? -1.0f : 1.0f, A0.y < 0 ? -1.0f : 1.0f, A0.z < 0 ? -1.0f : 1.0f);
    w.cutRel = make_float3(float(std::fabs(A0.x) - s.cutSize.x), float(std::fabs(A0.y) - s.cutSize.y), float(std::fabs(A0.z) - s.cutSize.z));
    D3 A = A0;
    w.st = hostSteps;
    for (int i = 0; i < w.n; ++i) {
        hk::KStep& k = hostSteps[i];
        const float3 cc = i < s.split ? s.k : s.k2;
        const double C[3] = {cc.x, cc.y, cc.z};
        const double size = i < s.split ? s.a : s.a2;
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
        k.r2m = float(r2 - size);
        k.size = float(size);
        const double kA = r2 < size ? size / r2 : 1.0;
        A = Af * kA;
        k.An = f3(A);
    }
    w.AN = f3(A);
}

// The iterations follow the zoom (the pixel rule for the formula): each one adds structure about a few times finer,
// so one or two more a decade keep the detail from running out (at 10,000 times 10 left rings floating in the dark,
// 14-18 kept the lace, PLAN G1). Below a pixel the extra ones change nothing that is drawn.
// The iterations follow the pixel's footprint, point by point (hk::itersAt): a point r from the camera takes
// base + 2 log10(home distance / r) (at the first view 10, at 10,000 times 18 on the focus; at 10,000 times with only 10
// the lace had dissolved into rings, PLAN G1). Farther points take fewer, blended between two counts: no lace finer
// than a pixel in the far layers. This is the orbit's length: enough for points ~30 times nearer than the focus.
int HeykelApp::itersNow() const {
    const int base = cands[cand].shape.iters;
    if (!deep()) return base;
    if (world.s.fractal == hk::kCrystal) {   // the pixel rule at a point 30 times nearer than the focus, and two more
        hk::World w = world;
        setLod(w);
        const double r = std::max(length(camPos - focus) / 30.0, 1e-20);
        const double m = w.lodBase + w.lodK * std::log10(w.lodRef / r);
        return std::clamp(int(std::ceil(m)) + 2, 2, hk::kMaxSteps);
    }
    const double r = std::max(length(camPos - focus) / 30.0, 1e-20);
    const double m = base + cfg.f("iters.per.decade", 2.0f) * std::log10(length(homePos - homeFocus) / r);
    return std::clamp(int(std::ceil(m)) + 2, base, hk::kMaxSteps);
}

void HeykelApp::setLod(hk::World& w) const {
    w.lod = deep() ? 1 : 0;
    w.camRel = f3(camPos - focus);
    if (world.s.fractal == hk::kCrystal) {
        // The Menger's level n has holes 2 / 3^n across (fractal units; the cube is 2). A point r from the camera, whose
        // pixel's footprint is r pixAngle / scale there, takes the n whose smallest holes are lod.pixels pixels: none finer
        // is drawn (they would only be grain), the next ones open smoothly as you come closer. Özcan: "vektörde olduğu gibi
        // aşırı net"
        const double pixAngle = 2.0 * std::tan(0.5 * fovDeg * kDeg) / height;
        const double k = cfg.f("lod.pixels", 2.0f);
        w.lodRef = 1.0f;
        w.lodK = float(1.0 / std::log10(3.0));
        w.lodBase = float(std::log(2.0 * world.s.scale / (k * pixAngle)) / std::log(3.0));
        w.lodMin = 1.0f;
        return;
    }
    w.lodRef = float(length(homePos - homeFocus));
    w.lodK = cfg.f("iters.per.decade", 2.0f);
    w.lodBase = float(cands[cand].shape.iters) + cfg.f("iters.offset", 0.0f);
    w.lodMin = cfg.f("iters.min", 6.0f);
}

namespace {

// the Menger in doubles to a fractional level (as hk::deMengerLod)
double deMengerDLod(D3 z, double mf) {
    const int m0 = int(mf);
    double sc = 1.0;
    for (int i = 0; i < m0; ++i) {
        z = d3(std::fabs(z.x), std::fabs(z.y), std::fabs(z.z));
        if (z.x < z.y) std::swap(z.x, z.y);
        if (z.x < z.z) std::swap(z.x, z.z);
        if (z.y < z.z) std::swap(z.y, z.z);
        z = z * 3.0 - d3(2, 2, 2);
        if (z.z < -1.0) z.z += 2.0;
        sc *= 3.0;
    }
    const D3 a = d3(std::fabs(z.x), std::fabs(z.y), std::fabs(z.z));
    const double dc = std::min(std::max(a.x, a.y), std::min(std::max(a.y, a.z), std::max(a.z, a.x))) - (mf - m0) / 3.0;
    return std::max(sdBoxD(z, d3(1, 1, 1)), -dc) / sc;
}

// the Kleinian in doubles stopped after mf iterations (as hk::deKleinianLod)
double deKleinianDLod(D3 p, const hk::Shape& s, double mf) {
    const int m0 = int(mf);
    const double f = mf - m0;
    auto end = [&](D3 z, double df) {
        const double rxy = std::sqrt(z.x * z.x + z.y * z.y);
        return std::max(rxy - s.b, std::fabs(rxy * z.z) / length(z)) / df;
    };
    double df = 1.0, d0 = 0.0;
    for (int i = 0; i <= m0; ++i) {
        if (i == m0) d0 = end(p, df);
        const float3 c = i < s.split ? s.k : s.k2;
        p = d3(2.0 * std::clamp(p.x, -double(c.x), double(c.x)) - p.x, 2.0 * std::clamp(p.y, -double(c.y), double(c.y)) - p.y,
               2.0 * std::clamp(p.z, -double(c.z), double(c.z)) - p.z);
        const double k = std::max(double(i < s.split ? s.a : s.a2) / dot(p, p), 1.0);
        p = p * k;
        df *= k;
    }
    return d0 + (end(p, df) - d0) * f;
}

}   // namespace

double HeykelApp::deHost(D3 p) const {
    if (!deep()) return hk::deScene(f3(p), world);   // world's anchor is the origin
    const hk::Shape& s = world.s;
    if (s.fractal == hk::kCrystal) {
        double d;
        const D3 q = p - d3(s.center);
        const double rw = double(s.boundR) * s.scale, bd = length(q) - rw;
        if (bd > 0.05 * rw) {
            d = bd;
        } else {
            hk::World w = world;
            setLod(w);
            const double r = std::max(length(p - camPos), 1e-20);
            const double mf = std::clamp(w.lodBase + w.lodK * std::log10(w.lodRef / r), double(w.lodMin), hk::kMaxSteps - 1.001);
            D3 pf = mulD(s.orient, q) * (1.0 / s.scale);
            d = deMengerDLod(pf, mf);
            if (s.cut == 4) {
                const D3 nrm = d3(s.cutSize) * (1.0 / length(d3(s.cutSize)));
                d = std::max(d, dot(pf, nrm) - s.cutOffset);
            }
            d *= s.scale;
        }
        d = std::max(d, double(s.floorCut) - p.y);
        d = std::min(d, sdBoxD(p - d3(world.plinthC), d3(world.plinthH)));
        return std::min(d, p.y - world.floorY);
    }
    double d;
    const D3 q = p - d3(s.center);
    const double rw = double(s.boundR) * s.scale, bd = length(q) - rw;
    if (bd > 0.05 * rw) {
        d = bd;
    } else {
        // the same count as the GPU's (hk::itersAt), from the camera's distance
        const double r = std::max(length(p - camPos), 1e-20);
        const double mf = std::clamp(s.iters + cfg.f("iters.offset", 0.0f) + cfg.f("iters.per.decade", 2.0f) * std::log10(length(homePos - homeFocus) / r),
                                     double(cfg.f("iters.min", 6.0f)), hk::kMaxSteps - 1.001);
        const D3 pf = mulD(s.orient, q) * (1.0 / s.scale);
        d = deKleinianDLod(pf, s, mf);
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
    setLod(v.w);
    // perturbation only where floats fall short (it costs ~2-3 times the straight estimate): at 1,000 times the straight
    // one errs by 0.02-0.05 of a pixel (--test pert), the switch cannot be seen
    if (level < cfg.f("pert.from", 1000.0f)) v.w.pert = 0;
    if (v.w.pert && v.w.s.fractal == hk::kCrystal) {
        heykelSetOrbits(hostMSteps, nullptr, v.w.n);
        v.w.mst = nullptr;   // the kernels read the constant copy
    } else if (v.w.pert) {
        heykelSetOrbits(nullptr, hostSteps, v.w.n);
        v.w.st = nullptr;
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
    v.softKey = cfg.i("shadow.soft", 1);
    v.shadowSteps = cfg.i("shadow.steps", 160);
    v.shadowReach = cfg.f("shadow.reach", 100.0f) * float(length(camPos - focus));
    v.normalScale = cfg.f("normal.scale", 1.0f);
    v.filterRadius = cfg.f("filter.radius", 1.5f);
    v.tmax = 30.0f;
    {   // the key: a spot
        HeykelLight& l = v.key;
        l.pos = f3(lightTarget + d3(lampDir) * lightDist - focus);
        l.axis = -lampDir;
        l.r = lightRadius;
        l.le = cfg.f("light.e", 3.14159f) * lightDist * lightDist / (3.14159f * lightRadius * lightRadius);
        const float outer = std::atan(litRadius / lightDist), inner = std::atan(litRadius * cfg.f("light.spot.soft", 0.55f) / lightDist);
        l.coneOuter = std::cos(outer);
        l.coneInner = std::cos(inner);
        l.color = cfg.v3("light.color", make_float3(1.0f, 0.93f, 0.84f));
    }
    // the photographer puts the ring light on when going macro: faint at the first view (the room's dark, the plinth's
    // shaded side), full from ten times on; the ambient the same way (deep in the lattice the white walls light each other)
    const float ramp = std::clamp(std::log10(std::max(level, 1.0f)), 0.0f, 1.0f);
    {   // the eye light: on the lens, a little above it; its fall softened within ~eye.soft focus distances of it
        const float d = float(length(camPos - focus));
        v.eyePos = v.camPos + v.camUp * (cfg.f("eye.up", 0.05f) * d);
        v.eyeD = d;
        v.eyeC = cfg.f("eye.soft", 0.35f) * d;
        v.eyeE = (cfg.f("eye.first", 0.025f) + (cfg.f("eye", 0.12f) - cfg.f("eye.first", 0.025f)) * ramp) * cfg.f("light.e", 3.14159f);
        v.eyeColor = cfg.v3("eye.color", make_float3(0.92f, 0.96f, 1.0f));
    }
    v.fillLe = cfg.f("fill", 0.03f) * (1.0f - ramp);
    v.fillColor = cfg.v3("fill.color", make_float3(0.80f, 0.88f, 1.0f));
    v.ambient = (cfg.f("ambient.first", 0.0f) + (cfg.f("ambient", 0.03f) - cfg.f("ambient.first", 0.0f)) * ramp) * cfg.f("light.e", 3.14159f) / 3.14159f;
    v.albedo[0] = cfg.v3("albedo", make_float3(0.80f, 0.78f, 0.74f));
    v.albedo[1] = cfg.v3("albedo.plinth", make_float3(0.16f, 0.16f, 0.16f));
    v.albedo[2] = cfg.v3("albedo.floor", make_float3(0.035f, 0.035f, 0.035f));
    v.exposure = std::exp2(ev + cfg.f("ev.first", cfg.f("ev", 0.0f)) + (cfg.f("ev", 0.0f) - cfg.f("ev.first", cfg.f("ev", 0.0f))) * ramp);
    {   // a macro lens: the aperture a fixed fraction of the focus distance (the same blur at every zoom, in angle)
        const D3 toF = focus - camPos;
        v.focusDist = std::max(float(dot(toF, d3(v.camFwd))), 1e-12f);
        v.aperture = dof ? cfg.f("dof", 0.003f) * float(length(toF)) : 0.0f;
    }
    return v;
}

// the cursor's ray marched on the host in doubles (the same threshold as the pictures: half a pixel's footprint)
bool HeykelApp::pick(float px, float py, D3& hit, float3* normal, bool* sculpture) const {
    float3 r, u, f;
    cameraBasis(r, u, f);
    const double tanHalf = std::tan(0.5 * fovDeg * kDeg);
    const double aspect = double(width) / height;
    const double sx = (px / width * 2.0 - 1.0) * tanHalf * aspect, sy = (1.0 - py / double(height) * 2.0) * tanHalf;
    D3 rd = d3(f) + d3(r) * sx + d3(u) * sy;
    return pickRay(rd * (1.0 / length(rd)), hit, normal, sculpture);
}

bool HeykelApp::pickRay(D3 rd, D3& hit, float3* normal, bool* sculpture) const {
    const double tanHalf = std::tan(0.5 * fovDeg * kDeg);
    const double slope = 2.0 * tanHalf / height * cfg.f("detail", 0.5f);
    const int steps = cfg.i("steps", 400) * 2;
    double t = 0.0;
    for (int i = 0; i < steps; ++i) {
        const D3 p = camPos + rd * t;
        const double d = deHost(p);
        if (d < slope * t) {
            hit = p;
            if (sculpture)   // not the plinth, not the floor
                *sculpture = sdBoxD(p - d3(world.plinthC), d3(world.plinthH)) > 2.0 * slope * t && p.y - world.floorY > 2.0 * slope * t;
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

// How deep a point may be zoomed into. The Kleinian (perturbed) to zoom.max, except where nothing but a plane is left
// at depth: the block's flat cut faces and the plinth (the study's dives found a grey nothing there); the others to
// what floats carry.
float HeykelApp::zoomCapAt(D3 q) const {
    if (!deep()) return zoomCap();
    if (q.y < 0.0005) return cfg.f("zoom.max.plane", 30.0f);   // the plinth and the floor
    const hk::Shape& s = world.s;
    if (s.cut == 2) {
        const D3 a = mulD(s.orient, q - d3(s.center)) * (1.0 / s.scale);
        const double tol = 2e-3;
        if (std::fabs(a.x) > s.cutSize.x - tol || std::fabs(a.y) > s.cutSize.y - tol || std::fabs(a.z) > s.cutSize.z - tol)
            return cfg.f("zoom.max.plane", 30.0f);
    }
    return zoomCap();
}

// How far the camera can move from a towards b before the sculpture is within `clearance` of it (Ozcan 2026-10-05:
// turned round inside the lattice and zoomed out, the camera went backwards through a wall: "kamera modelin icine
// girememeli"). A march along the segment in doubles; the whole length when it is clear.
double HeykelApp::freePath(D3 a, D3 b, double clearance) const {
    const D3 ab = b - a;
    const double len = length(ab);
    if (len <= 0.0) return 0.0;
    const D3 dir = ab * (1.0 / len);
    double t = 0.0;
    for (int i = 0; i < 400; ++i) {
        const double d = deHost(a + dir * t);
        if (d < clearance) return std::max(0.0, t - clearance);
        t += std::max(d - clearance * 0.5, clearance * 0.25) * world.s.relax;
        if (t >= len) return len;
    }
    return t;
}

// Where the wheel zooms: the sculpture under the cursor; when the cursor is on the dark room or the plinth, the
// sculpture's point nearest to it on the screen (rays towards its middle and around it). Never a point in the air: the
// camera flew off towards it and the sculpture was lost (Ozcan 2026-10-05: "modele yakinlasayim derken modeli
// kaybedebiliyoruz").
bool HeykelApp::zoomTarget(float px, float py, D3& q) const {
    bool on = false;
    if (pick(px, py, q, nullptr, &on) && on) return true;
    float3 r, u, f;
    cameraBasis(r, u, f);
    const D3 c = focus;   // on the sculpture (or its middle, at the first view)
    const double rw = double(world.s.boundR) * world.s.scale;
    const double tanHalf = std::tan(0.5 * fovDeg * kDeg), aspect = double(width) / height;
    const D3 cur = d3(f) + d3(r) * ((px / width * 2.0 - 1.0) * tanHalf * aspect) + d3(u) * ((1.0 - py / double(height) * 2.0) * tanHalf);
    // from the focus towards the cursor in steps: the first ray that meets the sculpture
    const D3 toC = c - camPos;
    const double dc = length(toC);
    const D3 dirC = toC * (1.0 / dc), dirCur = cur * (1.0 / length(cur));
    for (int k = 0; k <= 8; ++k) {
        const double a = 1.0 - k / 8.0;   // from the cursor's side back to the focus
        D3 dir = dirC * (1.0 - a) + dirCur * a;
        dir = dir * (1.0 / length(dir));
        if (pickRay(dir, q, nullptr, &on) && on) return true;
    }
    for (int k = 0; k < 12; ++k) {   // the focus in a hole: around it
        const double ph = k * 6.2831853 / 12.0, rr = (k < 6 ? 0.15 : 0.35) * std::min(rw, dc);
        D3 dir = toC + d3(r) * (std::cos(ph) * rr) + d3(u) * (std::sin(ph) * rr);
        dir = dir * (1.0 / length(dir));
        if (pickRay(dir, q, nullptr, &on) && on) return true;
    }
    return false;
}

// The leash (Ozcan 2026-10-05: "modelin belli bir cevresinde kamera olsun, daha uzaga veya saga sola kacamasin"): the
// camera stays inside the sphere about the first view's centre through the first view's camera. Zooming in about a
// point far down a tunnel and then out again used to carry it off several times that far, into the dark room.
double HeykelApp::leashRadius() const {
    return length(homePos - homeFocus) * cfg.f("camera.leash", 1.0f);
}

// How much of the way from a to b is inside the leash (1: all of it).
double HeykelApp::leashFraction(D3 a, D3 b) const {
    const double R = leashRadius();
    const D3 f = a - homeFocus, d = b - a;
    if (dot(b - homeFocus, b - homeFocus) <= R * R) return 1.0;
    const double dd = dot(d, d), fd = dot(f, d), c = dot(f, f) - R * R;
    if (dd <= 0.0) return 1.0;
    if (c >= 0.0) return fd < 0.0 ? 1.0 : 0.0;   // already on the edge: inwards only
    return std::clamp((-fd + std::sqrt(std::max(fd * fd - dd * c, 0.0))) / dd, 0.0, 1.0);
}

// A fraction k of the way back to the first view: the camera round its centre (an arc, not a chord that could cut
// through the sculpture), its direction and the focus with it; k = 1 lands on it. No faster than `maxMove` (camera
// travel; as many radians of turn per first-view distance): what the arc could not do earlier is not made up in one
// jump. k comes back as done. False: the way is not clear.
bool HeykelApp::homeStep(double& k, D3& q, double maxMove) {
    k = std::clamp(k, 0.0, 1.0);
    const D3 a = camPos - homeFocus, b = homePos - homeFocus;
    const double ra = length(a), rb = length(b);
    auto at = [&](double kk) {
        D3 dir;
        if (ra < 1e-12) {
            dir = b * (1.0 / rb);
        } else {
            const D3 ua = a * (1.0 / ra), ub = b * (1.0 / rb);
            const double w = std::acos(std::clamp(dot(ua, ub), -1.0, 1.0));
            if (w < 1e-6) {
                dir = ub;
            } else if (w > 3.14159 - 1e-3) {   // right behind it: round the vertical
                const double ang = kk * w, c = std::cos(ang), s = std::sin(ang);
                dir = d3(ua.x * c - ua.z * s, ua.y, ua.x * s + ua.z * c);
            } else {
                dir = ua * (std::sin((1.0 - kk) * w) / std::sin(w)) + ub * (std::sin(kk * w) / std::sin(w));
            }
        }
        return homeFocus + dir * (ra + (rb - ra) * kk);
    };
    float dyaw = homeYaw - yaw;
    while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
    while (dyaw < -3.14159265f) dyaw += 6.2831853f;
    const double turn = (std::fabs(double(dyaw)) + std::fabs(double(homePitch - pitch))) * rb;   // as a travel
    for (int i = 0; i < 3; ++i) {   // the arc is near straight over a step: a proportional cut lands close
        const double move = std::max(length(at(k) - camPos), turn * k);
        if (move <= maxMove * 1.0001) break;
        k *= maxMove / move;
    }
    const D3 drawn = at(k);
    const D3 qk = q + (homeFocus - q) * k;
    const double clearance = length(drawn - qk) * cfg.f("camera.clearance", 0.05f);
    if (deHost(drawn) < clearance || freePath(camPos, drawn, clearance) < length(drawn - camPos) * 0.999) return false;
    camPos = drawn;
    yaw += dyaw * float(k);
    pitch += (homePitch - pitch) * float(k);
    q = qk;
    return true;
}

// The camera's pose onto the trail. A leg is kept only when the camera could not have gone straight from the point
// before it (a wall between): the trail stays short, and every leg of it is a clear line.
void HeykelApp::record() {
    const TrailPoint t{camPos, focus, yaw, pitch, level};
    if (trail.empty()) {
        trail.push_back(t);
        return;
    }
    if (trail.size() >= 2) {
        const TrailPoint& a = trail[trail.size() - 2];
        const double cl = 0.25 * std::min(deHost(a.pos), deHost(t.pos));
        const double len = length(t.pos - a.pos);
        if (cl > 0.0 && (len <= 0.0 || freePath(a.pos, t.pos, cl) >= len * 0.999)) {
            trail.back() = t;
            return;
        }
    }
    if (trail.size() < 65536) trail.push_back(t);
}

// Back the way the camera came (Ozcan 2026-10-05 asked for it to stay near the sculpture; deep in the lattice with a
// wall right behind, the wheel then did nothing at all). A backward zoom of -logAmount: over the legs the zoom grew on,
// at the rate it grew (a leg that went from 10 to 10,000 times is gone back over as it came, slowly where it is deep);
// over the legs it did not (turns), at a backward zoom's pace (camera.retrace times it). The focus and the direction
// come along, so the first point of the trail is the first view itself.
bool HeykelApp::retrace(float logAmount) {
    double left = -double(logAmount);
    const double homeDist = length(homePos - homeFocus), pace = cfg.f("camera.retrace", 1.5f);
    bool moved = false;
    auto wrap = [](double a) {
        while (a > 3.14159265358979) a -= 6.28318530717959;
        while (a < -3.14159265358979) a += 6.28318530717959;
        return a;
    };
    while (left > 1e-9 && trail.size() >= 2) {
        // a short cut: when the point before the next one is in clear sight, the next one is no longer on the way
        while (trail.size() >= 3) {
            const D3 a = trail[trail.size() - 3].pos, c = trail.back().pos;
            const double cl = 0.25 * std::min(deHost(a), deHost(c)), len = length(a - c);
            if (!(cl > 0.0 && freePath(c, a, cl) >= len * 0.999)) break;
            trail.erase(trail.end() - 2);
        }
        TrailPoint& C = trail.back();
        const TrailPoint& P = trail[trail.size() - 2];
        const double Lc = C.level, Lp = std::min(double(P.level), Lc);
        double g, L = Lc;
        // what the leg costs in backward zoom: its zoom change, its length at the scale it was seen at, its turn;
        // a zoom leg goes back as it came (the distance to what was zoomed on goes as 1 / zoom), the others evenly
        const double need = std::log(Lc / Lp);
        const double scale = std::max(std::max(length(P.pos - P.focus), length(C.pos - C.focus)), 1e-300);
        const double walk = std::max(length(P.pos - C.pos) / scale,
                                     std::fabs(wrap(double(P.yaw) - C.yaw)) + std::fabs(double(P.pitch) - C.pitch)) / pace;
        const double cost = std::max(need, walk);
        if (left >= cost) {
            left -= cost;
            g = 1.0;
            L = Lp;
        } else if (need >= walk) {
            L = Lc * std::exp(-left);
            g = (1.0 / L - 1.0 / Lc) / (1.0 / Lp - 1.0 / Lc);
            left = 0.0;
        } else {
            g = left / cost;
            L = Lc * std::pow(Lp / Lc, g);
            left = 0.0;
        }
        TrailPoint n;
        n.pos = C.pos + (P.pos - C.pos) * g;
        n.focus = C.focus + (P.focus - C.focus) * g;
        n.yaw = float(C.yaw + wrap(double(P.yaw) - C.yaw) * g);
        n.pitch = float(C.pitch + (double(P.pitch) - C.pitch) * g);
        n.level = float(L);
        if (g >= 1.0) trail.pop_back();
        trail.back() = n;
        camPos = n.pos;
        focus = n.focus;
        yaw = n.yaw;
        pitch = n.pitch;
        level = n.level;
        moved = true;
    }
    if (trail.size() == 1 && length(camPos - homePos) <= homeDist * 1e-9) home();
    return moved;
}

bool HeykelApp::zoomAbout(D3 q, float logAmount) {
    float target = level * std::exp(logAmount);
    const float cap = zoomCapAt(q);
    if (target > cap) logAmount = std::log(std::max(cap / level, 1.0f));
    const D3 q0 = q, before = camPos;
    const double r = length(camPos - q);
    const float wasYaw = yaw, wasPitch = pitch;
    float lv = level * std::exp(logAmount);
    if (logAmount >= 0.0f) {   // in: towards a point on the sculpture, inside the leash all the way (it is round)
        camPos = q + (camPos - q) * std::exp(-double(logAmount));
        // the zoom tells how near the camera is: towards a wall far down a tunnel it does not grow until the camera is
        // that near (it used to read 10,000 times with the camera hundreds of times that far off, and the way out then
        // took it as far off as the zoom said, out of the room)
        const double nearAs = cfg.f("zoom.true", 1.5f) * length(homePos - homeFocus) / std::max(length(camPos - q), 1e-300);
        lv = std::max(level, std::min(lv, float(std::min(nearAs, 1e30))));
        rewinding = false;
    } else if (rewinding) {   // once on the way back it keeps to it (a leg won and lost again is no way home)
        return retrace(logAmount);
    } else {
        lv = std::max(lv, 1.0f);
        // backwards along the line from q; the camera bumps into the sculpture instead of passing through it: with a
        // wall behind, it goes back the way it came instead
        D3 want = q + (camPos - q) * std::exp(-double(logAmount));
        const double travel = length(want - camPos);
        if (freePath(camPos, want, r * cfg.f("camera.clearance", 0.05f)) < travel) {
            rewinding = true;
            return retrace(logAmount);
        }
        // and it stops at the leash
        const double t = leashFraction(camPos, want);
        const bool leashed = t < 1.0;
        camPos = camPos + (want - camPos) * t;
        // the last of the way out leads back to the first view (position, direction, focus) instead of drifting off to
        // a side: from zoom.home.from times on, or as soon as the leash is in the way, a share of what is left, so that
        // it lands on the first view as the zoom reaches 1 (no jump at the end)
        const float from = cfg.f("zoom.home.from", 3.0f);
        auto u = [&](float l) {   // how far along the way home the zoom l is: 0 at `from`, 1 at the first view
            const double x = std::clamp(double((from - l) / (from - 1.0f)), 0.0, 1.0);
            return x * x * (3.0 - 2.0 * x);
        };
        double k = 0.0;
        if (level > 1.0f) {
            if (u(level) < 1.0) k = (u(lv) - u(level)) / (1.0 - u(level));
            if (leashed) k = std::max(k, 1.0 - std::log(double(lv)) / std::log(double(level)));
        } else {   // at the first zoom already, turned away from the first view: a glide back
            k = 1.0 - std::exp(6.0 * double(logAmount));
        }
        bool homed = false;
        if (k > 0.0) {
            const double a = length(camPos - homePos) / length(homePos - homeFocus);
            if (k >= 0.999 || (lv <= 1.0f && a < 1e-3)) k = 1.0;
            homed = homeStep(k, q, length(homePos - homeFocus) * -double(logAmount) * cfg.f("camera.retrace", 1.5f));
            if (homed && k >= 1.0) {
                home();
                return true;
            }
            if (!homed && (leashed || level <= 1.0f || lv <= 1.0f)) {   // the arc is blocked: the way it came
                camPos = before;
                yaw = wasYaw;
                pitch = wasPitch;
                rewinding = true;
                return retrace(logAmount);
            }
        }
        if (length(camPos - before) <= r * 1e-6) return false;
        if (!homed) lv = std::max(level * float(length(camPos - q) > 0.0 ? r / length(camPos - q) : 1.0), 1.0f);
    }
    // the lamp goes with the camera, as a macro photographer's does: scaled about the same point (its light on what
    // is seen stays the same while zooming; keepLampClear keeps its line clear, turning it smoothly)
    const double s = length(camPos - q) / std::max(r, 1e-300);
    lightTarget = q + (lightTarget - q0) * s;
    lightDist *= float(s);
    lightRadius *= float(s);
    litRadius *= float(s);
    level = lv;
    focus = q;
    record();
    return true;
}

// The lamp seen from where it points? A ray from the target (lifted off the surface) to the lamp's centre.
bool HeykelApp::lampClear(D3 target, float3 n, float3 dir, double dist) const {
    const double eps = dist * 2e-3;
    const D3 o = target + d3(n) * (eps * 2.0);
    const D3 rd = d3(dir);
    double t = 0.0;
    for (int i = 0; i < 300 && t < dist; ++i) {
        const double d = deHost(o + rd * t);
        if (d < eps * 0.5) return false;
        t += d * world.s.relax;
    }
    return true;
}

// When the lamp's line to its target is blocked (deep in the lattice the nearest arch is often in the way), look
// for a clear one near the direction it was given: rings of 15, 30, 45, 60 degrees around it.
void HeykelApp::clearLamp(const float3* normal) {
    float3 n = normal ? *normal : make_float3(0, 1, 0);
    if (!normal) {   // the target's normal from the estimate
        const double e = std::max(lightDist * 1e-3, 1e-12);
        const D3 p = lightTarget;
        const D3 g = d3(deHost(p + d3(e, 0, 0)) - deHost(p - d3(e, 0, 0)), deHost(p + d3(0, e, 0)) - deHost(p - d3(0, e, 0)),
                        deHost(p + d3(0, 0, e)) - deHost(p - d3(0, 0, e)));
        const double l = length(g);
        if (l > 0.0) n = f3(g * (1.0 / l));
    }
    float3 from = lightDir;
    if (dot(from, n) < 0.2f) from = normalize(from + n * (0.2f - dot(from, n)) * 1.5f);
    lampGoal = from;
    if (lampClear(lightTarget, n, from, lightDist)) return;
    float3 t, b;
    makeBasis(from, t, b);
    for (int ring = 1; ring <= 4; ++ring) {
        const float a = ring * 15.0f * kDeg;
        for (int k = 0; k < 8; ++k) {
            const float ph = (k + 0.5f * ring) * (6.2831853f / 8.0f);
            const float3 d = normalize(from * std::cos(a) + (t * std::cos(ph) + b * std::sin(ph)) * std::sin(a));
            if (dot(d, n) < 0.15f) continue;
            if (lampClear(lightTarget, n, d, lightDist)) {
                lampGoal = d;
                return;
            }
        }
    }
}

// The lamp's direction turns towards its goal at ~a third of the way a tenth of a second: a blocked line gives way to a
// clear one as a hand would move the lamp, never in one frame (Ozcan: the light "kipir kipir olup duzeliyor")
bool HeykelApp::turnLampTowardsGoal(float dt) {
    const float c = dot(lampDir, lampGoal);
    if (c > 0.99999f) return false;
    const float k = 1.0f - std::exp(-dt * cfg.f("light.follow", 4.0f));
    lampDir = normalize(lampDir + (lampGoal - lampDir) * k);
    if (dot(lampDir, lampGoal) > 0.99999f) lampDir = lampGoal;
    return true;
}

void HeykelApp::settleLight() {
    // the zoom has stopped: the lamp's target on what is being zoomed at, at the view's scale, a clear line
    aimLight(focus);
}

// While zooming: the lamp moves with the camera (zoomAbout), but deep in the lattice an arch comes between it and its
// target. One ray a frame says so; then the nearest clear direction (otherwise only the flat light from the camera is
// left and the preview turns into a grey haze of arches, as the study saw at 64 times).
void HeykelApp::keepLampClear() {
    if (level <= 1.0f) return;
    const double e = std::max(double(lightDist) * 1e-3, 1e-12);
    const D3 p = lightTarget;
    const D3 g = d3(deHost(p + d3(e, 0, 0)) - deHost(p - d3(e, 0, 0)), deHost(p + d3(0, e, 0)) - deHost(p - d3(0, e, 0)),
                    deHost(p + d3(0, 0, e)) - deHost(p - d3(0, 0, e)));
    const double l = length(g);
    const float3 n = l > 0.0 ? f3(g * (1.0 / l)) : make_float3(0, 1, 0);
    if (!lampClear(lightTarget, n, lampGoal, lightDist)) clearLamp(&n);
}

// Every frame of a zoom: the lamp's target glides onto the point zoomed at (a surface: deep in the lattice a target
// left hanging in the air lit nothing, and the views went flat grey), its distance, size and pool follow the view, its
// line is kept clear. Nothing jumps: the target and the direction ease (light.follow), the rest scales continuously.
void HeykelApp::followLight(float dt) {
    const double k = 1.0 - std::exp(-double(dt) * cfg.f("light.follow", 4.0f));
    lightTarget = lightTarget + (focus - lightTarget) * k;
    const float d = float(length(camPos - lightTarget));
    lightDist = cfg.f("light.dist", 1.4f) * d;
    lightRadius = cfg.f("light.size", 0.035f) * lightDist;
    const float tanHalf = std::tan(0.5f * fovDeg * kDeg);
    const float ramp = std::clamp(std::log10(std::max(level, 1.0f)), 0.0f, 1.0f);
    const float spot = cfg.f("light.spot.first", 0.55f) + (cfg.f("light.spot", 0.9f) - cfg.f("light.spot.first", 0.55f)) * ramp;
    litRadius = spot * (2.0f * tanHalf * d);
    keepLampClear();
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
    // the leash: turning about a point off the middle could carry the camera out of it; it comes in along its line
    // to the focus instead (a little closer, the zoom with it)
    const D3 g = focus - homeFocus;
    const double R = leashRadius(), rr = dot(rel, rel), gr = dot(g, rel), gc = dot(g, g) - R * R;
    double lam = 1.0;
    if (dot(g + rel, g + rel) > R * R && gc < 0.0) lam = std::clamp((-gr + std::sqrt(std::max(gr * gr - rr * gc, 0.0))) / rr, 1e-3, 1.0);
    rel = rel * lam;
    const D3 p = focus + rel;
    // the camera bumps into the sculpture: the end clear, and the way there (a fast drag deep inside could step a wall)
    const double clearance = length(rel) * cfg.f("camera.clearance", 0.05f);
    if (deHost(p) < clearance || freePath(camPos, p, clearance) < length(p - camPos)) return false;
    camPos = p;
    yaw = newYaw;
    pitch = newPitch;
    level = std::min(level / float(lam), zoomCap());
    rewinding = false;
    record();
    return true;
}

void HeykelApp::defaultLight() {
    float3 r, u, f;
    cameraBasis(r, u, f);
    const float3 rel = cfg.v3("light.rel", make_float3(-0.78f, 0.55f, 0.30f));
    lightDir = normalize(r * rel.x + u * rel.y - f * rel.z);
    lampDir = lampGoal = lightDir;
}

void HeykelApp::aimLight(D3 target, const float3* normal) {
    lightTarget = target;
    const float d = float(length(camPos - target));
    lightDist = cfg.f("light.dist", 1.4f) * d;
    lightRadius = cfg.f("light.size", 0.035f) * lightDist;
    const float tanHalf = std::tan(0.5f * fovDeg * kDeg);
    // the spot's pool: narrow at the first view (it falls off on the plinth, the room stays dark), wide from ten times on
    // (deep in the lattice everything in view wants it)
    const float ramp = std::clamp(std::log10(std::max(level, 1.0f)), 0.0f, 1.0f);
    const float spot = cfg.f("light.spot.first", 0.55f) + (cfg.f("light.spot", 0.9f) - cfg.f("light.spot.first", 0.55f)) * ramp;
    litRadius = spot * (2.0f * tanHalf * d);
    clearLamp(normal);
    lampDir = lampGoal;   // a click: the lamp is put there at once
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
    lampDir = lampGoal = lightDir;   // the hand on it decides: no search while dragging
}

void HeykelApp::resetSamples() {
    cudaMemset(accum, 0, size_t(width) * height * sizeof(float4));
    samples = 0;
}

void HeykelApp::addSamples(int count, int stripe, int stripes) {
    if (count <= 0) return;
    const HeykelView v = view();
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    heykelSamples(v, accum, samples, count, stripe, stripes, 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, a, b);
    sampleMs = ms * stripes / count;
    cudaEventDestroy(a);
    cudaEventDestroy(b);
    if (stripe == stripes - 1) samples += count;   // a sample counts when its last rows are in
}

void HeykelApp::renderFrame() {
    const HeykelView v = view();
    const int base = cfg.i("frame.base", 4), edge = cfg.i("frame.edge", 8);
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    heykelFrame(v, accum, base, edge, cfg.f("frame.contrast", 0.08f), 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    cudaEventElapsedTime(&frameMs, a, b);
    cudaEventDestroy(a);
    cudaEventDestroy(b);
    samples = base;
}

void HeykelApp::resolve() { heykelResolve(view(), accum, output, 0); }

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
// --kararlilik zoom|isik: frames for tools/kararlilik.py. live_NNNN: what the window draws while something moves (the
// fixed pattern's first samples, as many as the live scene takes in ~25 ms); ref_NNNN: the same moment with 64.
// zoom: from `from` to `to` times towards the target in `frames` steps; isik: at `from` times, the lamp swung by ~1°
// a frame (a drag).
int stabilityFrames(HeykelApp& app, int cand, const std::string& mode, D3 target, bool hasTarget, float2 px, float from, float to, int frames,
                    const std::string& out) {
    app.setCandidate(cand);
    std::filesystem::create_directories(std::filesystem::path(app.root) / out);
    app.home();
    app.defaultLight();
    app.aimLight(app.homeFocus);
    if (!hasTarget && !app.pick(px.x * app.width, px.y * app.height, target)) {   // no target: what is under the pixel
        std::printf("secilen pikselde yuzey yok\n");
        return 1;
    }
    app.zoomAbout(target, std::log(from));
    app.settleLight();
    for (int f = 0; f < frames; ++f) {
        if (f > 0) {
            if (mode == "zoom") app.zoomAbout(target, std::log(to / from) / (frames - 1));
            else app.turnLight(0.017f, 0.004f);
        }
        app.renderFrame();   // as the window draws it
        const int n = app.samples;
        app.resolve();
        char name[512];
        std::snprintf(name, sizeof(name), "%s/%s/live_%04d", app.root.c_str(), out.c_str(), f);
        app.save(name);
        app.resetSamples();
        for (int k = 0; k < 8; ++k) app.addSamples(8);
        app.resolve();
        std::snprintf(name, sizeof(name), "%s/%s/ref_%04d", app.root.c_str(), out.c_str(), f);
        app.save(name);
        std::printf("  kare %d: x%.0f, canli %d ornek + kenar, kare %.1f ms\n", f, app.level, n, app.frameMs);
    }
    return 0;
}

// --test kamera: random walks of the hand (zoom in towards a random pixel anywhere on the screen, the dark room's
// corners too; turn; zoom out), the camera checked after every step: never inside the sculpture (Ozcan 2026-10-05 found
// it backing through a wall after turning round inside) and never away from it (zooming towards the dark room, the
// camera flew off and the sculpture was lost). Each walk ends with the wheel turned out in small steps, as the window
// does over its frames: the camera must reach the first view, without a jump on the way (--set test.seed=N for others).
int testCamera(HeykelApp& app) {
    app.setCandidate(app.sculpture());
    unsigned int seed = unsigned(app.cfg.f("test.seed", 777.0f));
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float((seed >> 8) * (1.0 / 16777216.0)); };
    int steps = 0, inside = 0, blockedOut = 0, blockedTurn = 0, homes = 0, lost = 0;
    double worst = 1e30, farthest = 0.0, jump = 0.0;
    int longest = 0;   // the most frame shares a walk took to get back
    const double homeDist = length(app.homePos - app.homeFocus);
    double worstIn = 1e30, worstOut = 1e30;   // the narrowest clearance zooming in / on the way back
    auto check = [&](int kind) {   // after every single move; kind 0 in, 1 turn, 2 out
        ++steps;
        farthest = std::max(farthest, length(app.camPos - app.homeFocus) / homeDist);
        const double r = length(app.camPos - app.focus);
        const double d = app.deHost(app.camPos) / r;   // the camera's clearance, as a fraction of its distance
        worst = std::min(worst, d);
        if (kind == 0) worstIn = std::min(worstIn, d);
        if (kind == 2) worstOut = std::min(worstOut, d);
        if (d <= 0.0) ++inside;
    };
    for (int walk = 0; walk < 40; ++walk) {
        app.home();
        for (int k = 0; k < 60; ++k) {
            const float op = rnd();
            if (op < 0.45f) {   // in, towards a random pixel anywhere
                D3 q;
                if (app.zoomTarget(rnd() * app.width, rnd() * app.height, q))
                    for (int j = 0; j < 4; ++j) { app.zoomAbout(q, 0.35f); check(0); }
            } else if (op < 0.75f) {   // turn, as a drag does: many small steps
                const float dy = (rnd() - 0.5f) * 0.08f, dp = (rnd() - 0.5f) * 0.04f;
                for (int j = 0; j < 40; ++j) {
                    const bool ok = app.orbit(dy, dp);
                    check(1);
                    if (!ok) { ++blockedTurn; break; }
                }
            } else {   // out
                for (int j = 0; j < 8; ++j) {
                    const bool ok = app.zoomAbout(app.focus, -0.35f);
                    check(2);
                    if (!ok) { ++blockedOut; break; }
                }
            }
        }
        // all the way out, a frame's share of the wheel at a time (0.05 of the 0.30 a notch is); the camera's largest
        // move in one of them, as a fraction of the first view's distance, is the jump the eye would see
        bool reached = false;
        int j = 0;
        for (; j < 4000 && !reached; ++j) {
            const D3 was = app.camPos;
            app.zoomAbout(app.focus, -0.05f);
            check(2);
            jump = std::max(jump, length(app.camPos - was) / homeDist);
            reached = app.level == 1.0f && length(app.camPos - app.homePos) < homeDist * 1e-9;
        }
        longest = std::max(longest, j);
        if (reached) {
            ++homes;
        } else {
            ++lost;
            std::printf("  donemedi: yuruyus %d, seviye %.3f, ilk gorunume %.3f, iz %d nokta\n", walk, app.level,
                        length(app.camPos - app.homePos) / homeDist, int(app.trail.size()));
        }
    }
    std::printf("kamera: en dar aciklik yakinlasirken %.2e, geri donerken %.2e (odak uzakliginin kati)\n", worstIn, worstOut);
    std::printf("kamera: %d adim, iceride %d, en dar aciklik %.4f (uzakligin kati), duvara carpan geri cekilme %d, donus %d, "
                "en uzak %.4f (ilk gorunumun uzakliginin kati, tasma %.2f), ilk gorunume donen yuruyus %d/%d, "
                "en buyuk sicrama %.4f (0.05'lik payda, ilk gorunumun uzakliginin kati), en uzun donus %.1f tik\n",
                steps, inside, worst, blockedOut, blockedTurn, farthest, app.leashRadius() / homeDist, homes, homes + lost, jump,
                longest * 0.05 / std::log(1.35));
    return inside == 0 && farthest <= app.leashRadius() / homeDist * 1.0001 && lost == 0 && jump < 0.1 ? 0 : 1;
}

int testPerturbation(HeykelApp& app) {
    app.setCandidate(app.sculpture());
    if (!app.deep()) { std::printf("aday %d derin degil\n", app.cand); return 1; }
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
            const float before = app.level;
            app.zoomAbout(q, std::log(2.0f));
            if (app.level <= before * 1.0001f) { ok = false; break; }   // a plane (zoomCapAt): not an anchor for this
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
        app.level = float(zoom);
        for (const D3& W : anchors) {
            // the camera at this zoom's distance from the anchor: the iterations follow it (hk::itersAt), the same count
            // on every side
            app.camPos = W + d3(0.0, 0.0, dist);
            app.focus = W;
            hk::World wf = app.world;
            wf.s.iters = app.itersNow();
            app.setLod(wf);
            wf.camRel = f3(app.camPos);   // wf takes world positions (its anchor is the origin)
            hk::World wa = wf;
            app.setAnchor(wa, W);
            app.setLod(wa);
            for (int j = 0; j < 64; ++j) {
                // points within ~200 pixels of the anchor
                const D3 o = d3(rnd() - 0.5, rnd() - 0.5, rnd() - 0.5) * (400.0 * pixel);
                const double dD = app.deHost(W + o);
                if (std::fabs(dD) > 50.0 * pixel) continue;   // near the surface only: that is where it decides
                const double dP = hk::deScene(f3(o), wa);
                const double dF = hk::deScene(f3(W + o), wf);
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
    int dives = 0;
    D3 target;
    bool hasTarget = false, moving = false;
    std::string stability;
    int frames = 24;
    unsigned int seed = 1u;
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
        else if (a == "--dalis") dives = std::stoi(next());
        else if (a == "--seed") seed = unsigned(std::stoul(next()));
        else if (a == "--hareket") moving = true;
        else if (a == "--kararlilik") stability = next();
        else if (a == "--kare") frames = std::stoi(next());
        else if (a == "--hedef") {   // a world point to zoom straight at (the tour's dives)
            std::stringstream ss(next());
            char c;
            ss >> target.x >> c >> target.y >> c >> target.z;
            hasTarget = true;
        }
    }
    app.loadConfig();
    app.width = w;
    app.height = h;
    app.buildCandidates();
    if (test == "pert") return testPerturbation(app);
    if (test == "kamera") return testCamera(app);
    if (!stability.empty()) {
        app.init();
        setPNGCompression(1);
        const int cand = which == "all" ? app.sculpture() : std::stoi(which);
        return stabilityFrames(app, cand, stability, target, hasTarget, px.x >= 0.0f ? px : make_float2(0.5f, 0.45f),
                               zooms.size() > 0 ? zooms.front() : 50.0f, zooms.size() > 1 ? zooms.back() : 70.0f, frames, out);
    }
    app.init();
    setPNGCompression(1);
    std::filesystem::create_directories(std::filesystem::path(app.root) / out);
    // the jobs: a candidate and the pixel the zoom closes in on; --dalis N: N random pixels on the Cathedral (the dives
    // for the tour are chosen from these)
    struct Job {
        int k;
        float2 dv;
        std::string label;
    };
    std::vector<Job> jobs;
    if (dives > 0) {
        app.setCandidate(app.sculpture());
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float((seed >> 8) * (1.0 / 16777216.0)); };
        for (int t = 0; t < dives * 20 && int(jobs.size()) < dives; ++t) {
            const float2 dv = make_float2(0.2f + 0.6f * rnd(), 0.12f + 0.66f * rnd());
            app.home();
            D3 q;
            if (!app.pick(dv.x * w, dv.y * h, q) || q.y < 0.003) continue;   // on the sculpture, above the plinth
            char label[64];
            std::snprintf(label, sizeof(label), "d%02d", int(jobs.size()));
            jobs.push_back({app.cand, dv, label});
            std::printf("dalis %s: piksel %.4f, %.4f\n", label, dv.x, dv.y);
        }
    } else {
        std::vector<int> list;
        if (which == "all") for (int k = 0; k < int(app.cands.size()); ++k) list.push_back(k);
        else for (float f : floats(which)) list.push_back(int(f));
        for (int k : list) jobs.push_back({k, px.x >= 0.0f ? px : app.cands[k].dive, std::to_string(k) + "_" + app.cands[k].id});
    }
    for (const Job& job : jobs) {
        const int k = job.k;
        app.setCandidate(k);
        const float2 dv = job.dv;
        for (float z : zooms) {
            app.home();
            app.defaultLight();
            app.aimLight(app.homeFocus);
            if (z > 1.0f && hasTarget) {   // straight at a world point, as the tour does
                if (moving) {   // the picture in the middle of the zoom: small steps, the lamp kept clear, not settled
                    while (app.level < z * 0.999f) {
                        app.zoomAbout(target, std::min(0.05f, std::log(z / app.level)));
                        app.keepLampClear();
                    }
                } else {
                    app.zoomAbout(target, std::log(z));
                    app.settleLight();
                }
            } else if (z > 1.0f) {   // as a hand on the wheel would: twice closer a step, picking again under the same pixel
                D3 q;
                float3 n;
                bool ok = true;
                while (app.level < z * 0.999f) {
                    if (!app.pick(dv.x * w, dv.y * h, q, &n)) { ok = false; break; }
                    const float before = app.level;
                    app.zoomAbout(q, std::log(std::min(2.0f, z / app.level)));
                    if (app.level <= before * 1.0001f) { ok = false; break; }   // a plane: shallow (zoomCapAt)
                }
                if (!ok) {
                    std::printf("  x%.0f: secilen noktada yuzey yok (x%.0f'de)\n", z, app.level);
                    continue;
                }
                // the point zoomed at, for the tour (resolution-free: a world point, not a pixel)
                std::printf("  x%g odak %.17g, %.17g, %.17g\n", z, app.focus.x, app.focus.y, app.focus.z);
                app.settleLight();
            }
            const double t0 = nowS();
            if (spp <= 0) {   // --spp 0: the window's frame (renderFrame), its time
                app.renderFrame();
            } else {
                app.resetSamples();
                int left = spp;
                while (left > 0) {
                    const int b = std::min(left, 8);
                    app.addSamples(b);
                    left -= b;
                }
            }
            app.resolve();
            cudaDeviceSynchronize();
            const double t1 = nowS();
            char name[256];
            std::snprintf(name, sizeof(name), "%s/%s_x%g", out.c_str(), job.label.c_str(), z);
            app.save(app.root + "/" + name);
            const cudaError_t e = cudaGetLastError();
            if (spp <= 0)
                std::printf("  x%-6g kare %.1f ms%s -> %s.png\n", z, app.frameMs, e != cudaSuccess ? cudaGetErrorString(e) : "", name);
            else
                std::printf("  x%-6g %.1f ms/ornek, %d ornek %.1f sn%s -> %s.png\n", z, app.sampleMs, spp, t1 - t0,
                            e != cudaSuccess ? cudaGetErrorString(e) : "", name);
        }
    }
    app.release();
    return 0;
}
