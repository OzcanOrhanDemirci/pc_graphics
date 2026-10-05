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

// The Milky Way (Ö3's first piece): a face-on model built from measurements. Arms: the log-periodic spirals with kinks
// of Reid et al. 2019 (ApJ 885, 131, Table 2: maser parallaxes of young massive stars), each continued beyond its measured
// azimuths with a 12-degree pitch; the long bar after Wegg, Gerhard & Portail 2015 (half-length 5 kpc, 28 degrees from the
// Sun-centre line); an exponential old disc (scale length 2.6 kpc); dust lanes on the arms' inner edges. Azimuth beta from
// the centre-Sun line, growing with the rotation (clockwise seen from the north galactic pole). Lengths in kpc.
constexpr int kMaxArms = 8;
struct GArm {
    float betaKink = 0, Rkink = 8, tanLo = 0.2f, tanHi = 0.2f;   // radians, kpc, tan of the pitch below / above the kink
    float betaMin = -1, betaMax = 1;                             // the measured azimuths (radians)
    float width = 0.3f, strength = 1.0f;                         // Gaussian sigma at the kink (kpc); its share
    float ext = 0.5f;                                            // how far past the measured azimuths it goes (radians)
};
struct GGalaxy {
    int on = 0;
    float3 c{0, 0, 0}, n{0, 0, 1}, e0{1, 0, 0}, e1{0, 1, 0};   // centre, north pole, towards the Sun's place, the rotation
    float kpc = 1.0f;                                           // camera units per kpc
    float3 disc{0, 0, 0}, bulge{0, 0, 0}, young{0, 0, 0}, nebula{0, 0, 0};   // face-on central brightness of each (k)
    float hDisc = 2.6f, barAngle = 0.4887f, barHalf = 5.0f;
    float dust = 1.6f;                                          // the lanes' optical depth
    float fade = 1.0f;
    int nArms = 0;
    GArm arms[kMaxArms];
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
    int on = 0;                 // its plane is drawn (below ~3 km; above, the Earth's sphere is, with these axes)
    int frame = 0;              // the axes and the beach point are valid (the local maps are read in them)
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

// the maps (Ö1): texture objects and where they lie. Local maps are squares centred on the beach point, in its east and
// north metres; the world maps are equirectangular (longitude -180..180 left to right, latitude 90..-90 top to bottom)
struct GMaps {
    unsigned long long world = 0;    // Blue Marble NG, October: the Earth without clouds (linear x worldGain = albedo)
    unsigned long long zonal = 0;    // its mean along every latitude (the Earth spinning many times in the shutter)
    unsigned long long region = 0;   // the region around home, 500 m (Blue Marble's tile)
    unsigned long long bay = 0;      // the home bay, 10 m (Sentinel-2; alpha: where it has data)
    unsigned long long moon = 0;     // the Moon (LRO)
    unsigned long long cloudWorld = 0, cloudRegion = 0;   // today's clouds (rgb the satellite's colour, a the cloud)
    float cloudGain = 1.0f, cloudFade = 1.0f;   // the clouds' share (they thin out as the camera comes down to them)
    unsigned long long shore = 0;    // the sea's signed distance near the beach point (m, + over the water; float)
    float shoreHalf = 2048.0f, shoreCentre = 0.0f;   // its half width (m); its value at the beach point
    int shoreSize = 0;                               // texels across (read with point samples, interpolated in float)
    float worldGain = 1.0f, bayGain = 0.25f, moonGain = 0.6f;
    float regionHalf = 1024e3f, regionTexel = 500.0f;
    float bayHalf = 40960.0f, bayTexel = 10.0f;
    float worldTexels = 21600.0f, moonTexels = 8192.0f;   // across 360 degrees
};

// The Earth's air (Ö1): single scattering, Rayleigh + aerosol (Mie) + ozone, exponential layers, the sunlight's way
// through it by Chapman's function (Schüler's approximation). Deterministic: fixed steps along each ray. In camera units
// like everything else; metres where the densities are read.
struct GAtmo {
    int on = 0;
    float3 c{0, 0, 0};          // the Earth's centre (camera space)
    float r = 1.0f;             // its radius (camera units)
    float h = 1.0f;             // the camera's altitude above it (camera units, from doubles)
    float top = 0.0f;           // the air's top above the ground (camera units)
    float3 sun{0, 0, 1};        // towards the Sun (camera space, unit)
    float3 E{0, 0, 0};          // the sunlight's irradiance at the top of the air, display-referred (k x W/m^2)
    float metres = 1.0f;        // metres per camera unit
    float3 betaR{5.802e-6f, 13.558e-6f, 33.1e-6f};   // Rayleigh scattering at sea level (1/m)
    float betaM = 2.0e-5f, absorbM = 0.2e-5f;        // aerosol scattering and absorption at sea level (1/m): a clear
                                                     // day (optical depth ~0.03; MODIS of 5 October: western Anatolia clear)
    float3 betaO{0.650e-6f, 1.881e-6f, 0.085e-6f};   // ozone absorption at its peak (1/m)
    float HR = 8000.0f, HM = 1200.0f, g = 0.76f;
    float fade = 1.0f;          // its share (it comes in with the scale)
};

struct OlcekView {
    int width = 0, height = 0;
    float metres = 1.0f;                        // D: metres per camera-space unit (the maps' footprints)
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
    GMaps maps;
    GAtmo atmo;
    GGalaxy galaxy;
};

}   // namespace olcek
