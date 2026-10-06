// Scene 8's parts: the pieces a control room is assembled from, each built with the kit at its real measures (metres).
// Every part is made in its own frame (where it says) and placed by the kit's Xform.
#pragma once
#include "scenes/denizalti/dz_geom.h"

#include <vector>

namespace dz {

// A pipe along a polyline with bends of radius bendR at the corners (centre-line radius); r the outside radius.
// Bends get the bend's own surface coordinates continuing the run's (features like braid stay continuous).
void pipeRun(Kit& k, const std::vector<float3>& pts, float r, float bendR);

// A bolted flange pair across a pipe of radius r at the frame's origin, pipe along z: two discs of radius rf, thickness
// each tf, n bolts on the bolt circle; hex nuts on both sides with the bolt's threaded end standing proud
void flangePair(Kit& k, float r, float rf, float tf, int n);

// A hex bolt standing on z = 0 along +z: a nut of across-flats s and a threaded stud of radius rs standing len proud
void studAndNut(Kit& k, float s, float rs, float len);

// A handwheel in the xy plane facing +z at the origin: rim (centre radius R, tube r), n spokes, hub radius rh
void handwheel(Kit& k, float R, float r, int n, float rh, uint32_t rimMat, uint32_t hubMat);

// A globe valve on a pipe of radius rp along x through the origin, bonnet and stem up +z, handwheel diameter D on top
void globeValve(Kit& k, float rp, float D, uint32_t wheelMat);

// A round dial gauge facing +z at the origin: case radius rc, depth d, on a short stem from below (-y)
void gauge(Kit& k, float rc, float d);

// The pressure hull's inside: radius r, angles a0..a1 about z (0 = +x, pi/2 = +y), from z0 to z1
void hullWall(Kit& k, float r, float a0, float a1, float z0, float z1);

// A ring frame inside the hull at z: a T-section (web height hw, thickness tw, flange width bf, thickness tf) from angle
// a0 to a1, its outer edge on radius r (welded to the plating)
void ringFrame(Kit& k, float r, float a0, float a1, float z, float hw, float tw, float bf, float tf);

// A tread plate deck in the xz plane at y = 0 (w across x, l along z), thickness t, normal +y
void deckPlate(Kit& k, float w, float l, float t);

}   // namespace dz
