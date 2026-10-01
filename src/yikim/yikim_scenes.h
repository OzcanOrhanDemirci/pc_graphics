// Scene 3 "Yikim": worlds built from code. Test rigs for the solver (free fall, stacks, slope, cantilever) and the
// reinforced-concrete frame: columns, beams and slab panels as boxes; touching boxes are glued automatically.
#pragma once
#include "scene.h"   // Config
#include "yikim/phys.h"

#include <string>
#include <vector>

enum PieceKind : int { PK_COLUMN = 0, PK_BEAM, PK_SLAB, PK_WALL, PK_OTHER, PK_COUNT };

struct Piece {           // an axis-aligned box that becomes one body
    double3 lo, hi;
    int kind;
    int body = -1;
};

struct FrameSpec {
    int baysX = 2, baysZ = 2, storeys = 2;
    double spanX = 4.0, spanZ = 4.0, storeyH = 3.0;
    double colX = 0.30, colZ = 0.30;           // column section; one side must equal the beam width (no overlap)
    double beamW = 0.30, beamD = 0.50, slabT = 0.12;
    double hingeLen = 0.8;                      // column foot segment (plastic hinge zone)
    double beamSeg = 1.0, slabPanel = 1.0;      // target segment lengths
    float densityConcrete = 2500.0f, densitySlab = 4500.0f;   // slab: + finishes and contents (~2 kN/m^2)
    float rhoColumn = 0.010f, rhoBeam = 0.008f, rhoSlab = 0.004f;   // steel ratios
};

// glue every pair of touching pieces (shared face area > minArea) with an interface; the ground (y = 0) too
void glueTouching(PhysWorld& w, std::vector<Piece>& pieces, int mat, const FrameSpec& spec, bool groundBond);
// fibres over a rectangle: centre c, unit axes u, v (in the face), half sizes a, b; steel spread over the perimeter
std::vector<FiberDesc> fibreGrid(float3 c, float3 u, float3 v, float a, float b, float steelArea);

// test rigs
void buildFreeFall(PhysWorld& w, float drop);
void buildTower(PhysWorld& w, int n, float size);
void buildPyramid(PhysWorld& w, int base, float size, float x0);
int buildSlope(PhysWorld& w, float angleDeg);                         // returns the block's body
int buildCantilever(PhysWorld& w, int segments, float segLen, float b, float h, const PhysMaterial& m);   // returns the tip body
void buildPile(PhysWorld& w, int count, unsigned seed);
// the reinforced-concrete frame; returns the pieces (body indices filled in)
std::vector<Piece> buildFrame(PhysWorld& w, const FrameSpec& spec, const PhysMaterial& concrete);
FrameSpec frameSpecFromConfig(const Config& cfg);
PhysMaterial concreteFromConfig(const Config& cfg);
