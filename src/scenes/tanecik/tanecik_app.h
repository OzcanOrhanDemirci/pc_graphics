// Scene 4 "Tanecik": the state of a run (settings, the mannequin and its particles, the renderer, the camera).
// Shared by the live scene in the application (tanecik_live.cpp) and the command-line tools (tanecik_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/tanecik/body.h"
#include "scenes/tanecik/body_graph.h"
#include "scenes/tanecik/tanecik_renderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// a camera that circles a point (the figure): right drag turns it, the wheel brings it closer
struct OrbitCamera {
    float3 target = make_float3(0, 0.95f, 0);
    float yaw = 0.0f, pitch = 0.05f, dist = 3.6f;   // yaw 0: in front of him (on +z), looking back along -z
    float fovDeg = 30.0f;
    float3 position() const {
        return target + make_float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * dist;
    }
    void apply(TCamera& cam, unsigned int width, unsigned int height) const {
        cam.pos = position();
        cam.fwd = normalize(target - cam.pos);
        cam.right = normalize(cross(cam.fwd, make_float3(0, 1, 0)));
        cam.up = cross(cam.right, cam.fwd);
        cam.tanHalfY = std::tan(0.5f * fovDeg * 3.14159265f / 180.0f);
        cam.tanHalfX = cam.tanHalfY * float(width) / float(height);
    }
    // the mouse dragged right turns the view right (the camera goes round to his left), down looks down
    void turn(float dx, float dy, float sensitivity) {
        yaw -= dx * sensitivity;
        pitch = std::max(-0.15f, std::min(1.3f, pitch + dy * sensitivity));
    }
};

struct TanecikApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    TanecikRenderer renderer;
    unsigned int width = 1920, height = 1080;
    Mannequin man;
    BodyParticles body;
    TriMesh studio, skin;
    std::vector<TriMesh> boneMeshes;   // one per bone, at rest
    BodyGraph graph;
    std::vector<SkinBind> skinBind;
    float3* dPos = nullptr;      // every particle's position on the device (what the renderer draws and the skin rides on)
    Xform pose[B_COUNT];         // the bones' placements now
    OrbitCamera orbit;
    int particleView = 0;        // 0 every particle, 1 a sparse share, 2 none (display only)

    void loadConfig() {
        cfg = Config();
        cfg.load(root + "/scenes/tanecik.cfg");
        for (const auto& o : overrides) {
            const size_t eq = o.find('=');
            if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
        }
    }
    // the mannequin, its particles and meshes (on the CPU; a second or two)
    void build() {
        body = fillBody(man, cfg.f("beden.aralik", 0.007f), cfg.f("beden.titresim", 0.12f));
        skin = skinMesh(man, cfg.f("beden.deri_hucre", 0.004f));
        boneMeshes.clear();
        for (int b = 0; b < B_COUNT; ++b) boneMeshes.push_back(skeletonMesh(b));
        const float s = body.spacing;
        graph = buildBodyGraph(body, cfg.f("beden.bag_yaricap", 1.5f) * s, cfg.f("beden.kemik_en_az", 0.75f) * s);
        skinBind = bindSkin(skin, body, graph);
        for (Xform& x : pose) x = Xform();
        studio = studioMesh(cfg.f("studyo.duvar_z", -4.0f), cfg.f("studyo.kavis", 1.6f), cfg.f("studyo.yukseklik", 7.0f), cfg.f("studyo.genislik", 14.0f),
                             cfg.f("studyo.on_z", 14.0f));
    }
    void upload() {
        renderer.uploadMeshes(studio, skin, boneMeshes);
        renderer.setSkinBinding(skinBind, graph.bone);
        release();
        cudaMalloc(reinterpret_cast<void**>(&dPos), body.pos.size() * sizeof(float3));
        cudaMemcpy(dPos, body.pos.data(), body.pos.size() * sizeof(float3), cudaMemcpyHostToDevice);
        renderer.setBoneXforms(pose);
        uploadParticles();
    }
    void release() {
        if (dPos) cudaFree(dPos);
        dPos = nullptr;
    }
    // the bones turned about their joints (forward kinematics, no physics): particles carried by their bones, the
    // skin riding on them. A test of the drawing's moving parts before the solver drives them.
    void applyPose(const float3 turn[B_COUNT]) {
        forwardKinematics(graph, turn, Xform(), pose);
        std::vector<float3> p(body.pos.size());
        for (size_t i = 0; i < p.size(); ++i) p[i] = pose[graph.bone[i]].apply(body.pos[i]);
        cudaMemcpy(dPos, p.data(), p.size() * sizeof(float3), cudaMemcpyHostToDevice);
        renderer.setBoneXforms(pose);
        renderer.deformSkin(dPos);
        renderer.updateParticles(dPos, nullptr);
    }
    // the particles as drawn: all of them, a sparse share (every n-th by a hash, so it does not form a pattern), or none
    void uploadParticles() {
        std::vector<uint32_t> idx;
        const float keep = particleView == 0 ? 1.0f : particleView == 1 ? cfg.f("gorunum.seyrek_pay", 0.2f) : 0.0f;
        for (size_t i = 0; i < body.pos.size(); ++i) {
            unsigned h = unsigned(i) * 2654435761u;
            h ^= h >> 15;
            h *= 0x2c1b3c6dU;
            h ^= h >> 12;
            if ((h & 0xffffff) / 16777216.0f >= keep) continue;
            idx.push_back(uint32_t(i));
        }
        const float scale = cfg.f(particleView == 1 ? "gorunum.seyrek_yaricap" : "gorunum.tanecik_yaricap", 0.3f);
        renderer.setParticleSubset(idx, scale * body.spacing);
        renderer.updateParticles(dPos, nullptr);
    }
    void applyLook(bool glass) {
        TanecikParams& p = renderer.params();
        p.glass = glass ? 1 : 0;
        p.skinAlbedo = cfg.v3(glass ? "cam.buzlu_renk" : "deri.renk", glass ? make_float3(0.62f, 0.42f, 0.33f) : make_float3(0.58f, 0.38f, 0.29f));
        p.shellOptical = cfg.v3("cam.optik_derinlik", make_float3(0.22f, 0.48f, 0.65f));
        p.skinFrost = cfg.f("cam.buzlu", 0.2f);
        p.skinF0 = cfg.f(glass ? "cam.f0" : "deri.f0", glass ? 0.04f : 0.028f);
        p.skinGloss = cfg.f("deri.parlaklik", 30.0f);
        p.boneAlbedo = cfg.v3("kemik.renk", make_float3(0.78f, 0.72f, 0.60f));
        p.tissueAlbedo = cfg.v3("tanecik.renk", make_float3(0.55f, 0.17f, 0.15f));
        p.liquidAlbedo = cfg.v3("sivi.renk", make_float3(0.35f, 0.02f, 0.02f));
        p.studioAlbedo = cfg.v3("studyo.renk", make_float3(0.78f, 0.77f, 0.75f));
        p.ambient = cfg.v3("isik.ortam", make_float3(0.24f, 0.24f, 0.25f));
        p.maxDiffuse = cfg.i("isik.sekme", 2);
        p.exposure = std::pow(2.0f, cfg.f("goruntu.ev", 0.0f));
        // the soft boxes: a key to the front left, a top light, two strips behind him for the outline
        auto box = [&](const std::string& k, float3 c, float3 aim, float w, float h, float3 Le) {
            TRectLight l;
            l.c = cfg.v3(k + ".konum", c);
            const float3 n = normalize(cfg.v3(k + ".hedef", aim) - l.c);   // it faces its target
            float3 u = cross(make_float3(0, 1, 0), n);
            if (dot(u, u) < 1e-6f) u = make_float3(1, 0, 0);
            u = normalize(u);
            const float3 v = cross(n, u);   // cross(u, v) = n
            l.u = u * (0.5f * cfg.f(k + ".en", w));
            l.v = v * (0.5f * cfg.f(k + ".boy", h));
            l.Le = cfg.v3(k + ".parlaklik", Le);
            return l;
        };
        p.lights[0] = box("isik.ana", make_float3(-2.5f, 2.4f, 1.3f), make_float3(0, 1.1f, 0), 1.4f, 1.6f, make_float3(11.0f, 10.5f, 9.8f));
        p.lights[1] = box("isik.tepe", make_float3(0.3f, 3.6f, 0.6f), make_float3(0, 0.0f, 0.2f), 2.0f, 1.2f, make_float3(3.0f, 3.0f, 3.1f));
        p.lights[2] = box("isik.arka_sol", make_float3(-2.4f, 1.3f, -0.9f), make_float3(0, 1.0f, 0.0f), 0.35f, 2.2f, make_float3(12.0f, 12.0f, 12.5f));
        p.lights[3] = box("isik.arka_sag", make_float3(2.4f, 1.3f, -0.9f), make_float3(0, 1.0f, 0.0f), 0.35f, 2.2f, make_float3(12.0f, 12.0f, 12.5f));
        p.lightCount = std::min(T_MAX_LIGHTS, cfg.i("isik.sayi", 4));
    }
    void setCamera() {
        orbit.target = cfg.v3("kamera.hedef", make_float3(0, 0.95f, 0));
        orbit.yaw = cfg.f("kamera.yaw_deg", 0.0f) * 3.14159265f / 180.0f;
        orbit.pitch = cfg.f("kamera.pitch_deg", 3.0f) * 3.14159265f / 180.0f;
        orbit.dist = cfg.f("kamera.uzaklik", 4.2f);
        orbit.fovDeg = cfg.f("kamera.fov", 30.0f);
        orbit.apply(renderer.params().cam, width, height);
    }
};

int runTanecikCli(int argc, char** argv);
