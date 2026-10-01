// Scene 2 "Fener": the window, the keys and the headless renders.
//
//   pc_graphics.exe --scene fener                         borderless full screen, live
//   pc_graphics.exe --scene fener --headless --frames N --out captures/x    a still: N jittered frames averaged at a
//                                                          frozen time (offline quality), PNG
//   pc_graphics.exe --scene fener --headless --live N --out captures/x      N live frames at 30 fps: timings + frames
//   pc_graphics.exe --scene fener --headless --journey N --every k --out captures/x   the walk A -> B, N frames at 30 fps
//   pc_graphics.exe --scene fener --map captures/harita.png    top-down map (islands, forest, A, B, the journey)
//   options: --view A|B, --time t (seconds: beam angle, waves), --width W, --height H, --set key=value, --windowed
//
// Keys: Tab scene -> player (camera island) -> player (lighthouse island) -> scene. Player: WASD, mouse, Shift run,
//       Space jump, F12 screenshot. Scene: Space walk A <-> B (again on the way: turn back), B jump to the other view, H hud, S screenshot, F5 reload scenes/fener.cfg + device programs, P pause time,
//       Left/Right time -/+ 1 s, Up/Down exposure, 1-5 views (final, surface, volume, depth, normals), Esc quit.
#include "fener/fener_renderer.h"
#include "gpu_stats.h"
#include "image_io.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <windows.h>
#include <GL/gl.h>
#include "stb_easy_font.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static double fnow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
static float halton(unsigned int i, unsigned int b) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= float(b); r += f * float(i % b); i /= b; }
    return r;
}

// The walk from A to B (Space). Distance d along the path with a speed that ramps up, cruises and brakes in time
// to stop at the end; pressing Space again on the way brakes and walks back.
struct Journey {
    std::vector<float> arc;      // cumulative length at each path point
    float d = 0.0f, v = 0.0f;    // metres along the path, metres per second (signed)
    int target = 0;              // 0: A, 1: B
    float length() const { return arc.empty() ? 0.0f : arc.back(); }
    bool moving() const { return std::fabs(v) > 1e-3f || std::fabs(d - target * length()) > 0.01f; }
    void build(const std::vector<float3>& p) {
        arc.assign(p.size(), 0.0f);
        for (size_t k = 1; k < p.size(); ++k) arc[k] = arc[k - 1] + std::sqrt(sqr(p[k].x - p[k - 1].x) + sqr(p[k].z - p[k - 1].z));
        d = std::min(d, length());
    }
    void step(float dt, float vmax, float acc) {
        const float goal = target * length();
        const float rem = goal - d;
        const float dir = rem >= 0.0f ? 1.0f : -1.0f;
        // braking distance at the current speed decides whether to speed up or slow down
        const float vWanted = dir * std::min(vmax, std::sqrt(2.0f * acc * std::fabs(rem)));
        if (v < vWanted) v = std::min(v + acc * dt, vWanted);
        else v = std::max(v - acc * dt, vWanted);
        d += v * dt;
        if ((dir > 0 && d >= goal) || (dir < 0 && d <= goal)) {
            if (std::fabs(v) < 0.3f) { d = goal; v = 0.0f; }
        }
        d = std::min(std::max(d, 0.0f), length());
    }
};

// Player mode (Tab): walk the islands. A kinematic body: gravity, jumping, the ground as a height function, tree trunks,
// the tower and the cabin as obstacles, swimming where the sea is deep. Nothing in the world was built for it.
struct Player {
    float3 feet = {0, 0, 0}, vel = {0, 0, 0};
    float yaw = 0.0f, pitch = 0.0f;   // yaw 0 looks north (+z), positive turns right
    bool onGround = false, swimming = false;
};
struct Trunks {                          // tree trunks in an 8 m grid for the player's collisions
    std::vector<float3> t;               // x, z, radius
    std::vector<std::vector<int>> cell;
    int nx = 0, nz = 0;
    float x0 = -1000.0f, z0 = -1800.0f, cs = 8.0f;
    void build(const FenerScene& s) {
        t.clear();
        for (const FInstanceCPU& in : s.instances)
            if (s.meshes[in.mesh].name.rfind("spruce", 0) == 0) t.push_back(make_float3(in.m[3], in.m[11], std::max(0.18f, in.m[5] * 0.0125f)));
        nx = int(2400.0f / cs);
        nz = int(2400.0f / cs);
        cell.assign(size_t(nx) * nz, {});
        for (int k = 0; k < int(t.size()); ++k) {
            const int cx = int((t[k].x - x0) / cs), cz = int((t[k].y - z0) / cs);
            if (cx >= 0 && cz >= 0 && cx < nx && cz < nz) cell[size_t(cz) * nx + cx].push_back(k);
        }
    }
    // push a circle of radius r at (x, z) out of every trunk it overlaps
    void resolve(float& x, float& z, float r) const {
        const int cx = int((x - x0) / cs), cz = int((z - z0) / cs);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int X = cx + dx, Z = cz + dz;
                if (X < 0 || Z < 0 || X >= nx || Z >= nz) continue;
                for (int k : cell[size_t(Z) * nx + X]) {
                    const float ex = x - t[k].x, ez = z - t[k].y, d = std::sqrt(ex * ex + ez * ez), m = r + t[k].z;
                    if (d < m && d > 1e-4f) { x = t[k].x + ex / d * m; z = t[k].y + ez / d * m; }
                }
            }
    }
};

struct FenerApp {
    FenerRenderer renderer;
    Config cfg;
    FenerScene scene;
    std::string root = PROJECT_ROOT;
    unsigned int width = 2560, height = 1440;
    std::vector<std::string> overrides;
    int view = 0;
    float ev = 0.0f;
    bool still = false;          // progressive accumulation at a frozen time (headless stills)
    std::vector<uchar4> pixels;
    Journey journey;
    Player player;
    Trunks trunks;
    int mode = 0;                        // 0 the scene, 1 player on the camera island, 2 player by the lighthouse

    void loadConfig() {
        cfg = Config();
        cfg.load(root + "/scenes/fener.cfg");
        for (const auto& o : overrides) {
            const size_t eq = o.find('=');
            if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
        }
    }
    void applyConfig() {
        FenerParams& p = renderer.params();
        const float D2R = 3.14159265f / 180.0f;
        p.moonDir = normalize(cfg.v3("moon.dir", make_float3(-0.15f, 0.30f, 1.0f)));
        p.moonCosMax = std::cos(cfg.f("moon.radius_deg", 0.55f) * D2R);
        p.moonE = cfg.v3("moon.E", make_float3(0.040f, 0.046f, 0.056f));
        p.moonDisc = cfg.v3("moon.disc", make_float3(0.24f, 0.25f, 0.26f));
        p.lampI = cfg.f("lamp.I", 9.0e5f);
        p.beamW = cfg.f("lamp.beam_w_deg", 3.2f) * D2R;
        p.beamWv = cfg.f("lamp.beam_wv_deg", 3.5f) * D2R;
        p.beamTilt = cfg.f("lamp.beam_tilt_deg", -1.5f) * D2R;
        p.spillK = cfg.f("lamp.spill_k", 0.02f);
        p.spillWv = cfg.f("lamp.spill_wv_deg", 12.0f) * D2R;
        p.spillTilt = cfg.f("lamp.spill_tilt_deg", -15.0f) * D2R;
        p.lampLeak = cfg.f("lamp.leak", 900.0f);
        p.lampSurfK = cfg.f("lamp.surface_k", 0.15f);
        p.lampGlassBase = cfg.f("lamp.glass_base", 0.5f);
        p.lampGlassK = cfg.f("lamp.glass_k", 60.0f);
        p.lampColor = cfg.v3("lamp.color", make_float3(1.0f, 0.97f, 0.92f));
        p.winRadiance = cfg.v3("window.radiance", make_float3(0.9f, 0.36f, 0.10f));
        p.fogRho0 = cfg.f("fog.rho0", 1.0f / 520.0f);
        p.fogH = cfg.f("fog.height", 55.0f);
        p.fogHaze = cfg.f("fog.haze", 1.0f / 22000.0f);
        p.fogGround = cfg.f("fog.ground", 1.0f / 160.0f);
        p.fogGroundH = cfg.f("fog.ground_height", 7.0f);
        p.fogAmbient = cfg.v3("fog.ambient", make_float3(0.0025f, 0.0030f, 0.0035f));
        p.fogAmbientHalo = cfg.f("fog.halo", 0.25f);
        p.fogAmbientG = cfg.f("fog.halo_g", 0.75f);
        p.nightSky = cfg.v3("sky.night", make_float3(0.0010f, 0.0014f, 0.0022f));
        p.waterDeep = cfg.v3("water.deep", make_float3(0.0004f, 0.0008f, 0.0010f));
        p.moonGlintK = cfg.f("water.moon_glint", 13.0f);
        p.volLampSamples = cfg.i("volume.lamp_samples", 8);
        p.volMoonSamples = cfg.i("volume.moon_samples", 4);
        p.volBeamScale = cfg.f("volume.beam_scale", 2.6f);
        p.volBeamCap = cfg.f("volume.beam_cap", 0.09f);
        p.volMoonScale = cfg.f("volume.moon_scale", 1.0f);
        p.beamPhaseG = cfg.f("volume.beam_g", 0.15f);
        p.beamPhaseHalo = cfg.f("volume.beam_halo", 0.15f);
        ev = cfg.f("display.ev", 3.39f);
        p.exposure = std::pow(2.0f, ev);
        p.glowWideK = cfg.f("display.glow_wide", 0.05f);
        p.glowNarrowK = cfg.f("display.glow_narrow", 0.45f);
        p.glowThreshold = cfg.f("display.glow_threshold", 1.5f);
        p.vignette = cfg.f("display.vignette", 0.28f);
        p.taaAlpha = still ? 0.0f : cfg.f("display.taa_alpha", 0.10f);
    }
    void rebuildScene() {
        const double t0 = fnow();
        scene = buildFenerScene(cfg);
        renderer.uploadScene(scene);
        std::printf("scene: %zu meshes, %zu instances, %zu triangles drawn (%.1f s)\n", scene.meshes.size(), scene.instances.size(),
                    scene.triangles(), fnow() - t0);
        trunks.build(scene);
        setView(view);
    }
    // where the walk stands at distance d: position on the path (eye above the smoothed ground), looking along the way
    // and towards the lighthouse, blending into view A over the first metres and into view B over the last
    FenerView journeyView(float d) const {
        const auto& P = scene.journey;
        const float L = journey.length();
        auto at = [&](float x) {
            x = std::min(std::max(x, 0.0f), L);
            size_t k = size_t(std::upper_bound(journey.arc.begin(), journey.arc.end(), x) - journey.arc.begin());
            k = std::min(std::max<size_t>(k, 1), P.size() - 1);
            const float a = journey.arc[k - 1], b = journey.arc[k];
            const float t = b > a ? (x - a) / (b - a) : 0.0f;
            return P[k - 1] + (P[k] - P[k - 1]) * t;
        };
        float3 p = at(d);
        float gy = 0.0f;
        for (int k = -6; k <= 6; ++k) gy += at(d + k * 3.0f).y;   // the eye floats over the ground's bumps
        gy /= 13.0f;
        const float wA = 1.0f - smoothstepf(0.0f, cfg.f("journey.blend", 70.0f), d);
        const float wB = smoothstepf(L - cfg.f("journey.blend", 70.0f), L, d);
        const float eye = cfg.f("journey.eye", 2.1f);
        const FenerView& A = scene.views[0];
        const FenerView& B = scene.views[1];
        p.y = gy + eye;
        // travel direction (look-ahead), turned partly towards the lighthouse, slightly up into the canopy
        const float sgn = journey.target == 1 ? 1.0f : -1.0f;
        float3 tdir = at(d + sgn * 25.0f) - at(d - sgn * 5.0f);
        tdir.y = 0.0f;
        tdir = normalize(tdir);
        const float3 toL = normalize(make_float3(-p.x, 0.0f, -p.z));
        float3 f = normalize(tdir * (1.0f - cfg.f("journey.look_lighthouse", 0.35f)) + toL * cfg.f("journey.look_lighthouse", 0.35f));
        f.y = std::tan(cfg.f("journey.pitch", 4.0f) * 3.14159265f / 180.0f);
        FenerView v{"yol", p, p + f * 100.0f, cfg.f("journey.fov", 46.0f)};
        auto blend = [&](const FenerView& e, float w) {
            if (w <= 0.0f) return;
            const float3 fe = normalize(e.target - e.pos), fv = normalize(v.target - v.pos);
            v.pos = v.pos + (e.pos - v.pos) * w;
            v.target = v.pos + normalize(fv + (fe - fv) * w) * 100.0f;
            v.vfovDeg = v.vfovDeg + (e.vfovDeg - v.vfovDeg) * w;
        };
        blend(A, wA * wA * (3.0f - 2.0f * wA));
        blend(B, wB * wB * (3.0f - 2.0f * wB));
        return v;
    }
    // The eye adapts: under the canopy the exposure opens, partly (the forest must stay darker than the open views).
    // display.adapt_ref is the metered log2 luminance of the open views, so A and B keep display.ev.
    float evAdapt = 0.0f;
    void adaptExposure(float dt, bool instant) {
        const float k = cfg.f("display.adapt", 0.6f);
        float target = k * (cfg.f("display.adapt_ref", -8.0f) - renderer.meteredLog2());
        target = std::min(std::max(target, cfg.f("display.adapt_min", -1.0f)), cfg.f("display.adapt_max", 3.0f));
        evAdapt = instant ? target : evAdapt + (target - evAdapt) * (1.0f - std::exp(-dt / cfg.f("display.adapt_seconds", 1.5f)));
        renderer.params().exposure = std::pow(2.0f, ev + evAdapt);
    }
    // ---- the player
    void placePlayer(int where) {
        if (where == 1) {   // where view A stands, looking the way A looks
            const FenerView& A = scene.views[0];
            player.feet = make_float3(A.pos.x, fenerTerrain(A.pos.x, A.pos.z), A.pos.z);
            const float3 f = normalize(A.target - A.pos);
            player.yaw = std::atan2(f.x, f.z);
            player.pitch = std::asin(f.y);
        } else {            // the lighthouse island, in the clearing south of the tower, looking up at it
            player.feet = make_float3(3.0f, 0.0f, -16.0f);
            player.feet.y = fenerTerrain(player.feet.x, player.feet.z);
            player.yaw = std::atan2(-3.0f, 16.0f);
            player.pitch = 0.72f;   // up the tower to the lantern
        }
        player.vel = make_float3(0, 0, 0);
        renderer.resetHistory();
    }
    // forward/right in -1..1 (W/S, D/A), run (Shift), jump (Space pressed this frame)
    void stepPlayer(float inFwd, float inRight, bool run, bool jump, float dt) {
        Player& P = player;
        const float yawS = std::sin(P.yaw), yawC = std::cos(P.yaw);
        const float3 fwd = make_float3(yawS, 0.0f, yawC), right = make_float3(yawC, 0.0f, -yawS);
        float3 wish = fwd * inFwd + right * inRight;
        const float wl = length(wish);
        if (wl > 0.0f) wish = wish / wl;
        const float speed = P.swimming ? cfg.f("player.swim", 2.0f) : (run ? cfg.f("player.run", 8.0f) : cfg.f("player.walk", 4.2f));
        const float acc = P.onGround || P.swimming ? 30.0f : 6.0f;   // little control in the air
        for (int c = 0; c < 2; ++c) {
            float& v = c == 0 ? P.vel.x : P.vel.z;
            const float target = (c == 0 ? wish.x : wish.z) * speed;
            const float dv = target - v, mx = acc * dt;
            v += std::max(-mx, std::min(mx, dv));
        }
        // horizontal move, then the obstacles: trunks, the tower, the cabin
        float x = P.feet.x + P.vel.x * dt, z = P.feet.z + P.vel.z * dt;
        const float r = 0.35f;
        trunks.resolve(x, z, r);
        {   // the tower: a circle around the origin (with its foundation)
            const float d = std::sqrt(x * x + z * z), m = 4.7f + r;
            if (d < m && d > 1e-4f) { x = x / d * m; z = z / d * m; }
        }
        {   // the cabin: an axis-aligned box 8 x 5.2 m
            const float hx = 4.1f + r, hz = 2.7f + r, ex = x - scene.cabin.x, ez = z - scene.cabin.z;
            if (std::fabs(ex) < hx && std::fabs(ez) < hz && P.feet.y < scene.cabin.y + 6.0f) {
                if (hx - std::fabs(ex) < hz - std::fabs(ez)) x = scene.cabin.x + (ex > 0 ? hx : -hx);
                else z = scene.cabin.z + (ez > 0 ? hz : -hz);
            }
        }
        P.feet.x = x;
        P.feet.z = z;
        // vertical: the ground, gravity, jumping; deep water floats you at the surface
        const float ground = fenerTerrain(x, z);
        const float eye = cfg.f("player.eye", 1.7f);
        P.swimming = ground < -(eye - 0.25f);
        if (P.swimming) {
            P.feet.y = -(eye - 0.25f) + 0.06f * std::sin(float(fnow()) * 1.3f);   // the eye just above the waves
            P.vel.y = 0.0f;
            P.onGround = false;
            return;
        }
        if (jump && P.onGround) { P.vel.y = cfg.f("player.jump", 4.8f); P.onGround = false; }
        P.vel.y -= cfg.f("player.gravity", 9.81f) * dt;
        P.feet.y += P.vel.y * dt;
        const bool wasOnGround = P.onGround;
        if (P.feet.y <= ground || (wasOnGround && P.vel.y <= 0.0f && P.feet.y - ground < 0.45f)) {   // land, or follow a slope down
            P.feet.y = ground;
            P.vel.y = 0.0f;
            P.onGround = true;
        } else {
            P.onGround = false;
        }
    }
    void applyPlayerCamera() {
        const Player& P = player;
        const float3 pos = P.feet + make_float3(0.0f, cfg.f("player.eye", 1.7f), 0.0f);
        const float3 f = make_float3(std::sin(P.yaw) * std::cos(P.pitch), std::sin(P.pitch), std::cos(P.yaw) * std::cos(P.pitch));
        fenerCamera(FenerView{"oyuncu", pos, pos + f * 10.0f, cfg.f("player.fov", 58.0f)}, width, height, renderer.params().cam);
    }

    void applyJourney() {
        if (scene.journey.size() < 2) return;
        fenerCamera(journeyView(journey.d), width, height, renderer.params().cam);
    }
    void setView(int v) {
        view = (v % int(scene.views.size()) + int(scene.views.size())) % int(scene.views.size());
        fenerCamera(scene.views[view], width, height, renderer.params().cam);
        journey.build(scene.journey);
        journey.target = view;
        journey.d = view == 1 ? journey.length() : 0.0f;
        journey.v = 0.0f;
        renderer.resetHistory();
    }
    bool compile() {
        std::string log;
        const double t0 = fnow();
        const bool ok = renderer.compileDevice(log);
        std::printf("%s fener programs (%.1f s)\n", ok ? "compiled" : "COMPILE FAILED:", fnow() - t0);
        if (!log.empty() && (!ok || log.size() > 2)) std::printf("%s\n", log.c_str());
        return ok;
    }
    void loadBlueNoise() {
        int w = 0, h = 0;
        std::vector<unsigned char> g;
        if (readPNGGray(root + "/assets/bluenoise128.png", w, h, g) && w == 128 && h == 128) {
            std::vector<float> f(g.size());
            for (size_t i = 0; i < g.size(); ++i) f[i] = (g[i] + 0.5f) / 256.0f;
            float* d = nullptr;
            cudaMalloc(&d, f.size() * sizeof(float));
            cudaMemcpy(d, f.data(), f.size() * sizeof(float), cudaMemcpyHostToDevice);
            renderer.params().blueNoise = d;
        } else {
            std::printf("blue noise texture missing\n");
        }
    }
    // the lamp turns; the sea moves; everything is a function of t
    void setTime(float t) {
        FenerParams& p = renderer.params();
        p.time = t;
        const float period = cfg.f("lamp.period", 14.0f);
        p.lampPhi = cfg.f("lamp.phi0_deg", 38.0f) * 3.14159265f / 180.0f + 2.0f * 3.14159265f * t / period * cfg.f("lamp.direction", 1.0f);
    }
    void frame(unsigned int k) {
        FCamera& c = renderer.params().cam;
        const unsigned int n = still ? 64u : 16u;
        c.jitter = make_float2(halton(k % n + 1, 2) - 0.5f, halton(k % n + 1, 3) - 0.5f);
        renderer.renderFrame();
    }
    void save(const std::string& prefix) {
        std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
        renderer.download(pixels);
        writePNG(prefix + ".png", int(width), int(height), reinterpret_cast<const unsigned char*>(pixels.data()));
        std::printf("saved %s.png\n", prefix.c_str());
    }
};

static std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

static void printTimes(const FenerTimes& t) {
    std::printf("passes: surface %.2f  volume %.2f  resolve %.2f  compose %.2f  taa %.2f  glow %.2f  final %.2f  = %.2f ms\n", t.surface, t.volume,
                t.resolve, t.compose, t.taa, t.glow, t.final, t.total());
}

static void drawText(float x, float y, const std::string& text, float scale) {
    static char buffer[200000];
    const int quads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr, buffer, sizeof(buffer));
    glEnableClientState(GL_VERTEX_ARRAY);
    for (int pass = 0; pass < 2; ++pass) {
        glPushMatrix();
        glTranslatef(x + (pass == 0 ? scale : 0), y + (pass == 0 ? scale : 0), 0);
        glScalef(scale, scale, 1);
        if (pass == 0) glColor4f(0, 0, 0, 0.8f);
        else glColor4f(0.85f, 0.9f, 1.0f, 1.0f);
        glVertexPointer(2, GL_FLOAT, 16, buffer);
        glDrawArrays(GL_QUADS, 0, quads * 4);
        glPopMatrix();
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

static bool g_fkeys[GLFW_KEY_LAST + 1] = {};
static void fenerKey(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key <= GLFW_KEY_LAST && action == GLFW_PRESS) g_fkeys[key] = true;
}
static bool fpressed(int key) {
    const bool p = g_fkeys[key];
    g_fkeys[key] = false;
    return p;
}

// --autotest: keys pressed on a timeline through the same path real key presses take (window check)
struct FenerKeyAt { double t; int key; };
static const FenerKeyAt kFenerAutotest[] = {{3.0, GLFW_KEY_SPACE}, {12.0, GLFW_KEY_S}, {20.0, GLFW_KEY_SPACE}, {24.0, GLFW_KEY_S}, {26.0, GLFW_KEY_ESCAPE}};
// --autotest-player: Tab into the player, jump, Tab to the lighthouse island, screenshot, Tab back to the scene, quit
static const FenerKeyAt kFenerAutotestPlayer[] = {{3.0, GLFW_KEY_TAB}, {5.0, GLFW_KEY_SPACE}, {7.0, GLFW_KEY_TAB}, {9.0, GLFW_KEY_F12},
                                                  {10.0, GLFW_KEY_TAB}, {12.0, GLFW_KEY_S}, {13.0, GLFW_KEY_ESCAPE}};

static int runFenerWindow(FenerApp& app, bool windowed, int autotest) {
    const FenerKeyAt* timeline = autotest == 2 ? kFenerAutotestPlayer : kFenerAutotest;
    const size_t timelineLen = autotest == 2 ? sizeof(kFenerAutotestPlayer) / sizeof(FenerKeyAt) : sizeof(kFenerAutotest) / sizeof(FenerKeyAt);
    if (!glfwInit()) { std::printf("glfwInit failed\n"); return 1; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    GLFWwindow* win = nullptr;
    if (windowed) {
        win = glfwCreateWindow(1280, 720, "pc_graphics - Fener", nullptr, nullptr);
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);   // losing focus must not minimise it (a minimised window's swap can stall)
        win = glfwCreateWindow(mode->width, mode->height, "pc_graphics - Fener", mon, nullptr);
    }
    if (!win) { std::printf("window creation failed\n"); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);
    glfwSetKeyCallback(win, fenerKey);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, app.width, app.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    uchar4* pinned = nullptr;
    cudaMallocHost(&pinned, size_t(app.width) * app.height * sizeof(uchar4));

    FenerParams& p = app.renderer.params();
    bool hud = true, paused = false;
    double t = app.cfg.f("time", 0.0f), last = fnow(), fpsT = fnow(), lastStats = 0, lastStep = fnow();
    int fpsFrames = 0;
    float fps = 0;
    unsigned int k = 0;
    GpuStats stats;
    std::string message;
    double messageUntil = 0;
    auto say = [&](const std::string& m) { message = m; messageUntil = fnow() + 3.0; std::printf("%s\n", m.c_str()); };
    const double winStart = fnow();
    size_t autoNext = 0;
    double mouseX = 0.0, mouseY = 0.0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (autotest && autoNext < timelineLen && fnow() - winStart > timeline[autoNext].t) {
            std::printf("autotest: key %d at %.1f s (%.1f fps, walk %.0f m, v %.1f m/s, EV %+.2f, mode %d, feet %.1f %.1f %.1f)\n", timeline[autoNext].key,
                        fnow() - winStart, fps, app.journey.d, app.journey.v, app.evAdapt, app.mode, app.player.feet.x, app.player.feet.y, app.player.feet.z);
            std::printf("          window: focused %d, iconified %d\n", glfwGetWindowAttrib(win, GLFW_FOCUSED), glfwGetWindowAttrib(win, GLFW_ICONIFIED));
            std::fflush(stdout);
            g_fkeys[timeline[autoNext].key] = true;
            ++autoNext;
        }
        if (fpressed(GLFW_KEY_ESCAPE)) break;
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED)) {   // minimised: nothing to show, wait instead of rendering
            glfwWaitEventsTimeout(0.05);
            last = lastStep = fnow();
            continue;
        }
        if (fpressed(GLFW_KEY_H)) hud = !hud;
        if (fpressed(GLFW_KEY_P)) { paused = !paused; say(paused ? "zaman durdu" : "zaman akiyor"); }
        if (fpressed(GLFW_KEY_RIGHT)) t += 1.0;
        if (fpressed(GLFW_KEY_LEFT)) t -= 1.0;
        if (fpressed(GLFW_KEY_TAB)) {
            app.mode = (app.mode + 1) % 3;
            if (app.mode == 0) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                app.applyJourney();
                app.renderer.resetHistory();
                say("SAHNE");
            } else {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
                glfwGetCursorPos(win, &mouseX, &mouseY);
                app.placePlayer(app.mode);
                say(app.mode == 1 ? "OYUNCU: WASD yuru, fare bak, Shift kos, Bosluk zipla  (Tab: fener adasi)" : "OYUNCU: fener adasi  (Tab: sahneye don)");
            }
        }
        bool jump = false;
        if (fpressed(GLFW_KEY_SPACE)) {
            if (app.mode == 0) {
                app.journey.target ^= 1;
                say(app.journey.target ? "yola cikis: B" : "yola cikis: A");
            } else {
                jump = true;
            }
        }
        if (fpressed(GLFW_KEY_B) && app.mode == 0) { app.setView(app.view + 1); say(std::string("bakis ") + app.scene.views[app.view].name); }
        for (int v = 0; v < 5; ++v)
            if (fpressed(GLFW_KEY_1 + v)) p.view = v;
        if (fpressed(GLFW_KEY_UP)) { app.ev += 0.25f; p.exposure = std::pow(2.0f, app.ev); }
        if (fpressed(GLFW_KEY_DOWN)) { app.ev -= 0.25f; p.exposure = std::pow(2.0f, app.ev); }
        if (fpressed(GLFW_KEY_F5)) {
            app.loadConfig();
            if (app.compile()) { app.applyConfig(); app.rebuildScene(); say("yeniden derlendi"); }
            else say("DERLEME HATASI: konsola bak");
        }
        if (app.mode == 0 && fpressed(GLFW_KEY_S)) { app.save(app.root + "/captures/fener_" + stamp()); say("ekran goruntusu kaydedildi"); }
        if (app.mode != 0 && fpressed(GLFW_KEY_F12)) { app.save(app.root + "/captures/fener_" + stamp()); say("ekran goruntusu kaydedildi"); }
        g_fkeys[GLFW_KEY_S] = false;   // in player mode S walks backwards; F12 takes the screenshot

        const double now = fnow();
        if (!paused) t += now - last;
        last = now;
        app.setTime(float(t));
        if (app.mode == 0 && app.journey.moving()) {
            app.journey.step(float(std::min(now - lastStep, 0.1)), app.cfg.f("journey.speed", 12.0f), app.cfg.f("journey.accel", 1.0f));
            app.applyJourney();
        }
        if (app.mode != 0) {
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            const float sens = app.cfg.f("player.mouse", 0.0022f);
            app.player.yaw += float(mx - mouseX) * sens;
            app.player.pitch = std::max(-1.45f, std::min(1.45f, app.player.pitch - float(my - mouseY) * sens));
            mouseX = mx;
            mouseY = my;
            const float inF = (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f) - (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f);
            const float inR = (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f) - (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f);
            app.stepPlayer(inF, inR, glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS, jump, float(std::min(now - lastStep, 0.05)));
            app.applyPlayerCamera();
        }
        app.frame(k++);
        app.adaptExposure(float(std::min(now - lastStep, 0.1)), false);
        lastStep = now;
        cudaMemcpy(pinned, p.output, size_t(app.width) * app.height * sizeof(uchar4), cudaMemcpyDeviceToHost);

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, fbw, fbh, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.width, app.height, GL_RGBA, GL_UNSIGNED_BYTE, pinned);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        const float sx = float(fbw) / app.width, sy = float(fbh) / app.height, s = std::min(sx, sy);
        const float w = app.width * s, h = app.height * s, x0 = 0.5f * (fbw - w), y0 = 0.5f * (fbh - h);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x0, y0);
        glTexCoord2f(1, 0); glVertex2f(x0 + w, y0);
        glTexCoord2f(1, 1); glVertex2f(x0 + w, y0 + h);
        glTexCoord2f(0, 1); glVertex2f(x0, y0 + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        ++fpsFrames;
        if (fnow() - fpsT > 0.5) { fps = float(fpsFrames / (fnow() - fpsT)); fpsFrames = 0; fpsT = fnow(); }
        if (fnow() - lastStats > 0.25) { stats = gpuStatsRead(); lastStats = fnow(); }
        if (hud) {
            char b[512];
            const FenerTimes& tm = app.renderer.times();
            std::snprintf(b, sizeof(b),
                          "FENER %s  %.1f fps  yol %.0f / %.0f m  t %.1f s   yuzey %.1f  hacim %.1f  taa %.1f  isilti %.1f ms   EV %+.2f\n"
                          "%.0f W / %.0f W   %u MHz   %u C   gorunum %d   Tab: sahne / oyuncu / fener adasi",
                          app.mode == 0 ? "SAHNE" : (app.player.swimming ? "OYUNCU (yuzuyor)" : "OYUNCU"), fps, app.journey.d, app.journey.length(), t,
                          tm.surface, tm.volume + tm.resolve, tm.compose + tm.taa, tm.glow + tm.final, app.ev, stats.watts, stats.limitWatts,
                          stats.smClockMHz, stats.tempC, p.view);
            drawText(24, float(fbh) - 64.0f, b, 2.0f);
            if (fnow() < messageUntil) drawText(24, float(fbh) - 100.0f, message, 2.0f);
        }
        glfwSwapBuffers(win);
    }
    cudaFreeHost(pinned);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

int runFener(int argc, char** argv) {
    FenerApp app;
    bool headless = false, windowed = false;
    int autotest = 0;
    int frames = 0, live = 0, journeyFrames = 0, journeyEvery = 30;
    float atMeters = -1.0f;
    bool playerTest = false;
    int pview = 0;   // 1 surface only, 2 volume only, 3 depth, 4 normals
    float t0 = -1.0f;
    std::string out, viewName, mapOut;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--headless") headless = true;
        else if (a == "--windowed") windowed = true;
        else if (a == "--autotest") autotest = 1;
        else if (a == "--autotest-player") autotest = 2;
        else if (a == "--frames") frames = std::stoi(next());
        else if (a == "--live") live = std::stoi(next());
        else if (a == "--journey") journeyFrames = std::stoi(next());
        else if (a == "--at") atMeters = std::stof(next());
        else if (a == "--player-test") { playerTest = true; headless = true; }
        else if (a == "--pview") pview = std::stoi(next());
        else if (a == "--every") journeyEvery = std::max(1, std::stoi(next()));
        else if (a == "--out") out = next();
        else if (a == "--view") viewName = next();
        else if (a == "--time") t0 = std::stof(next());
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else if (a == "--map") mapOut = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    app.still = headless && frames > 0;
    app.loadConfig();
    app.renderer.init(app.width, app.height, app.root, unsigned(app.cfg.i("volume.scale", 1)));
    std::printf("GPU: %s\n", app.renderer.deviceName().c_str());
    if (!app.compile()) return 1;
    gpuStatsInit();
    app.loadBlueNoise();
    app.applyConfig();
    if (viewName == "B") app.view = 1;
    app.rebuildScene();
    if (!mapOut.empty()) {
        const bool ok = fenerMap(app.scene, app.cfg, mapOut);
        std::printf("%s %s (journey %zu m)\n", ok ? "map" : "MAP FAILED", mapOut.c_str(), app.scene.journey.size());
        app.renderer.shutdown();
        return ok ? 0 : 1;
    }
    if (t0 < 0.0f) t0 = app.cfg.f("time", 0.0f);
    int rc = 0;
    app.renderer.params().view = pview;
    if (atMeters >= 0.0f) {   // a still from the walk: the camera at this many metres from A
        app.journey.target = 1;
        app.journey.d = std::min(atMeters, app.journey.length());
        app.applyJourney();
    }
    if (headless && frames > 0) {
        app.setTime(t0);
        app.frame(0);   // first launch: OptiX finishes its lazy compilation
        app.renderer.resetHistory();
        const double s0 = fnow();
        for (int k = 0; k < frames; ++k) app.frame(unsigned(k));
        app.adaptExposure(0.0f, true);   // the still is shown as the adapted eye would see it: render once more
        app.renderer.resetHistory();
        for (int k = 0; k < frames; ++k) app.frame(unsigned(k));
        std::printf("still: %d frames in %.2f s, metered log2 %.2f, adaptation %+.2f EV\n", frames, fnow() - s0, app.renderer.meteredLog2(), app.evAdapt);
        printTimes(app.renderer.times());
        app.save(out.empty() ? app.root + "/captures/fener_still_" + stamp() : out);
    } else if (headless && playerTest) {
        // player check without a window: walk north from A into the cove, back, jump, run through the forest, swim;
        // then the lighthouse island. Positions printed, a frame saved at each step.
        const std::string o = out.empty() ? app.root + "/captures/fener_oyuncu_" + stamp() : out;
        struct Leg { const char* name; int mode; float f, r, yaw; bool run, jump; float seconds; };
        const Leg legs[] = {{"A'dan koya, suya", 1, 1, 0, 0.0f, false, false, 6.0f},  {"geri, kiyiya", 1, -1, 0, 0.0f, false, false, 8.0f},
                            {"zipla", 1, 0, 0, 0.0f, false, true, 1.2f},               {"guneye kos, ormana", 1, 1, 0, 3.1416f, true, false, 8.0f},
                            {"dogu, agaclar arasi", 1, 1, 0, 1.5708f, true, false, 8.0f}, {"fener adasi", 2, 0, 0, 0.0f, false, false, 0.5f},
                            {"kuleye yuru", 2, 1, 0, -0.18f, false, false, 6.0f},      {"kulubeye", 2, 1, 0, 1.75f, false, false, 14.0f}};
        int curMode = 0, n = 0;
        for (const Leg& L : legs) {
            if (L.mode != curMode) { app.mode = curMode = L.mode; app.placePlayer(L.mode); }
            if (L.f != 0.0f || L.r != 0.0f) app.player.yaw = L.yaw;
            const int steps = int(L.seconds * 60.0f);
            float maxY = app.player.feet.y;
            for (int k = 0; k < steps; ++k) {
                app.stepPlayer(L.f, L.r, L.run, L.jump && k == 0, 1.0f / 60.0f);
                maxY = std::max(maxY, app.player.feet.y);
            }
            const float g = fenerTerrain(app.player.feet.x, app.player.feet.z);
            std::printf("%-22s feet %8.1f %6.2f %8.1f  ground %6.2f  peak %+.2f  %s%s\n", L.name, app.player.feet.x, app.player.feet.y,
                        app.player.feet.z, g, maxY - g, app.player.onGround ? "yerde" : "havada", app.player.swimming ? " yuzuyor" : "");
            app.applyPlayerCamera();
            app.setTime(t0 + n);
            app.renderer.resetHistory();
            for (int k = 0; k < 24; ++k) app.frame(unsigned(k));
            app.adaptExposure(0.0f, true);
            for (int k = 0; k < 24; ++k) app.frame(unsigned(k));
            char nm[32];
            std::snprintf(nm, sizeof(nm), "_%d", n++);
            app.save(o + nm);
        }
    } else if (headless && journeyFrames > 0) {
        const std::string o = out.empty() ? app.root + "/captures/fener_yol_" + stamp() : out;
        app.journey.target = 1;
        double sum = 0;
        for (int k = 0; k < journeyFrames; ++k) {
            app.setTime(t0 + k / 30.0f);
            app.journey.step(1.0f / 30.0f, app.cfg.f("journey.speed", 12.0f), app.cfg.f("journey.accel", 1.0f));
            app.applyJourney();
            const double s = fnow();
            app.frame(unsigned(k));
            app.adaptExposure(1.0f / 30.0f, k == 0);
            sum += fnow() - s;
            if (k % journeyEvery == 0 || k == journeyFrames - 1) {
                char nm[32];
                std::snprintf(nm, sizeof(nm), "_%05d", k);
                app.save(o + nm);
            }
        }
        std::printf("journey: %d frames, %.0f of %.0f m, %.2f ms/frame\n", journeyFrames, app.journey.d, app.journey.length(), 1000.0 * sum / journeyFrames);
        printTimes(app.renderer.times());
    } else if (headless && live > 0) {
        const std::string o = out.empty() ? app.root + "/captures/fener_live_" + stamp() : out;
        double sum = 0;
        for (int k = 0; k < live; ++k) {
            app.setTime(t0 + k / 30.0f);
            const double s = fnow();
            app.frame(unsigned(k));
            if (k >= 5) sum += fnow() - s;
            if (k == live / 2 || k == live - 1) app.save(o + "_f" + std::to_string(k));
        }
        std::printf("live %ux%u: %.2f ms/frame wall (%.1f fps)\n", app.width, app.height, 1000.0 * sum / std::max(1, live - 5),
                    std::max(1, live - 5) / sum);
        printTimes(app.renderer.times());
    } else {
        rc = runFenerWindow(app, windowed, autotest);
    }
    gpuStatsShutdown();
    app.renderer.shutdown();
    return rc;
}
