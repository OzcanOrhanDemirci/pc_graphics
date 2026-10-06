// Scene 9 "Isı" (draft): the world as distances, the shading, the haze and the beams (see isi_kernels.h).
// Units are metres of the grown world: a person on the balcony is 1.7 m, the graphics card's wall 120 m high.
#include "scenes/isi/isi_kernels.h"
#include "shared/display.h"

namespace {

// ---------------------------------------------------------------------------------------------------------------
// the layout (the balcony looks along -z; y up; the board is the ground, y = 0)
constexpr float kWallZ = -230.0f;              // the graphics card's face
constexpr float kFanY = 64.0f, kFanR = 33.0f;   // its three fans
constexpr float kSky = 360.0f;                 // the case's perforated panel
constexpr float kRamZ0 = 10.0f, kRamDZ = 15.0f; // the memory's towers along z
constexpr float kPumpX = 98.0f, kPumpZ = -125.0f;
constexpr float kPsuX = 520.0f, kPsuY = 170.0f, kPsuZ = -1150.0f, kPsuYaw = 0.3f;
__device__ __forceinline__ float fanX(int i) { return -72.0f + 72.0f * float(i); }
__device__ __forceinline__ float ramX(int row) { return row == 0 ? -72.0f : -98.0f; }

enum Id {
    kGround = 0, kWall, kBlade, kFin, kTower, kPlinth, kPump, kHalo, kLava, kVrm, kSsd, kPsu, kCable, kSteel, kGauge,
    kLamp, kChip, kCeiling, kHose, kHeatPipe, kOrgan, kBeacon
};

// ---------------------------------------------------------------------------------------------------------------
// hashes (no random numbers: the same answer for the same cell, every frame)
__device__ __forceinline__ unsigned int pcg(unsigned int v) {
    const unsigned int s = v * 747796405u + 2891336453u;
    const unsigned int w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
__device__ __forceinline__ float hash1(int a, int b, int c = 0) {
    return float(pcg(unsigned(a) * 73856093u ^ pcg(unsigned(b) * 19349663u ^ unsigned(c) * 83492791u)) >> 8) * (1.0f / 16777216.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// distances
__device__ __forceinline__ float sdBox(float3 p, float3 b) {
    const float qx = fabsf(p.x) - b.x, qy = fabsf(p.y) - b.y, qz = fabsf(p.z) - b.z;
    return sqrtf(sqr(fmaxf(qx, 0.0f)) + sqr(fmaxf(qy, 0.0f)) + sqr(fmaxf(qz, 0.0f))) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
}
__device__ __forceinline__ float sdCylZ(float3 p, float r, float h) {   // axis z, half length h
    const float dx = sqrtf(p.x * p.x + p.y * p.y) - r, dz = fabsf(p.z) - h;
    return fminf(fmaxf(dx, dz), 0.0f) + sqrtf(sqr(fmaxf(dx, 0.0f)) + sqr(fmaxf(dz, 0.0f)));
}
__device__ __forceinline__ float sdCylY(float3 p, float r, float h) {   // axis y, half length h
    const float dx = sqrtf(p.x * p.x + p.z * p.z) - r, dy = fabsf(p.y) - h;
    return fminf(fmaxf(dx, dy), 0.0f) + sqrtf(sqr(fmaxf(dx, 0.0f)) + sqr(fmaxf(dy, 0.0f)));
}
__device__ __forceinline__ float sdCapsule(float3 p, float3 a, float3 b, float r) {
    const float3 pa = p - a, ba = b - a;
    const float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h) - r;
}
__device__ __forceinline__ float sdTorusY(float3 p, float R, float r) {   // in the xz-plane
    const float q = sqrtf(p.x * p.x + p.z * p.z) - R;
    return sqrtf(q * q + p.y * p.y) - r;
}
__device__ __forceinline__ void opU(float& d, int& id, float dn, int idn) {
    if (dn < d) {
        d = dn;
        id = idn;
    }
}

// the hoses from the pump's top: up, then away to the right and back into the haze (towards a radiator unseen), as
// four straight pieces (x = where the hose leaves the pump)
__device__ __forceinline__ float3 hosePoint(float x, int k) {
    const float xs[5] = {0.0f, 4.0f, 14.0f, 34.0f, 70.0f};
    const float ys[5] = {48.0f, 72.0f, 92.0f, 104.0f, 110.0f};
    const float zs[5] = {0.0f, -6.0f, -20.0f, -40.0f, -70.0f};
    return make_float3(x + xs[k], ys[k], kPumpZ + zs[k]);
}
__device__ float sdHose(float3 p, float x, float& u) {
    float best = 1e30f;
    float acc = 0.0f;
    u = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const float3 a = hosePoint(x, k), b = hosePoint(x, k + 1);
        const float3 pa = p - a, ba = b - a;
        const float len = length(ba);
        const float h = saturate(dot(pa, ba) / (len * len));
        const float d = length(pa - ba * h) - 3.2f;
        if (d < best) {
            best = d;
            u = acc + h * len;
        }
        acc += len;
    }
    return best;
}

// the console's frame: its top tilted towards the person on the balcony
constexpr float kConsoleY = 24.4f, kConsoleZ = 58.6f, kConsoleTilt = 0.6f;
__device__ __forceinline__ float3 toConsole(float3 p) {
    const float3 q = p - make_float3(0.0f, kConsoleY, kConsoleZ);
    const float c = cosf(kConsoleTilt), s = sinf(kConsoleTilt);
    return make_float3(q.x, c * q.y + s * q.z, -s * q.y + c * q.z);
}
__device__ __forceinline__ float3 toPsu(float3 p) {
    const float3 q = p - make_float3(kPsuX, kPsuY, kPsuZ);
    const float c = cosf(kPsuYaw), s = sinf(kPsuYaw);
    return make_float3(c * q.x - s * q.z, q.y, s * q.x + c * q.z);
}

// The world's distance and what is nearest. `u` carries a coordinate along pipes for the lava's bands.
__device__ float mapScene(float3 p, const IsiView& v, int& id, float& u) {
    float d = p.y;   // the board
    id = kGround;
    u = 0.0f;
    opU(d, id, kSky - p.y, kCeiling);

    // the board's small parts: one box in some cells, kept 1.5 m from the cell's edge so the cell bounds the rest
    if (p.y < 7.0f) {
        const float cs = 16.0f;
        const float cx = floorf(p.x / cs), cz = floorf(p.z / cs);
        const float mx = (cx + 0.5f) * cs, mz = (cz + 0.5f) * cs;
        float dc = 1e30f;   // no box in this cell
        const bool avenue = fabsf(mx) < 34.0f && mz > kWallZ - 12.0f && mz < 90.0f;
        if (!avenue && mx * mx + mz * mz < 1.3e6f && hash1(int(cx), int(cz), 1) < 0.42f) {
            const float hx = 1.0f + 2.6f * hash1(int(cx), int(cz), 2), hy = 0.6f + 2.2f * hash1(int(cx), int(cz), 3);
            const float hz = 1.0f + 2.6f * hash1(int(cx), int(cz), 4);
            const float ox = (hash1(int(cx), int(cz), 5) - 0.5f) * (cs - 3.0f - 2.0f * hx);
            const float oz = (hash1(int(cx), int(cz), 6) - 0.5f) * (cs - 3.0f - 2.0f * hz);
            dc = sdBox(p - make_float3(mx + ox, hy, mz + oz), make_float3(hx, hy, hz));
        }
        const float edge = fminf(0.5f * cs - fabsf(p.x - mx), 0.5f * cs - fabsf(p.z - mz)) + 1.5f;
        opU(d, id, fminf(dc, fmaxf(edge, p.y - 5.6f)), kChip);
    }

    // ---- the graphics card: a wall with three fans, the hot fins behind them, heat pipes on its crown
    {
        const float bound = sdBox(p - make_float3(0.0f, 72.0f, kWallZ - 18.0f), make_float3(124.0f, 72.0f, 32.0f));
        if (bound > d) {
            // nothing of it is nearer than what we have
        } else {
            float body = sdBox(p - make_float3(0.0f, 60.0f, kWallZ - 18.0f), make_float3(110.0f, 60.0f, 18.0f));
            const float chamber = sdBox(p - make_float3(0.0f, kFanY, kWallZ - 24.0f), make_float3(104.0f, 38.0f, 10.0f));
            float holes = 1e30f;
            for (int i = 0; i < 3; ++i) holes = fminf(holes, sdCylZ(p - make_float3(fanX(i), kFanY, kWallZ - 7.0f), kFanR, 7.5f));
            body = fmaxf(body, -holes);
            body = fmaxf(body, -chamber);
            opU(d, id, body, kWall);
            // the plinth it stands on
            opU(d, id, sdBox(p - make_float3(0.0f, 3.0f, kWallZ + 10.0f), make_float3(122.0f, 3.0f, 10.0f)), kWall);
            // the fins: thin plates across x, only inside the chamber
            const float fx = p.x - 2.6f * floorf(p.x / 2.6f + 0.5f);
            const float fin = fmaxf(sdBox(make_float3(fx, p.y - kFanY, p.z - (kWallZ - 24.0f)), make_float3(0.22f, 38.0f, 10.0f)), chamber);
            opU(d, id, fin, kFin);
            // the heat pipes through the fins
            const float pz = p.z - (kWallZ - 24.0f);
            const float ys[4] = {38.0f, 52.0f, 76.0f, 90.0f};
            for (int k = 0; k < 4; ++k) {
                const float hp = fmaxf(sqrtf(sqr(p.y - ys[k]) + pz * pz) - 1.7f, fabsf(p.x) - 103.0f);
                if (hp < d) {
                    d = hp;
                    id = kHeatPipe;
                    u = p.x;
                }
            }
            // the fans: a hub and nine blades each, turning
            for (int i = 0; i < 3; ++i) {
                const float3 r = p - make_float3(fanX(i), kFanY, kWallZ - 7.0f);
                if (sqrtf(r.x * r.x + r.y * r.y) > kFanR + 1.0f || fabsf(r.z) > 9.0f) continue;
                opU(d, id, sdCylZ(r - make_float3(0.0f, 0.0f, -1.0f), 8.0f, 6.0f), kBlade);
                const float sa = 6.2831853f / 9.0f;
                float a = atan2f(r.y, r.x) - v.fanAngle - 0.7f * float(i);
                a -= sa * floorf(a / sa + 0.5f);
                const float rad = sqrtf(r.x * r.x + r.y * r.y);
                const float bx = rad * cosf(a), by = rad * sinf(a);
                const float ct = 0.80f, st = 0.60f;   // the blade's pitch, ~37 degrees
                const float ty = by * ct - (r.z + 1.0f) * st, tz = by * st + (r.z + 1.0f) * ct;
                const float hw = fminf(5.4f, 0.30f * rad);
                opU(d, id, sdBox(make_float3(bx - 20.0f, ty, tz), make_float3(11.5f, hw, 0.35f)) * 0.8f, kBlade);
            }
            // the crown: eight heat pipes standing out of its top, hot at the ends
            const int k = min(7, max(0, int(floorf((p.x + 84.0f) / 24.0f + 0.5f))));
            const float x0 = -84.0f + 24.0f * float(k);
            const float organ = sdCapsule(p, make_float3(x0, 112.0f, kWallZ - 18.0f), make_float3(x0, 138.0f, kWallZ - 18.0f), 2.6f);
            if (organ < d) {
                d = organ;
                id = kOrgan;
                u = p.y;
            }
        }
    }

    // ---- the memory: two rows of eight towers on their plinths, a light on every spire
    {
        const float bound = sdBox(p - make_float3(-85.0f, 72.0f, -42.0f), make_float3(24.0f, 72.0f, 62.0f));
        if (bound < d) {
            for (int row = 0; row < 2; ++row) {
                const float x = ramX(row);
                opU(d, id, sdBox(p - make_float3(x, 2.0f, kRamZ0 - 3.5f * kRamDZ), make_float3(8.0f, 2.0f, 4.0f * kRamDZ)), kPlinth);
                const int k = min(7, max(0, int(floorf((kRamZ0 - p.z) / kRamDZ + 0.5f))));
                const float z = kRamZ0 - kRamDZ * float(k);
                const float3 q = p - make_float3(x, 0.0f, z);
                opU(d, id, sdBox(q - make_float3(0.0f, 56.0f, 0.0f), make_float3(5.0f, 52.0f, 5.0f)), kTower);
                opU(d, id, sdBox(q - make_float3(0.0f, 120.0f, 0.0f), make_float3(3.6f, 12.0f, 3.6f)), kTower);
                opU(d, id, sdCapsule(q, make_float3(0.0f, 132.0f, 0.0f), make_float3(0.0f, 139.0f, 0.0f), 0.35f), kSteel);
                opU(d, id, length(q - make_float3(0.0f, 139.6f, 0.0f)) - 0.6f, kBeacon);
            }
        }
    }

    // ---- the processor's pump: a temple on a stepped base, a ring of light on top, two hoses bending up and back
    {
        const float bound = sdBox(p - make_float3(kPumpX + 30.0f, 58.0f, kPumpZ - 30.0f), make_float3(75.0f, 60.0f, 70.0f));
        if (bound < d) {
            const float3 q = p - make_float3(kPumpX, 0.0f, kPumpZ);
            opU(d, id, sdBox(q - make_float3(0.0f, 3.0f, 0.0f), make_float3(32.0f, 3.0f, 32.0f)), kPlinth);
            opU(d, id, sdBox(q - make_float3(0.0f, 8.0f, 0.0f), make_float3(27.0f, 2.0f, 27.0f)), kPlinth);
            opU(d, id, sdCylY(q - make_float3(0.0f, 28.0f, 0.0f), 22.0f, 18.0f), kPump);
            opU(d, id, sdCylY(q - make_float3(0.0f, 47.5f, 0.0f), 15.0f, 1.6f), kPump);
            opU(d, id, sdTorusY(q - make_float3(0.0f, 46.4f, 0.0f), 18.5f, 1.2f), kHalo);
            for (int h = 0; h < 2; ++h) {
                float hu;
                const float hd = sdHose(p, kPumpX - 8.0f + 16.0f * float(h), hu);
                if (hd < d) {
                    d = hd;
                    id = kHose;
                    u = hu;
                }
            }
        }
    }

    // ---- the MOSFETs: a long finned block behind the pump, its foot glowing
    {
        const float bound = sdBox(p - make_float3(145.0f, 16.0f, -195.0f), make_float3(12.0f, 16.0f, 57.0f));
        if (bound < d) {
            opU(d, id, sdBox(p - make_float3(145.0f, 8.0f, -195.0f), make_float3(10.0f, 8.0f, 55.0f)), kVrm);
            const float fz = p.z - 3.2f * floorf(p.z / 3.2f + 0.5f);
            const float fin = fmaxf(sdBox(make_float3(p.x - 145.0f, p.y - 23.0f, fz), make_float3(10.0f, 7.0f, 0.45f)), fabsf(p.z + 195.0f) - 53.0f);
            opU(d, id, fin, kVrm);
        }
    }

    // ---- the lava: two pipes from under the balcony to the wall, and one from the pump
    {
        const float bound = fminf(sdBox(p - make_float3(0.0f, 4.0f, (40.0f + kWallZ) * 0.5f), make_float3(20.0f, 4.0f, (40.0f - kWallZ) * 0.5f + 4.0f)),
                                  sdBox(p - make_float3(47.0f, 4.0f, -185.0f), make_float3(36.0f, 4.0f, 50.0f)));
        if (bound < d) {
            for (int s = -1; s <= 1; s += 2) {
                const float lp = sdCapsule(p, make_float3(14.0f * s, 3.0f, 40.0f), make_float3(14.0f * s, 3.0f, kWallZ + 2.0f), 2.6f);
                if (lp < d) {
                    d = lp;
                    id = kLava;
                    u = -p.z;   // the bands travel towards the balcony: heat coming from the card
                }
            }
            const float lp2 = fminf(sdCapsule(p, make_float3(78.0f, 3.0f, -140.0f), make_float3(30.0f, 3.0f, -190.0f), 2.2f),
                                    sdCapsule(p, make_float3(30.0f, 3.0f, -190.0f), make_float3(16.0f, 3.0f, kWallZ + 2.0f), 2.2f));
            if (lp2 < d) {
                d = lp2;
                id = kLava;
                u = -p.z * 1.1f;
            }
        }
    }

    // ---- the storage: a long low archive, its lights on top
    opU(d, id, sdBox(p - make_float3(52.0f, 2.5f, -2.0f), make_float3(22.0f, 2.5f, 8.0f)), kSsd);

    // ---- lamps along the avenue: the size of a person, for the scale
    {
        const float lb = fminf(fabsf(p.x - 24.0f), fabsf(p.x + 24.0f)) - 1.0f;
        if (lb < d && p.y < 8.0f) {
            const float x = p.x > 0.0f ? 24.0f : -24.0f;
            const int k = min(15, max(0, int(floorf((32.0f - p.z) / 16.0f + 0.5f))));
            const float z = 32.0f - 16.0f * float(k);
            opU(d, id, sdCapsule(p, make_float3(x, 0.0f, z), make_float3(x, 5.0f, z), 0.16f), kSteel);
            opU(d, id, length(p - make_float3(x, 5.35f, z)) - 0.42f, kLamp);
        }
    }

    // ---- the power supply: a far dark mountain of a box, its fan glowing, its cables reaching for the card
    {
        const float3 q = toPsu(p);
        const float bound = sdBox(q, make_float3(210.0f, 180.0f, 160.0f));
        if (bound < d) opU(d, id, sdBox(q, make_float3(200.0f, 170.0f, 150.0f)), kPsu);
        const float cb = sdBox(p - make_float3(258.0f, 18.0f, -632.0f), make_float3(170.0f, 26.0f, 380.0f));
        if (cb < d) {
            for (int k = 0; k < 4; ++k) {
                const float3 a = make_float3(380.0f + 18.0f * k, 14.0f, -1000.0f), b = make_float3(112.0f + 5.0f * k, 14.0f, kWallZ - 34.0f);
                const float3 pa = p - a, ba = b - a;
                const float h = saturate(dot(pa, ba) / dot(ba, ba));
                const float cd = length(pa - ba * h) - 6.0f;
                if (cd < d) {
                    d = cd;
                    id = kCable;
                    u = h * length(ba);
                }
            }
        }
    }

    // ---- the balcony: its pillar, floor, railing and the console of gauges
    {
        const float bound = sdBox(p - make_float3(0.0f, 13.0f, 64.0f), make_float3(10.0f, 13.0f, 10.0f));
        if (bound < d) {
            opU(d, id, sdBox(p - make_float3(0.0f, 11.6f, 66.0f), make_float3(6.0f, 11.6f, 6.0f)), kSteel);
            opU(d, id, sdBox(p - make_float3(0.0f, 23.6f, 63.0f), make_float3(8.0f, 0.4f, 7.0f)), kSteel);
            opU(d, id, sdCapsule(p, make_float3(-8.0f, 25.05f, 57.6f), make_float3(8.0f, 25.05f, 57.6f), 0.05f), kSteel);
            opU(d, id, sdCapsule(p, make_float3(-8.0f, 24.55f, 57.6f), make_float3(8.0f, 24.55f, 57.6f), 0.025f), kSteel);
            const float px = p.x - 1.6f * floorf(p.x / 1.6f + 0.5f);
            if (fabsf(p.x) < 8.1f) opU(d, id, sdCapsule(make_float3(px, p.y, p.z), make_float3(0.0f, 24.0f, 57.6f), make_float3(0.0f, 25.05f, 57.6f), 0.035f), kSteel);
            for (int s = -1; s <= 1; s += 2)
                opU(d, id, sdCapsule(p, make_float3(8.0f * s, 25.05f, 57.6f), make_float3(8.0f * s, 25.05f, 70.0f), 0.05f), kSteel);
            const float3 c = toConsole(p);
            opU(d, id, sdBox(c, make_float3(1.3f, 0.4f, 0.26f)), kGauge);
            opU(d, id, sdBox(p - make_float3(0.0f, 24.2f, 58.8f), make_float3(1.2f, 0.2f, 0.18f)), kSteel);
        }
    }
    return d;
}

__device__ __forceinline__ float mapD(float3 p, const IsiView& v) {
    int id;
    float u;
    return mapScene(p, v, id, u);
}

__device__ float3 normalAt(float3 p, float e, const IsiView& v) {
    const float3 k1 = make_float3(1, -1, -1), k2 = make_float3(-1, -1, 1), k3 = make_float3(-1, 1, -1), k4 = make_float3(1, 1, 1);
    const float3 n = k1 * mapD(p + k1 * e, v) + k2 * mapD(p + k2 * e, v) + k3 * mapD(p + k3 * e, v) + k4 * mapD(p + k4 * e, v);
    const float l2 = dot(n, n);
    return l2 > 1e-20f ? n * rsqrtf(l2) : make_float3(0.0f, 1.0f, 0.0f);
}

__device__ bool march(const IsiView& v, float3 ro, float3 rd, float& t, int& id, float& u) {
    t = 0.02f;
    for (int i = 0; i < v.maxSteps; ++i) {
        const float3 p = ro + rd * t;
        const float d = mapScene(p, v, id, u);
        const float eps = fmaxf(0.0015f, t * v.pixAngle * 0.5f);
        if (d < eps) return true;
        t += d * 0.9f;
        if (t > v.tmax) return false;
    }
    return true;   // out of steps at a grazing surface: call it hit
}

__device__ float softShadow(const IsiView& v, float3 ro, float3 rd, float tmax, float k) {
    float res = 1.0f, t = 0.3f;
    for (int i = 0; i < 72 && t < tmax; ++i) {
        const float h = mapD(ro + rd * t, v);
        if (h < 0.002f) return 0.0f;
        res = fminf(res, k * h / t);
        t += fmaxf(h, 0.2f);
    }
    res = saturate(res);
    return res * res * (3.0f - 2.0f * res);
}

__device__ float occlusion(const IsiView& v, float3 p, float3 n) {
    float occ = 0.0f, w = 1.0f;
    const float hs[4] = {0.4f, 1.4f, 3.5f, 8.0f};
    for (int i = 0; i < 4; ++i) {
        occ += (hs[i] - mapD(p + n * hs[i], v)) / hs[i] * w;
        w *= 0.7f;
    }
    return saturate(1.0f - 0.45f * occ);
}

// ---------------------------------------------------------------------------------------------------------------
// light and colour
// heat 0..1 as a glowing colour: dark red, orange, yellow-white (a hot metal's run, by eye)
__device__ __forceinline__ float3 hot(float h) {
    h = saturate(h);
    const float3 c1 = make_float3(0.85f, 0.10f, 0.02f), c2 = make_float3(1.0f, 0.36f, 0.06f), c3 = make_float3(1.0f, 0.74f, 0.38f);
    return h < 0.5f ? lerp3(c1, c2, h * 2.0f) : lerp3(c2, c3, h * 2.0f - 1.0f);
}
// the haze's colour and the sky's: cold blue going amber as the energy spent grows
__device__ __forceinline__ float3 coldWarm(const IsiView& v, float3 cold, float3 warm) { return lerp3(cold, warm, v.energy); }

// a smooth 0..1 stripe of width w around 0 of a periodic coordinate x (period 1), its edge softened over fw
__device__ __forceinline__ float band(float x, float w, float fw) {
    const float f = fabsf(x - floorf(x + 0.5f));
    return 1.0f - smoothstepf(w - fw, w + fw, f);
}

// the gauges' faces: seven backlit dials in a row on the console's tilted top
__device__ float3 gaugeFace(const IsiView& v, float3 p, float fp, bool& isFace) {
    const float3 c = toConsole(p);
    isFace = false;
    if (c.y < 0.39f) return make_float3(0, 0, 0);
    const int i = int(floorf((c.x + 1.08f) / 0.36f + 0.5f));
    const float x0 = -1.08f + 0.36f * float(i);
    if (i < 0 || i > 6) return make_float3(0, 0, 0);
    const float dx = (c.x - x0) / 0.65f, dz = c.z / 0.65f;
    const float r = sqrtf(dx * dx + dz * dz);
    if (r > 0.25f) return make_float3(0, 0, 0);
    isFace = true;
    const float aa = fmaxf(fp / 0.65f, 0.002f);
    const float3 cream = make_float3(1.0f, 0.86f, 0.62f);
    float3 col = cream * 0.55f;
    const float bezel = smoothstepf(0.205f - aa, 0.205f + aa, r);
    // the angle on the face: 0 at the bottom-left end, 1 at the bottom-right (270 degrees), up = away from the person
    float ang = atan2f(dx, -dz);   // 0 straight up
    const float val = (ang + 2.356f) / 4.712f;
    // ticks
    const float tick = band(val * 10.0f, 0.06f, aa * 10.0f / (r * 4.712f + 1e-3f)) * smoothstepf(0.15f, 0.16f, r) * (1.0f - smoothstepf(0.19f, 0.195f, r));
    const float redZone = (val > 0.8f && val < 1.0f) ? smoothstepf(0.17f, 0.175f, r) * (1.0f - smoothstepf(0.19f, 0.195f, r)) : 0.0f;
    col = lerp3(col, make_float3(0.05f, 0.04f, 0.03f), tick * 0.9f);
    col = lerp3(col, make_float3(0.7f, 0.06f, 0.03f), redZone);
    // the needle
    const float na = -2.356f + 4.712f * saturate(v.gauge[i]);
    const float2 nd = make_float2(sinf(na), -cosf(na));
    const float along = dx * nd.x + dz * nd.y, across = fabsf(dx * nd.y - dz * nd.x);
    const float needle = (along > -0.03f && along < 0.18f) ? 1.0f - smoothstepf(0.006f - aa, 0.006f + aa, across) : 0.0f;
    col = lerp3(col, make_float3(0.02f, 0.02f, 0.02f), needle);
    const float hub = 1.0f - smoothstepf(0.016f - aa, 0.016f + aa, r);
    col = lerp3(col, make_float3(0.03f, 0.03f, 0.03f), hub);
    return lerp3(col, make_float3(0.008f, 0.007f, 0.006f), bezel);
}

struct Surface {
    float3 albedo{0.05f, 0.05f, 0.05f}, emit{0, 0, 0};
    float spec = 0.0f;
};

__device__ Surface material(const IsiView& v, float3 p, float3 n, int id, float u, float t) {
    Surface s;
    const float fp = t * v.pixAngle;   // a pixel's footprint here (m)
    const float awakeTotal = saturate(0.45f * v.gpu + 0.3f * v.cpu + 0.15f * v.ram + 0.1f * v.ssd);
    switch (id) {
    case kGround: {
        // dark board with copper-gold traces: lines along x and z on a 9 m grid, some cells only
        s.albedo = make_float3(0.018f, 0.030f, 0.026f);
        const float g = 9.0f;
        const int cx = int(floorf(p.x / g)), cz = int(floorf(p.z / g));
        const float lx = band(p.z / g, 0.035f, fp / g), lz = band(p.x / g, 0.035f, fp / g);
        const float onX = hash1(cx, cz, 11) < 0.55f ? 1.0f : 0.0f, onZ = hash1(cx, cz, 12) < 0.45f ? 1.0f : 0.0f;
        float tr = fmaxf(lx * onX, lz * onZ);
        tr *= 1.0f - smoothstepf(0.4f, 1.2f, fp / g * 10.0f);   // far: the lines fade into the board's tone
        s.albedo = lerp3(s.albedo, make_float3(0.32f, 0.22f, 0.07f), tr);
        s.spec = 0.3f * tr;
        // data along the traces when the memory or the storage is awake: pulses running towards the processor
        const float pulse = band((p.x + p.z) / 60.0f + v.time * 0.6f, 0.04f, 0.02f) * tr * (0.6f * v.ram + 0.4f * v.ssd);
        s.emit = make_float3(1.0f, 0.62f, 0.25f) * (pulse * 3.0f);
        break;
    }
    case kWall: s.albedo = make_float3(0.055f, 0.055f, 0.06f); s.spec = 0.4f; break;
    case kBlade: s.albedo = make_float3(0.012f, 0.012f, 0.014f); s.spec = 0.3f; break;
    case kFin: {
        const float depth = saturate((kWallZ - 14.0f - p.z) / 20.0f);
        s.albedo = make_float3(0.12f, 0.11f, 0.10f);
        s.emit = hot(0.18f + 0.45f * depth * v.gpu) * (v.gpu * (0.25f + 2.6f * depth * depth));
        break;
    }
    case kHeatPipe: {
        s.albedo = make_float3(0.3f, 0.12f, 0.05f);
        s.emit = hot(0.55f + 0.35f * v.gpu) * (v.gpu * (2.5f + 2.0f * band(u / 30.0f - v.flow / 30.0f, 0.18f, 0.05f)));
        break;
    }
    case kOrgan: {
        const float top = saturate((u - 112.0f) / 26.0f);
        s.albedo = make_float3(0.25f, 0.12f, 0.06f);
        s.spec = 0.5f;
        s.emit = hot(0.35f + 0.5f * top * v.gpu) * (v.gpu * (0.15f + 6.0f * top * top * top));
        break;
    }
    case kTower: {
        s.albedo = make_float3(0.045f, 0.05f, 0.065f);
        s.spec = 0.25f;
        // windows on the side faces: floors of 3.2 m, columns of 2.2 m, lit by the data
        if (fabsf(n.y) < 0.5f) {
            const int row = fabsf(p.x - ramX(0)) < fabsf(p.x - ramX(1)) ? 0 : 1;
            const int k = min(7, max(0, int(floorf((kRamZ0 - p.z) / kRamDZ + 0.5f))));
            const float across = fabsf(n.x) > 0.5f ? p.z : p.x;
            const float fy = p.y / 3.2f, fx = across / 2.2f;
            const int fl = int(floorf(fy)), col = int(floorf(fx));
            const float slot = floorf(v.time * (0.4f + 2.5f * v.ram) + hash1(row * 8 + k, fl, col));
            const float frac = 0.06f + 0.34f * v.ram;
            const float lit = hash1(row * 8 + k + 100 * int(slot), fl, col) < frac ? 1.0f : 0.0f;
            const float win = band(fy - 0.5f, 0.22f, fp / 3.2f) * band(fx - 0.5f, 0.3f, fp / 2.2f);
            const float far = smoothstepf(0.25f, 0.9f, fp);   // past a window a pixel: their mean
            const float w = lerpf(win * lit, 0.22f * 0.3f * 4.0f * frac, far);
            const float rise = band(p.y / 46.0f - v.time * 0.35f, 0.05f, 0.03f) * v.ram;   // the data climbing
            const float3 wc = lerp3(make_float3(0.75f, 0.86f, 1.0f), make_float3(1.0f, 0.82f, 0.6f), hash1(fl, col, 7));
            s.emit = wc * (w * (0.9f + 1.4f * v.ram) + rise * 1.5f * w + rise * 0.05f);
            s.albedo = lerp3(s.albedo, make_float3(0.02f, 0.025f, 0.03f), win * (1.0f - far));
        }
        break;
    }
    case kBeacon: {
        const float blink = 0.5f + 0.5f * sinf(v.time * 2.1f + p.z * 0.31f);
        s.albedo = make_float3(0.1f, 0.02f, 0.02f);
        s.emit = make_float3(1.0f, 0.08f, 0.04f) * (4.0f + 20.0f * blink * blink);
        break;
    }
    case kPlinth: s.albedo = make_float3(0.05f, 0.05f, 0.055f); s.spec = 0.2f; break;
    case kPump: s.albedo = make_float3(0.03f, 0.03f, 0.034f); s.spec = 0.8f; break;
    case kHalo: {
        s.albedo = make_float3(0.2f, 0.2f, 0.2f);
        s.emit = lerp3(make_float3(0.55f, 0.65f, 0.9f) * 0.6f, hot(0.7f) * 9.0f, v.cpu);
        break;
    }
    case kHose: {
        s.albedo = make_float3(0.03f, 0.03f, 0.03f);
        s.spec = 0.4f;
        const float b = band((u - v.flow * 0.8f) / 20.0f, 0.10f, fp / 20.0f + 0.01f);
        const float slit = smoothstepf(0.88f - fp * 0.3f, 0.96f, fabsf(n.x));
        s.emit = hot(0.3f + 0.3f * b) * (v.cpu * slit * (0.5f + 2.5f * b));
        break;
    }
    case kLava: {
        s.albedo = make_float3(0.04f, 0.035f, 0.03f);
        s.spec = 0.5f;
        const float b = band((u + v.flow) / 24.0f, 0.10f, fp / 24.0f + 0.01f);
        const float slit = smoothstepf(0.90f - fp * 0.3f, 0.97f, n.y);
        s.emit = hot(0.3f + 0.25f * v.gpu + 0.3f * b) * (v.gpu * slit * (0.5f + 2.5f * b));
        break;
    }
    case kVrm: {
        s.albedo = make_float3(0.11f, 0.11f, 0.12f);
        s.spec = 0.4f;
        // the foot: a slit glowing with the MOSFETs' heat
        const float foot = (1.0f - smoothstepf(2.6f, 3.4f, p.y)) * (p.y < 16.0f ? 1.0f : 0.0f);
        s.emit = hot(0.2f + 0.5f * v.cpu) * (foot * (0.15f + 5.0f * v.cpu));
        break;
    }
    case kSsd: {
        s.albedo = make_float3(0.03f, 0.03f, 0.035f);
        if (n.y > 0.5f || n.z > 0.5f) {
            const float cell = band(p.x / 2.4f, 0.22f, fp / 2.4f) * band((p.z + 2.0f) / 16.0f, 0.08f, fp / 16.0f);
            const float chase = band(p.x / 44.0f - v.time * 1.2f, 0.12f, 0.02f);
            s.emit = make_float3(0.5f, 0.85f, 1.0f) * (cell * (0.08f + v.ssd * (0.8f + 6.0f * chase)));
        }
        break;
    }
    case kPsu: {
        s.albedo = make_float3(0.008f, 0.008f, 0.009f);
        const float3 q = toPsu(p);
        if (q.z > 149.0f) {   // the face towards the board: the fan
            const float r = sqrtf(q.x * q.x + sqr(q.y - 10.0f));
            const float ring = 1.0f - smoothstepf(5.0f - fp, 5.0f + fp, fabsf(r - 125.0f)) + 0.5f * (1.0f - smoothstepf(24.0f - fp, 24.0f + fp, r));
            s.emit = hot(0.15f + 0.4f * awakeTotal) * (ring * (0.4f + 2.5f * awakeTotal));
        }
        break;
    }
    case kCable: {
        s.albedo = make_float3(0.02f, 0.02f, 0.02f);
        s.spec = 0.3f;
        const float b = band((u - v.flow * 3.0f) / 40.0f, 0.05f, fp / 40.0f + 0.01f);
        s.emit = make_float3(1.0f, 0.68f, 0.15f) * (b * (0.3f + 4.0f * awakeTotal) * saturate(n.y + 0.3f));
        break;
    }
    case kSteel: s.albedo = make_float3(0.06f, 0.06f, 0.065f); s.spec = 0.5f; break;
    case kGauge: {
        bool face;
        const float3 f = gaugeFace(v, p, fp, face);
        if (face) {
            s.albedo = make_float3(0.0f, 0.0f, 0.0f);
            s.emit = f * 0.3f;
        } else {
            s.albedo = make_float3(0.035f, 0.035f, 0.04f);
            s.spec = 0.4f;
        }
        break;
    }
    case kLamp: s.albedo = make_float3(0.2f, 0.2f, 0.2f); s.emit = make_float3(1.0f, 0.72f, 0.42f) * 14.0f; break;
    case kChip: s.albedo = make_float3(0.035f, 0.035f, 0.04f); s.spec = 0.3f; break;
    case kCeiling: {
        // the perforated panel from inside: holes on a staggered grid, the room's light through them
        s.albedo = make_float3(0.01f, 0.01f, 0.012f);
        const float g = v.skyHole;
        const float sz = p.z / (g * 0.866f);
        const float row = floorf(sz + 0.5f);   // the nearest row; every other one shifted half a hole
        const float sx = p.x / g + 0.5f * fmodf(fabsf(row), 2.0f);
        const float dx = (sx - floorf(sx + 0.5f)) * g, dz = (sz - row) * g * 0.866f;
        const float r = sqrtf(dx * dx + dz * dz);
        // every hole its own brightness, most of them faint: a field of stars, not a lit ceiling
        const float h = hash1(int(floorf(sx + 0.5f)), int(row), 21);
        const float star = 0.04f + 4.0f * h * h * h * h * h * h;
        const float hole = (1.0f - smoothstepf(1.2f - fp, 1.2f + fp, r)) * star;
        const float far = smoothstepf(0.6f, 2.5f, fp);
        const float lit = lerpf(hole, 0.1f * 4.5f / (g * g * 0.866f), far);
        s.emit = coldWarm(v, make_float3(0.7f, 0.82f, 1.0f), make_float3(1.0f, 0.82f, 0.62f)) * (lit * 3.0f);
        break;
    }
    default: break;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// the haze: an exponential layer over the board, integrated in closed form along the ray
__device__ __forceinline__ float fogDepth(const IsiView& v, float3 ro, float3 rd, float t) {
    const float k = 1.0f / v.fogHeight;
    const float a = v.fogDensity * expf(-ro.y * k);
    const float b = rd.y * k;
    if (fabsf(b * t) < 1e-4f) return a * t;
    return a * (1.0f - expf(fminf(-b * t, 30.0f))) / b;
}

// the hot parts' glow in the haze: a point light's in-scattering integrated in closed form (atan)
__device__ float3 lightGlow(const IsiView& v, float3 ro, float3 rd, float tEnd) {
    float3 g = make_float3(0, 0, 0);
    for (int i = 0; i < kIsiLights; ++i) {
        const IsiLight& L = v.light[i];
        if (maxc(L.color) <= 0.0f) continue;
        const float3 w = L.pos - ro;
        const float t0 = dot(w, rd);
        const float h = sqrtf(fmaxf(dot(w, w) - t0 * t0, 0.0f) + L.soft * L.soft);
        const float ang = atanf((tEnd - t0) / h) - atanf(-t0 / h);
        const float dens = v.fogDensity * expf(-L.pos.y / v.fogHeight);
        const float T = expf(-fogDepth(v, ro, rd, clampf(t0, 0.0f, tEnd)));
        g += L.color * (dens * ang / h * T * (0.25f * INV_PIf));
    }
    return g * v.glowLe;
}

// the beams from the sky's holes: a Gaussian tube's in-scattering integrated in closed form (erf)
__device__ float3 beamGlow(const IsiView& v, float3 ro, float3 rd, float tEnd) {
    float sum = 0.0f;
    for (int i = 0; i < v.beams; ++i) {
        const IsiBeam B = v.beam[i];
        const float3 w = ro - B.top;
        const float wb = dot(w, B.dir), db = dot(rd, B.dir);
        const float3 uu = w - B.dir * wb, vv = rd - B.dir * db;
        const float a = fmaxf(dot(vv, vv), 1e-6f), bq = dot(uu, vv), c = dot(uu, uu);
        const float dmin2 = fmaxf(c - bq * bq / a, 0.0f);
        const float s2 = B.sigma * B.sigma;
        if (dmin2 > 12.0f * s2) continue;
        float t0 = 0.0f, t1 = tEnd;
        if (fabsf(db) > 1e-6f) {
            float ta = -wb / db, tb = (B.len - wb) / db;
            if (ta > tb) {
                const float x = ta;
                ta = tb;
                tb = x;
            }
            t0 = fmaxf(t0, ta);
            t1 = fminf(t1, tb);
        } else if (wb < 0.0f || wb > B.len) {
            continue;
        }
        if (t1 <= t0) continue;
        const float tc = -bq / a;
        const float sa = sqrtf(a) / B.sigma;
        const float I = 0.8862269f / sa * (erff(sa * (t1 - tc)) - erff(sa * (t0 - tc))) * expf(-dmin2 / s2);
        const float tm = clampf(tc, t0, t1);
        const float3 pm = ro + rd * tm;
        const float dens = v.fogDensity * expf(-pm.y / v.fogHeight);
        const float T = expf(-fogDepth(v, ro, rd, tm));
        // fainter near the bottom: the hole's light spreads as it falls
        const float fall = 1.0f - 0.6f * saturate(dot(pm - B.top, B.dir) / fmaxf(B.len, 1.0f));
        sum += I * dens * T * B.le * fall;
    }
    return coldWarm(v, make_float3(0.72f, 0.84f, 1.0f), make_float3(1.0f, 0.8f, 0.58f)) * (sum * v.beamLe * 0.25f * INV_PIf);
}

// the light a beam lays on a surface where it lands
__device__ float beamLight(const IsiView& v, float3 p, float3 n) {
    float e = 0.0f;
    for (int i = 0; i < v.beams; ++i) {
        const IsiBeam B = v.beam[i];
        const float3 w = p - B.top;
        const float s = dot(w, B.dir);
        if (s < 0.0f || s > B.len + 1.5f) continue;
        const float d2 = dot(w, w) - s * s;
        if (d2 > 9.0f * B.sigma * B.sigma) continue;
        e += B.le * expf(-d2 / (B.sigma * B.sigma)) * fmaxf(-dot(n, B.dir), 0.0f);
    }
    return e;
}

__device__ float3 shadeHit(const IsiView& v, float3 ro, float3 rd, float t, int id, float u) {
    const float3 p = ro + rd * t;
    const float e = fmaxf(0.004f, t * v.pixAngle);
    float3 n = normalAt(p, e, v);
    if (dot(n, rd) > 0.0f) n = normalize(n - rd * (2.0f * dot(n, rd)));
    const Surface s = material(v, p, n, id, u, t);
    float3 col = s.emit;
    if (maxc(s.albedo) <= 0.0f) return col;
    const float ao = occlusion(v, p, n);
    // the sky's light: the room's light through the panel, cold (amber as the energy grows)
    const float3 sky = coldWarm(v, make_float3(0.030f, 0.040f, 0.065f), make_float3(0.06f, 0.042f, 0.025f)) * v.skyLe;
    float3 irr = sky * ((0.35f + 0.65f * saturate(n.y * 0.5f + 0.5f)) * ao * M_PIf);
    float3 specAcc = make_float3(0, 0, 0);
    for (int i = 0; i < kIsiLights; ++i) {
        const IsiLight& L = v.light[i];
        if (maxc(L.color) <= 0.0f) continue;
        float3 l = L.pos - p;
        const float d2 = dot(l, l);
        const float dist = sqrtf(d2);
        l = l * (1.0f / dist);
        const float cosT = dot(n, l);
        if (cosT <= 0.0f) continue;
        float vis = 1.0f;
        if (i == 1) vis = softShadow(v, p + n * (e * 3.0f), l, dist - L.soft, 10.0f);   // the middle fan's light: shadows
        const float3 E = L.color * (cosT * vis / (d2 + L.soft * L.soft));
        irr += E;
        if (s.spec > 0.0f) {
            const float3 h = normalize(l - rd);
            specAcc += E * (s.spec * powf(fmaxf(dot(n, h), 0.0f), 48.0f) * 6.0f);
        }
    }
    irr += coldWarm(v, make_float3(0.72f, 0.84f, 1.0f), make_float3(1.0f, 0.8f, 0.58f)) * (beamLight(v, p, n) * v.beamLe * 0.06f);
    col += s.albedo * INV_PIf * irr + specAcc * INV_PIf;
    return col;
}

__device__ float3 rayDir(const IsiView& v, float px, float py) {
    const float aspect = float(v.width) / float(v.height);
    const float sx = (px / v.width * 2.0f - 1.0f) * v.tanHalf * aspect;
    const float sy = (1.0f - py / v.height * 2.0f) * v.tanHalf;
    return normalize(v.camFwd + v.camRight * sx + v.camUp * sy);
}

__device__ float3 radiance(const IsiView& v, float px, float py) {
    const float3 ro = v.camPos, rd = rayDir(v, px, py);
    float t;
    int id;
    float u;
    const bool hit = march(v, ro, rd, t, id, u);
    const float tEnd = hit ? t : v.tmax;
    const float T = expf(-fogDepth(v, ro, rd, tEnd));
    float3 c = hit ? shadeHit(v, ro, rd, t, id, u) * T : make_float3(0, 0, 0);
    const float3 fogCol = coldWarm(v, make_float3(0.006f, 0.010f, 0.022f), make_float3(0.03f, 0.018f, 0.009f)) * v.fogLe;
    c += fogCol * (1.0f - T);
    c += lightGlow(v, ro, rd, tEnd);
    c += beamGlow(v, ro, rd, tEnd);
    return c;
}

__global__ void kFrame(IsiView v, float4* hdr) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    // fixed sub-pixel positions: two on the diagonal, four more (a rotated grid) where those two disagree
    const float2 baseOff[2] = {{-0.25f, -0.25f}, {0.25f, 0.25f}};
    const float2 edgeOff[4] = {{-0.375f, 0.125f}, {0.125f, 0.375f}, {0.375f, -0.125f}, {-0.125f, -0.375f}};
    float3 sum = make_float3(0, 0, 0);
    float lmin = 1e30f, lmax = 0.0f;
    int n = 0;
    const int nb = min(v.base, 2);
    for (int s = 0; s < nb; ++s) {
        const float2 o = nb == 1 ? make_float2(0.0f, 0.0f) : baseOff[s];
        const float3 c = radiance(v, x + 0.5f + o.x, y + 0.5f + o.y);
        sum += c;
        ++n;
        const float l = luminance709(c);
        lmin = fminf(lmin, l);
        lmax = fmaxf(lmax, l);
    }
    if (v.edge > 0 && nb > 1 && lmax - lmin > v.contrast * (lmax + lmin + 0.01f)) {
        for (int s = 0; s < min(v.edge, 4); ++s) {
            sum += radiance(v, x + 0.5f + edgeOff[s].x, y + 0.5f + edgeOff[s].y);
            ++n;
        }
    }
    sum = sum * (1.0f / float(n));
    // a NaN in one pixel would spread through the glare to half the screen: zero it, and count it
    if (!(isfinite(sum.x) && isfinite(sum.y) && isfinite(sum.z))) {
        sum = make_float3(0, 0, 0);
        if (v.badCount) atomicAdd(v.badCount, 1u);
    }
    hdr[y * v.width + x] = make_float4(sum.x, sum.y, sum.z, 1.0f);
}

__device__ float4 tapHalf(const float4* img, int w, int h, float x, float y) {
    x = fminf(fmaxf(x - 0.5f, 0.0f), w - 1.001f);
    y = fminf(fmaxf(y - 0.5f, 0.0f), h - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float tx = x - x0, ty = y - y0;
    const int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    return (img[y0 * w + x0] * (1.0f - tx) + img[y0 * w + x1] * tx) * (1.0f - ty) + (img[y1 * w + x0] * (1.0f - tx) + img[y1 * w + x1] * tx) * ty;
}

__global__ void kResolve(IsiView v, const float4* hdr, const float4* bloomHalf, uchar4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    const float4 a = hdr[i];
    float3 c = make_float3(a.x, a.y, a.z);
    if (bloomHalf) {
        const int hw = max(1, v.width / 2), hh = max(1, v.height / 2);
        const float4 b = tapHalf(bloomHalf, hw, hh, (x + 0.5f) * 0.5f, (y + 0.5f) * 0.5f);
        c += make_float3(b.x, b.y, b.z) * v.bloom;
    }
    const float nx = (x + 0.5f) / v.width * 2.0f - 1.0f, ny = (y + 0.5f) / v.height * 2.0f - 1.0f;
    c = c * ((1.0f - v.vignette * (nx * nx * 0.6f + ny * ny * 0.4f)) * v.exposure);
    c = fmaxf3(c, make_float3(0, 0, 0));
    c = agx(c);
    const float nd = float(pcg(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcg(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}

// each beam marched down from its hole to the first surface it meets
__global__ void kBeamLengths(IsiBeam* beams, int n) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    IsiView v;   // the default world: fans still, nothing awake (the geometry does not depend on it but for the blades)
    IsiBeam b = beams[i];
    float t = 1.5f;
    for (int k = 0; k < 600; ++k) {
        const float3 p = b.top + b.dir * t;
        int id;
        float u;
        float d = mapScene(p, v, id, u);
        if (id == kCeiling) d = 1e30f;   // the panel itself is where the beam starts
        d = fminf(d, fmaxf(p.y, 0.0f) + 0.01f);
        if (d < 0.05f || p.y <= 0.0f) break;
        t += fmaxf(d * 0.9f, 0.05f);
        if (t > 800.0f) break;
    }
    beams[i].len = t;
}

}   // namespace

void isiFindBeamLengths(IsiBeam* beams, int n, cudaStream_t s) {
    if (n <= 0) return;
    kBeamLengths<<<(n + 63) / 64, 64, 0, s>>>(beams, n);
}
void isiFrame(const IsiView& v, float4* hdr, cudaStream_t s) {
    const dim3 b(16, 8);
    kFrame<<<dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y), b, 0, s>>>(v, hdr);
}
void isiResolve(const IsiView& v, const float4* hdr, const float4* bloomHalf, uchar4* out, cudaStream_t s) {
    const dim3 b(16, 8);
    kResolve<<<dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y), b, 0, s>>>(v, hdr, bloomHalf, out);
}
