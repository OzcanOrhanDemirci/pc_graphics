// Scene 7 "Ölçek": what one frame hands the GPU. Everything is in camera space with the camera at the origin, in units
// of D, the camera's distance to the focus (the point the screen's width W is measured at; D = W / (2 tan(fov/2))): the
// host works in metres and doubles along the tree of anchors (olcek_world.h) and divides by D last, so the numbers the
// GPU sees are near 1 for what is on the screen at every one of the 62 decades. Radiances are display-referred: the
// host has applied the exposure of this scale (PLAN §5: designed, a function of s; never read from the picture).
#pragma once
#include "shared/vec.h"

namespace olcek {

constexpr int kMaxBalls = 48, kMaxBlobs = 96, kMaxDiscs = 8, kMaxOctaves = 12;

// a solid sphere: lit by one light (a planet, a moon), or glowing (the Sun). Its intersection is solved from the
// camera's altitude above it (computed in doubles), stable from a dot on the screen to a sphere 10^12 times larger than
// the screen seen from just above its surface
struct GBall {
    float3 c{0, 0, 0};          // centre
    float r = 0.0f;             // radius
    float h = 0.0f;             // the camera's altitude above the surface: |c| - r, from doubles
    float3 toLight{0, 0, 1};    // from the ball towards its light (unit)
    float3 albedo{0.3f, 0.3f, 0.3f};
    float lit = 0.0f;           // the light's irradiance / pi (display-referred): a white Lambert surface facing it
    float3 emit{0, 0, 0};       // glowing radiance (the Sun), with limb darkening
    float weight = 1.0f;        // its share against its point (a small sphere hands over to a point: flux kept)
    int pattern = 0;            // 0 plain, 1 the Earth (placeholder continents), 2 the Moon (maria)
    float3 ax{1, 0, 0}, ay{0, 1, 0}, az{0, 0, 1};   // its body axes (the pattern turns with it)
    float spinBlur = 0.0f;      // radians: how far it turns while the eye's shutter is open (the averaging of PLAN §4)
    float3 home{0, 0, 1};       // the Earth: the beach in its body axes (the placeholder's coast is drawn there)
};

// a Gaussian cloud of light: the column through its middle is `emit` (a brightness, the same at every distance)
struct GBlob {
    float3 c{0, 0, 0};
    float sigma = 1.0f;
    float3 emit{0, 0, 0};
};

// a thin disc of light (a galaxy): surface brightness I0 exp(-r/h) face-on, arms, a bar
struct GDisc {
    float3 c{0, 0, 0}, n{0, 0, 1}, ax{1, 0, 0}, ay{0, 1, 0};   // centre, normal, axes in its plane
    float h = 1.0f, rmax = 4.0f, thick = 0.02f;                 // scale length, edge, thickness (all in D)
    float3 emit{0, 0, 0};                                        // face-on central surface brightness
    float arms = 0.0f, pitch = 0.22f, phase = 0.0f;              // arm contrast, tan of the pitch angle, the pattern's turn
    float bulge = 0.0f;                                          // the bulge's share (a round glow at the centre)
};

// a point of light: its flux as the sum of its pixels (display-referred radiance x pixels); a Gaussian on the screen
struct GSplat {
    float3 p;                   // position (camera space)
    float3 flux;
    float sigmaPx;              // its spread (the pixel's own ~0.75, more for a resolved disc)
};

// The ground near the beach point: the Earth's surface there, locally a plane (the Earth's ball draws it from afar; this
// draws what needs metres: the shore, the sand, the grains). Coordinates on it are metres from the beach point.
struct GGround {
    int on = 0;
    float fade = 1.0f;          // its share (into the atom it gives way to the clouds over a decade)
    float3 o{0, 0, 0};          // the beach point (camera space)
    float3 up{0, 0, 1}, east{1, 0, 0}, north{0, 1, 0};
    float metres = 1.0f;        // D in metres: camera units -> metres on the ground
    float beach = 0.0f;         // the shore's pattern's share against the Earth ball's (0..1)
    float grains = 0.0f;        // the sand grains' share (0: their mean colour)
    float grainR = 2.5e-4f;     // a grain's radius (m)
    float surface = 0.0f;       // the home grain's top seen as a surface (0..1): its texture, then the atoms
    float atoms = 0.0f;         // the atoms of its top layer (a height field, AFM-like) (0..1)
    float3 sunDir{0, 0, 1};     // towards the Sun (unit), and its irradiance / pi (display-referred)
    float sunLit = 0.0f;
    float lattice = 4.9e-10f;   // the lattice spacing (m)
    float atomSigma = 6e-11f;   // an atom's bump (m)
    float jitter = 0.0f;        // thermal vibration (m), after the averaging
    float vibPhase = 0.0f;      // the vibration's clock (radians)
};

// the self-similar haze of the desert and the foam at the bottom: octaves of noise on the focus plane, each with its
// own clock and its contrast after the averaging
struct GHaze {
    float amount = 0.0f;        // 0 off
    float3 color{1, 1, 1};
    int octaves = 0;
    int index[kMaxOctaves];     // the octave's own number (its wavelength 2^-index m): its noise, at every zoom
    float freq[kMaxOctaves];    // cycles per screen width
    float weight[kMaxOctaves];  // contrast (after the averaging)
    float phase[kMaxOctaves];   // its clock (wraps at 256)
    float foam = 0.0f;          // 0 the desert's soft haze, 1 the Planck foam's cells
};

struct OlcekView {
    int width = 0, height = 0;
    float tanHalfX = 0.364f, tanHalfY = 0.2f;   // tan(fov/2) across and up
    float pixAngle = 0.0f;                      // 2 tanHalfX / width
    float filterRadius = 1.5f;
    float saturation = 1.0f;                    // colour ends below the light's wavelength (PLAN §5)
    float3 background{0, 0, 0};
    int nBalls = 0, nBlobs = 0, nDiscs = 0;
    GBall balls[kMaxBalls];
    GBlob blobs[kMaxBlobs];
    GDisc discs[kMaxDiscs];
    GGround ground;
    GHaze haze;
};

}   // namespace olcek
