// Scene 3 "Yikim": the buildings, all of the same concrete, steel and brick: the apartment block, a tower and a house.
//
// The apartment block: a 1990s reinforced-concrete frame (ground-floor shops, four floors of flats)
// with hollow-brick infill walls and their window and door openings, front balconies, a flat roof with a parapet,
// and water tanks and solar collectors standing loose on the roof. Every wall is a stack of blocks glued to each other
// and to the frame with weak mortar (brick material); the shops' glass front leaves the ground storey soft.
#pragma once
#include "scenes/yikim/yikim_scenes.h"

struct ApartmentSpec {
    FrameSpec frame;
    double wallExt = 0.20, wallInt = 0.10;      // thicknesses
    double blockW = 0.9, blockH = 0.6;          // wall blocks (target size)
    float densityWall = 1100.0f;                // hollow brick with plaster
    double winW = 1.4, winH = 1.4, sill = 0.9;  // windows
    double doorW = 2.0, doorH = 2.2;            // the front openings onto the balconies
    double inDoorW = 0.9, inDoorH = 2.1;        // inner doors
    double balconyDepth = 1.3;
    bool balconies = true;
    float densityBalcony = 3000.0f;              // slab + screed, lighter than a floor with its contents
    float rhoBalcony = 0.012f;                   // a cantilever: heavy top bars at the root
    double parapetBalcony = 1.0, parapetRoof = 0.8;
    int tanks = 3, collectors = 3;
    unsigned paint = 0xd9b99b, frameColor = 0xb8b4ac, tankColor = 0x2a2c30, collectorColor = 0x1f3550;
};

struct Building {   // what a builder made
    std::vector<Piece> pieces;   // glued (frame, walls, balconies, parapets)
    std::vector<int> loose;      // bodies standing unglued on the roof
    double height = 0;           // roof level
    double width = 0, depth = 0; // footprint along x and z
};

Building buildApartment(PhysWorld& w, const ApartmentSpec& s, const PhysMaterial& concrete, const PhysMaterial& brick);
ApartmentSpec apartmentSpecFromConfig(const Config& cfg);
PhysMaterial brickFromConfig(const Config& cfg);

// The tower: a reinforced-concrete frame around a plain-concrete core (the lift shaft in the middle bay, no floor inside
// it), brick infill with large windows on every side, a glass lobby at the front of the ground storey, a parapet. The
// columns are larger than the apartment's (a frame of 30x50 cm columns would crush under twenty storeys); the
// materials are the same.
struct TowerSpec {
    FrameSpec frame;
    double coreT = 0.30, coreSeg = 2.2;          // core walls: thickness, segment length
    float coreRho = 0.0f;                        // their steel ratio: 0 plain concrete (< 0: a column's, as it was)
    double wallExt = 0.20, blockW = 1.5, blockH = 1.0;
    float densityWall = 1100.0f;
    double winW = 2.4, winH = 1.6, sill = 0.8;
    double parapet = 1.0;
    bool lobby = true;                           // the ground storey's front is glass (no wall): a soft storey
    unsigned paint = 0xcfc8bc, frameColor = 0xb8b4ac;
};
Building buildTowerBlock(PhysWorld& w, const TowerSpec& s, const PhysMaterial& concrete, const PhysMaterial& brick);
TowerSpec towerSpecFromConfig(const Config& cfg);

// The house: one storey under its ceiling slab, brick walls with windows and a front door, and a gable roof: sloping
// slabs (tiles on boards: light) meeting at the ridge, resting on concrete eave blocks along the long sides and on the
// triangular brick gable walls, with a chimney. Pieces cut by the roof's planes are glued across them.
struct HouseSpec {
    FrameSpec frame;
    double wallExt = 0.20, wallInt = 0.10, blockW = 0.9, blockH = 0.6;
    float densityWall = 1100.0f;
    double winW = 1.2, winH = 1.2, sill = 0.9, doorW = 1.0, doorH = 2.2;
    double pitchDeg = 30.0, roofT = 0.12;        // roof slope and slab thickness
    double overhang = 0.5, gableOverhang = 0.3;  // past the eaves and past the gable walls
    double roofStrip = 1.6, eaveGap = 0.10;      // strip width along the ridge; the eave blocks' height at the outer edge
    float densityRoof = 1000.0f;
    bool roof = true, ridge = true, chimney = true;   // false: without (diagnosis)
    unsigned paint = 0xe6dac4, frameColor = 0xb8b4ac, roofColor = 0x9b4a32, chimneyColor = 0x8a4b3a;
};
Building buildHouse(PhysWorld& w, const HouseSpec& s, const PhysMaterial& concrete, const PhysMaterial& brick);
HouseSpec houseSpecFromConfig(const Config& cfg);

// The seismic tower: base isolation, after Japanese practice. Glass in tiers, each centred on the one below (tierBays
// square, tierStoreys high), a steel spire on top; the frame stands on a concrete mat that rests on isolators instead of
// being bonded to the ground: rubber, soft across (the tower on them swings with a period of isoPeriod seconds, longer
// than an earthquake's strong shaking, so that little of it reaches the tower), stiff and strong in compression, never
// breaking. A curb round the mat, a moat's width away, is bonded to the ground: it rides the earthquake and shows how far
// the tower glides.
struct SpireSpec {
    FrameSpec frame;                             // the storeys (all tiers), the sections; bays set per tier
    std::vector<int> tierBays = {5, 3, 1}, tierStoreys = {10, 10, 6};
    std::vector<double> spire = {1.6, 6, 1.1, 6, 0.7, 6, 0.4, 5, 0.2, 4};   // (width, height) from the bottom up
    float spireRho = 0.06f, spireDensity = 1200.0f;   // hollow steel, its foot well anchored (heavier, it broke off at 8)
    // its concrete and bars: high-strength, as a new tower's (the others' 16 MPa crushed at the foot of 26 storeys)
    float fc = 40e6f, ft = 3.0e6f, fy = 420e6f, E = 33e9f;
    double matT = 1.0, matEdge = 1.0, matPanel = 5.0;   // the mat: thickness, beyond the base tier, panel size
    double isoPeriod = 3.5, isoLen = 0.5;               // the isolated period (s), the rubber's height (m)
    float isoE = 100e6f;                                // its stiffness in compression (Pa)
    double moat = 0.9, curbT = 0.5, curbH = 0.8;        // the curb: the moat's width, the curb's thickness and height
    double glassW = 5.0, glassT = 0.15;                 // the curtain wall: panel width (a pane a bay), thickness
    float densityGlass = 900.0f;                        // glass and mullions
    unsigned frameColor = 0x4b5057, glassColor = 0x1e2a33, spireColor = 0xc9ced3, matColor = 0x8f8f8b;
};
Building buildSpireTower(PhysWorld& w, const SpireSpec& s, const PhysMaterial& concrete, const PhysMaterial& brick);
SpireSpec spireSpecFromConfig(const Config& cfg);
