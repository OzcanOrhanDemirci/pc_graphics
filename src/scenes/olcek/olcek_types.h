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
    float gas = 0.3f;                                            // its light's share in nebulae (the minor arms: mostly gas)
};
// a spiral galaxy: the Milky Way (its model from measurements) and, drawn by the same code at their own size, M31 and
// M33 (lengths in "model kpc": real kpc / size, so that one set of shapes serves the three)
constexpr int kMaxGalaxies = 3;
struct GGalaxy {
    int on = 0;
    float size = 1.0f;                                          // real kpc per model kpc
    float barW = 0.6f;                                          // the bar's share of the bulge's light (the rest a round core)
    float3 c{0, 0, 0}, n{0, 0, 1}, e0{1, 0, 0}, e1{0, 1, 0};   // centre, north pole, towards the Sun's place, the rotation
    float kpc = 1.0f;                                           // camera units per kpc
    float3 disc{0, 0, 0}, bulge{0, 0, 0}, young{0, 0, 0}, nebula{0, 0, 0};   // face-on central brightness of each (k)
    float hDisc = 2.6f, barAngle = 0.4887f, barHalf = 5.0f;
    float dust = 1.6f;                                          // the lanes' optical depth
    float fade = 1.0f;
    int nArms = 0;
    GArm arms[kMaxArms];
    float T = 0.0f;   // the second option's eye: its light's colour temperature (shifted as a blackbody's); 0 the first's
    // the clusters' and nebulae's size (their light kept): the first option sees them from afar (1); the eye can stop a
    // few thousand light years from them, where the far view's blobs would be degrees wide
    float blobScale = 1.0f;
};

// The cosmic web beyond the survey (Ö3): a statistical sample (Zel'dovich, olcek_veri.py ag) filling the observable
// universe around us, its light the mean luminosity density of galaxies times the field, dimmed by the cosmological
// (1 + z)^-4 with z from the comoving distance (from us, the observer this universe is centred on). Two lookups at
// different scales and orientations hide the box's period. Within the survey's reach (~200 Mpc) the points are the light.
struct GWeb {
    int on = 0;
    unsigned long long field = 0;
    float3 c{0, 0, 0};          // us (camera space)
    float R = 1.0f;             // the observable universe's radius (camera units)
    float mpc = 1.0f;           // camera units per Mpc
    float boxA = 600.0f, boxB = 1530.0f;   // Mpc
    float3 bx{1, 0, 0}, by{0, 1, 0}, bz{0, 0, 1};   // the second lookup's axes (camera space)
    float3 j0{0, 0, 0};         // the mean emission per camera unit of path (display-referred)
    float cell = 2.34f;         // Mpc
    float inner = 200.0f;       // Mpc: the survey's reach (the web fades in beyond it)
    float dim[33];              // (1 + z)^-4 at r = i/32 of R
};

// The inner world's density map (Ö4-Ö5): what an instrument of that scale measures, drawn as a crystallographer draws it:
// soft shells where the density crosses levels a factor of e apart, glowing where the view grazes them. Bodies: atoms
// (electron density from Clementi-Raimondi orbital exponents), a nucleus (Woods-Saxon charge density), a proton and its
// quarks (Gaussians), a flux tube (a Y of segments). Positions in camera space; the density in the body's own unit.
constexpr int kMaxDensity = 24;
struct GDensityBody {
    float3 c{0, 0, 0};
    int type = 0;              // 0 Si atom, 1 O atom, 2 nucleus (Woods-Saxon), 3 Gaussian, 4 tube from c to c2
    float3 c2{0, 0, 0};
    float scale = 1.0f;        // metres per camera unit / the body's length unit (a0, fm): camera units -> its unit
    float r = 1.0f, a = 0.5f;  // Woods-Saxon radius and diffuseness; Gaussian / tube sigma (its unit)
    float amp = 1.0f;          // its peak, in particles per a0^3 (the one unit of the map, whatever the body's own)
};
struct GDensity {
    int on = 0, n = 0;
    int lattice = 0;           // the silica net around the home atom, drawn from the AFM's lattice (the ground's frame)
    float weight = 1.0f;       // its share (comes in and goes out over a decade)
    float lo = -2.0f, hi = 3.0f;   // log10 of the density the fill's dark and light ends stand for (this frame's range)
    float relief = 0.01f;      // the hill-shading's height: screen widths per decade
    float shells = 0.0f;       // the atoms' shells shown (0..1): where -Laplacian(rho) > 0 the fill is lighter (Bader)
    float pxA0 = 1.0f;         // a pixel at the focus, in a0
    GDensityBody b[kMaxDensity];
};

// The density, in particles per a0^3: the electrons of an atom (Slater-type orbitals with Clementi-Raimondi exponents,
// rho = sum N/(4 pi) (2 zeta)^(2n+1)/(2n)! r^(2n-2) e^(-2 zeta r), r in a0),
// a nucleus's nucleons, partons. The same code gives the host its exposure and the GPU its map.
HD float olP5(float t) { return t * t * t * t * t; }
HD float olP7(float t) { return t * t * t * t * t * t * t; }
// one Slater shell's density c r^m e^(-beta r) (m = 0, 2, 4) added to rho, and its Laplacian (1/r^2) d/dr (r^2 d/dr) to
// lap: c e^(-beta r) (m(m+1) r^(m-2) - 2 beta (m+1) r^(m-1) + beta^2 r^m); r kept above 1e-4 a0 in it (the cusp at the
// nucleus, far below any pixel there). The atoms' shells show in its sign (Bader): -lap > 0 where charge gathers.
HD void olShell(float c, int m, float beta, float r, float& rho, float& lap) {
    const float e = c * expf(-beta * r);
    const float rr = fmaxf(r, 1e-4f);
    const float rm = m == 0 ? 1.0f : (m == 2 ? r * r : r * r * r * r);
    const float rm2 = m == 0 ? 1.0f / (rr * rr) : (m == 2 ? 1.0f : rr * rr);   // r^(m-2)
    rho += e * rm;
    lap += e * (float(m * (m + 1)) * rm2 - 2.0f * beta * float(m + 1) * rm2 * rr + beta * beta * rm);
}
HD float olAtomDensityLap(int type, float r, float& lap) {
    // Si: 1s2 (13.5745), 2s2 (4.9008), 2p6 (4.9725), 3s2 (1.6344), 3p2 (1.4284); O: 1s2 (7.6579), 2s2 (2.2458), 2p4 (2.2266)
    float rho = 0.0f, l = 0.0f;
    if (type == 0) {
        const float z1 = 13.5745f, z2 = 4.9008f, z2p = 4.9725f, z3 = 1.6344f, z3p = 1.4284f;
        olShell(2.0f * 4.0f * z1 * z1 * z1, 0, 2.0f * z1, r, rho, l);
        olShell(2.0f * olP5(2.0f * z2) / 24.0f, 2, 2.0f * z2, r, rho, l);
        olShell(6.0f * olP5(2.0f * z2p) / 24.0f, 2, 2.0f * z2p, r, rho, l);
        olShell(2.0f * olP7(2.0f * z3) / 720.0f, 4, 2.0f * z3, r, rho, l);
        olShell(2.0f * olP7(2.0f * z3p) / 720.0f, 4, 2.0f * z3p, r, rho, l);
    } else {
        const float z1 = 7.6579f, z2 = 2.2458f, z2p = 2.2266f;
        olShell(2.0f * 4.0f * z1 * z1 * z1, 0, 2.0f * z1, r, rho, l);
        olShell(2.0f * olP5(2.0f * z2) / 24.0f, 2, 2.0f * z2, r, rho, l);
        olShell(4.0f * olP5(2.0f * z2p) / 24.0f, 2, 2.0f * z2p, r, rho, l);
    }
    lap += l * 0.0795775f;
    return rho * 0.0795775f;   // 1 / 4 pi
}
HD float olAtomDensity(int type, float r) {
    float lap = 0.0f;
    return olAtomDensityLap(type, r, lap);
}
// lap: the atoms' Laplacian (per a0^5); the other bodies add none (the shells are drawn only where atoms are the view)
HD float olBodyDensity(const GDensityBody& b, float3 p, float& lap) {
    const float3 d = (p - b.c) * b.scale;
    const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
    if (b.type <= 1) return olAtomDensityLap(b.type, sqrtf(dd), lap);
    if (b.type == 2) return b.amp / (1.0f + expf((sqrtf(dd) - b.r) / b.a));
    if (b.type == 3) return b.amp * expf(-0.5f * dd / (b.a * b.a));
    // a tube: the distance to the segment
    const float3 e = (b.c2 - b.c) * b.scale;
    const float ee = e.x * e.x + e.y * e.y + e.z * e.z;
    float t = (d.x * e.x + d.y * e.y + d.z * e.z) / fmaxf(ee, 1e-12f);
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    const float3 q = d - e * t;
    return b.amp * expf(-0.5f * (q.x * q.x + q.y * q.y + q.z * q.z) / (b.a * b.a));
}
HD float olDensity(const GDensity& D, float3 p, float& lap) {
    float rho = 1e-30f;
    for (int k = 0; k < D.n; ++k) rho += olBodyDensity(D.b[k], p, lap);
    return rho;
}
HD float olDensity(const GDensity& D, float3 p) {
    float lap = 0.0f;
    return olDensity(D, p, lap);
}

// Below the light (Ö4-Ö5, Özcan's revision: art and representation, representational colour): particles drawn on the
// screen in the order of their depth, far first, binned into 16-pixel tiles on the host. Kinds: a glow (a Gaussian: the
// exact projection of a 3D Gaussian cloud), a soft lit sphere (it covers what is behind it), a ring (a circle in 3D: an
// ellipse on the screen, its colour turning around it), a segment (a glowing capsule, its colour along it). Screen
// coordinates in pixels; colours display-referred like the rest.
enum { kPartGlow = 0, kPartSphere = 1, kPartRing = 2, kPartSegment = 3 };
constexpr int kTile = 16;
struct GParticle {
    float2 c{0, 0};            // centre (pixels; a segment's first end)
    float r = 1.0f;            // glow: sigma; sphere: radius; ring: radius along its major axis
    int kind = kPartGlow;
    float3 col{1, 1, 1};       // glow / ring peak, sphere's lit colour, segment's colour at its first end
    float alpha = 1.0f;        // sphere: coverage; the others: a factor on their light
    float3 col2{1, 1, 1};      // ring: its other side; segment: its far end; sphere: its rim's light
    float w = 1.0f;            // ring and segment: their width (sigma, pixels)
    float2 ax{1, 0};           // ring: its major axis (unit); segment: to its far end (pixels)
    float q = 1.0f;            // ring: minor / major
    float phase = 0.0f;        // ring: the angle its colour is col at
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
    float steps = 0.0f;         // the crystal's terraces, as an electron microscope sees them (0..1)
    float atoms = 0.0f;         // the atoms of its top layer (a height field, AFM-like) (0..1)
    float3 sunDir{0, 0, 1};     // towards the Sun (unit), and its irradiance / pi (display-referred)
    float sunLit = 0.0f;
    float lattice = 4.9e-10f;   // the lattice spacing (m)
    float atomSigma = 6e-11f;   // an atom's bump (m)
    float jitter = 0.0f;        // thermal vibration (m), after the averaging
    float vibPhase = 0.0f;      // the vibration's clock (radians)
};

// The desert and the bottom (Ö5): the time average of the field around the home quark, drawn as the density map is.
// Its energy density u = A(theta, d) / (d^2 + sigma^2)^2, d the distance to the quark (screen widths): A the strong,
// weak and hypercharge couplings at that distance (one loop, the Standard Model's: they run towards each other), each in
// a lobe of the slice (a picture's device: the rings are round where the three are equal). At the bottom the distances
// are uncertain by ~ the Planck length (Wheeler's foam: octaves of displacement, the slow ones moving, the fast ones
// averaged away by the shutter), and the quark is a closed string: the distance is to the loop, its modes as the
// shutter sees them (kept sharp, or widened by their averaged swing).
constexpr int kLoopPoints = 64;
struct GField {
    int on = 0;
    float weight = 0.0f;
    float logW = 0.0f;              // log10 of the screen's width in metres (the couplings run with metres)
    float sigma = 0.0012f;          // the point's smearing (screen widths): the probe's resolution, a few pixels
    float lo = 0.0f, hi = 10.0f;    // log10 u at the fill's dark and light ends
    float relief = 0.01f;           // the hill-shading's height: screen widths per decade
    float fillGain = 1.0f;          // the fill's share (the lines stay): low where the particles are the picture
    int octaves = 0;                // the foam
    int index[kMaxOctaves];         // the octave's own number (its wavelength 2^-index m)
    float freq[kMaxOctaves];        // cycles per screen width
    float amp[kMaxOctaves];         // displacement (screen widths), after the shutter's averaging
    float phase[kMaxOctaves];       // its clock (wraps at 256)
    int loop = 0;                   // the string drawn as a loop (else a point: it is far below the pixel)
    float loopBlur = 0.0f;          // the width its averaged swing adds (screen widths; its rms)
    float2 pts[kLoopPoints];        // the loop as the shutter keeps it sharp (screen widths from the focus)
    float line = 0.0f;              // the string itself, drawn: its brightness (0 while it is a point in the quark's smear)
    float lineW = 0.0006f;          // its own width (screen widths)
    // the foam's cells at the Planck length: each its own shift of the distances, the walls where they meet
    float cellFreq = 0.0f;          // cells per screen width
    float cellAmp = 0.0f;           // a cell's shift (screen widths), after the shutter
    float cellPhase = 0.0f;         // their clock (wraps at 256)
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

// The second option's eye (PLAN §3.1): the sky's diffuse light by the pixel. Each pixel's direction is taken back to
// the rest frame (aberration, tan(theta/2) = e^phi tan(theta'/2)) and its light shifted by D there: the Milky Way's
// panorama (ESO; near the Sun: the view from here) and the cosmic microwave background (with the Sun's dipole). A
// blackbody of T is seen as one of D T, its surface brightness D^4 times; the panorama keeps its own colour, turned by
// the blackbody's at its colour temperature. The stars come as splats, built on the host the same way.
struct GSky {
    int on = 0;
    int energy = 0;                    // the energy eye: every band's light, the band in false colour (energyColour)
    double mx = 0.0, my = 0.0, mz = 1.0;            // the motion in camera space (unit)
    double emp = 1.0, gamma = 1.0, beta = 0.0, eps = 1.0;   // e^-phi and the kinematics (eps = 1 - beta, exact)
    double tanX = 0.36, tanY = 0.2;    // the field (in doubles: the binoculars reach 1e-11 rad)
    float3 gal0{1, 0, 0}, gal1{0, 1, 0}, gal2{0, 0, 1};   // camera space to galactic: x to the centre, y to l 90, z north
    const float4* pano = nullptr;      // the Milky Way: linear RGB (W/m^2/sr, the Sun's spectrum's watts) and T / 10^4 K;
    int pw = 0, ph = 0;                // its smaller levels after it, each half the last (T weighted by the light)
    int nLod = 1;
    float pixAngle = 1e-3f;            // a pixel's angle at the middle of the field (radians): which level a pixel reads
    float panoWeight = 1.0f;           // 1 near the Sun, fading away from it
    const float4* bb = nullptr;        // blackbody table over log10 T: RGB (luminance 1), log10 visible share vs the Sun's
    int bbN = 0;
    float bbLogT0 = 1.0f, bbStep = 0.01f;
    float cmbT0 = 2.72548f;            // K (Fixsen 2009)
    float cmbDipole = 0.0012336f;      // the Sun's motion against it: 3.362 mK / T0 (Planck 2018)
    float3 cmbDir{0, 0, 1};            // ... towards l 264.02, b 48.25 (galactic)
    float k = 1.0f;                    // the exposure: display per W/m^2/sr (visible; bolometric in the energy eye)
    float lk = 0.0f;                   // ... its log10 (the kernel sums the light in log10)
    // the eye's own scatter (veiling glare, Stiles-Holladay: L = 10 E / theta^2, theta in degrees, ~13% of the light
    // between 0.1 and 90 degrees): around the brightest sources, their light at the eye (display units x sr) and colour
    int nGlare = 0;
    float3 glareDir[8];               // camera space (unit)
    float3 glareE[8];                 // E x k, coloured
};

// the energy eye's colours (PLAN §3.1, Özcan 2026-10-06): the band where a blackbody of T peaks (Wien), from radio to gamma
// rays, at luminance 1: deep red for radio and microwaves, red-orange infrared, the visible band in its own colour (vis,
// the blackbody's), blue ultraviolet, violet X-rays, white-violet gamma rays
HD float3 energyColour(float logT, float3 vis) {
    const float L[9] = {0.0f, 1.5f, 2.6f, 3.35f, 4.05f, 5.0f, 6.5f, 8.5f, 12.0f};
    const float3 C[9] = {make_float3(0.42f, 0.07f, 0.04f), make_float3(0.62f, 0.12f, 0.06f), make_float3(0.95f, 0.38f, 0.12f), vis, vis,
                         make_float3(0.36f, 0.55f, 1.00f), make_float3(0.50f, 0.36f, 1.00f), make_float3(0.78f, 0.55f, 1.00f),
                         make_float3(0.96f, 0.92f, 1.00f)};
    int i = 0;
    while (i < 7 && logT > L[i + 1]) ++i;
    const float t = fminf(fmaxf((logT - L[i]) / (L[i + 1] - L[i]), 0.0f), 1.0f);
    float3 c = C[i] * (1.0f - t) + C[i + 1] * t;
    const float lum = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    return c * (1.0f / fmaxf(lum, 1e-4f));
}

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
    GField field;
    float beachClock = 0.0f;   // the swash, seconds (wraps)
    GMaps maps;
    GAtmo atmo;
    GGalaxy galaxy[kMaxGalaxies];   // 0 the Milky Way, 1 M31, 2 M33
    GWeb web;
    GDensity dens;
    GSky sky;                       // the second option's eye: its diffuse sky (off elsewhere)
};

}   // namespace olcek
