// Scene 4 "Tanecik": weapons and blast, as data and mathematics on the host (independent of the particle solver).
//   - the loads behind each gun's slider (pistol, rifle, shotgun), real ones with their sources, realistic limits
//   - a projectile's path through tissue and bone: what it hands to the medium, segment by segment (momentum, energy,
//     a temporary-cavity radius), where it stops or with what speed it leaves; calibrated in 10% ordnance gelatin
//   - Kingery-Bulmash airblast (hemispherical TNT surface burst) and the impulse a surface element receives
//   - a sword's energy budget and what cutting costs; the bomb's and the cube's sliders
// Units SI throughout: kg, m, s, N, J, Pa; impulses N s, specific impulses Pa s (= kPa ms).
#pragma once
#include "shared/vec.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace weapons {

// ---- projectiles and loads

enum class Nose : uint8_t {
    Sphere,        // a shot pellet or a BB
    RoundNose,     // a pistol FMJ: stays point-forward in tissue (no yaw within a body's depth)
    Spitzer,       // a rifle bullet: point-forward for its neck length, then yaws through 90 deg to base-forward
    HollowPoint    // expands to expandedDiameter over its first 2 cm of tissue (when fast enough)
};

struct Projectile {
    float mass = 0;               // kg
    float diameter = 0;           // m
    float length = 0;             // m (when sideways it presents diameter x length)
    float velocity = 0;           // m/s at the muzzle (the scene's ranges are a few metres: taken as the impact speed)
    Nose nose = Nose::RoundNose;
    float neckLength = -1;        // Spitzer: m of tissue before yaw begins (Fackler); < 0 never
    float fragmentFraction = 0;   // Spitzer: share of the mass shed when the yaw passes 60 deg (Fackler: M193 36%, M855 50%)
    float expandedDiameter = 0;   // HollowPoint: m
    float expandVelocity = 0;     // HollowPoint: below this impact speed it stays closed (a round nose)
    double momentum() const { return double(mass) * velocity; }
    double energy() const { return 0.5 * double(mass) * velocity * velocity; }
};

struct Load {
    std::string name;
    Projectile shot;              // one bullet, or one pellet
    int pellets = 1;
    float spreadPerMetre = 0;     // shotgun: the pattern's diameter per metre of range (m/m)
    std::string source;           // where the numbers come from, and when they were read
    double momentum() const { return shot.momentum() * pellets; }   // all pellets
    double energy() const { return shot.energy() * pellets; }
};

enum class Gun : uint8_t { Pistol, Rifle, Shotgun };
const char* gunName(Gun g);
// the three named loads of a gun: 0 the lightest (slider 0), 1 the common one (the notch, 0.5), 2 the strongest (1)
const Load& namedLoad(Gun g, int which);
// the slider: 0..0.5 between the lightest and the notch, 0.5..1 between the notch and the strongest. Mass, diameter,
// length and speed are interpolated geometrically (so momentum and energy rise monotonically), the shape's numbers
// linearly, the nose from the nearer named load, the pellet count rounded
Load gunLoad(Gun g, float u);
// the largest speed the load can give a body of this mass if it stopped every pellet in it (p / m)
inline double maxPush(const Load& l, double bodyMass = 70.0) { return l.momentum() / bodyMass; }

// ---- a path through tissue

enum class Medium : uint8_t { Air = 0, Tissue = 1, Bone = 2 };
struct MediumProps {
    float density;                // kg/m^3
    float strength;               // Pa: the Poncelet resistance term
};
const MediumProps& mediumProps(Medium m);

struct Segment {
    float s0 = 0, s1 = 0;         // along the ray (m)
    float depth0 = 0, depth1 = 0; // tissue and bone crossed so far (m): what yaw and expansion follow
    Medium medium = Medium::Air;
    float vIn = 0, vOut = 0;      // m/s
    float massIn = 0, massOut = 0;   // kg; massIn - massOut = fragments shed here
    float dp = 0;                 // momentum handed to the medium here (N s), along the ray (fragments' included)
    float dE = 0;                 // energy handed to the medium here (J)
    float force = 0;              // mean retarding force (N) = dE / length for a bullet that keeps its mass
    float cavityRadius = 0;       // temporary cavity radius (m)
    float crushRadius = 0;        // permanent cavity radius (m): half the width the projectile presents
    float fragmentRadius = 0;     // > 0: fragments shed here spread up to this far from the path (m, Fackler: 7 cm)
    float yawDeg = 0;
};
struct Path {
    std::vector<Segment> segments;
    double p0 = 0, pOut = 0, pDeposited = 0;   // momentum in, carried out, handed over (N s)
    double e0 = 0, eOut = 0, eDeposited = 0;   // energy (J)
    float tissueLength = 0;       // m of tissue and bone crossed
    bool stopped = false;
    float stopS = 0;              // where it stopped (along the ray, m)
    float exitSpeed = 0;          // when it did not stop (m/s)
    float exitMass = 0;
    float peakForce = 0;          // N
    float maxCavityRadius = 0, maxCavityAt = 0;   // m, and its tissue depth
    float energyWithin(float tissueDepth) const;   // energy handed over in the first tissueDepth of tissue (E15: 0.15)
};
// what is at distance s along the ray
using MediumAt = std::function<Medium(float s)>;
// march a projectile from s = 0 along a ray up to maxLength (m), steps of ds through tissue and bone (air steps are merged
// into one segment and cost nothing). Exact Poncelet integration within a step: m v dv/ds = -(1/2 rho Cd A v^2 + sigma A).
Path traceShot(const Projectile& p, const MediumAt& medium, float maxLength, float ds = 0.002f);
inline Path traceThroughTissue(const Projectile& p, float maxLength, float ds = 0.002f) {
    return traceShot(p, [](float) { return Medium::Tissue; }, maxLength, ds);
}
// the temporary cavity as a radial flow (for the solver): tissue at distance r from the path of a segment moves outward at
// V0 * r0 / r for r0 <= r <= the cavity radius (an incompressible cylinder), with V0 set so the flow carries share of the
// segment's energy. r0: the innermost radius the solver resolves (the crush radius, or half a particle spacing). m/s.
float radialSpeed(const Segment& seg, float r, float r0, float share = 0.8f);

// the pellets' directions: uniform over the pattern's disc (diameter spreadPerMetre at 1 m), deterministic by seed
std::vector<float3> pelletDirections(const Load& l, float3 dir, unsigned seed);

// ---- blast (Kingery and Bulmash 1984, hemispherical TNT surface burst, in Swisdak's 1994 simplified fits)

struct Blast {
    float Z = 0;                  // scaled distance R / W^(1/3), m/kg^(1/3)
    float incidentPressure = 0;   // side-on peak overpressure, Pa
    float reflectedPressure = 0;  // normally reflected peak, Pa
    float incidentImpulse = 0;    // side-on specific impulse, Pa s
    float reflectedImpulse = 0;   // normally reflected specific impulse, Pa s
    float arrival = 0;            // s
    bool clamped = false;         // Z outside the fits' range: evaluated at the nearest end
};
Blast kingeryBulmash(float W, float R);                  // W kg TNT, R m
Blast kingeryBulmashImperial(float W, float R);          // the same from Swisdak's English-unit fits (a cross-check)
// the impulse (N s) a surface element of this area receives from W kg TNT on the ground at charge, along the blast's
// travel; facing: cos of the angle between the element's outward normal and the way back to the charge (1 face-on,
// <= 0 turned away: incident only). Between side-on and face-on: i_s + (i_r - i_s) cos^2 (a simplification).
float3 blastImpulse(float W, float3 charge, float3 p, float area, float facing);
// the bomb's slider: 0 = 50 g, 0.5 = an M67 hand grenade's 180 g Composition B (~0.20 kg TNT), 1 = 2 kg TNT; log scale
float bombCharge(float u);
constexpr float kGrenadeTNT = 0.180f * 1.11f;   // M67 filler x Comp B's pressure equivalence (see weapons.cpp)

// ---- sword and cube

float swordSpeedCap();            // m/s at the cutting point: a skilled cut
float swordEffectiveMass();       // kg moving with the edge
inline float swordEnergy(float speed) { return 0.5f * swordEffectiveMass() * speed * speed; }
float cutCostPerArea(Medium m);   // J/m^2 of cut surface (tissue, bone)
// a cut of this energy along an edge: how deep it goes into tissue (m)
inline float swordDepthInTissue(float energy, float edgeLength) { return energy / (cutCostPerArea(Medium::Tissue) * edgeLength); }
// the cube's slider: 10 kg .. 5 t on a log scale, the notch (0.5) ~224 kg
float cubeMass(float u);

// tests only: break a mechanism on purpose (a test must turn red)
struct Debug {
    bool dropFragmentMomentum = false;   // fragments vanish instead of handing over their momentum
    bool noYaw = false;                  // spitzers never yaw
    float kbCoefficientNudge = 0;        // added to one Kingery-Bulmash coefficient (metric incident pressure, A)
};
Debug& debug();

}   // namespace weapons
