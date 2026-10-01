// Scene 3 "Yikim": the apartment block. A 1990s reinforced-concrete frame (ground-floor shops, four floors of flats)
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

struct Apartment {
    std::vector<Piece> pieces;   // glued (frame, walls, balconies, parapets)
    std::vector<int> loose;      // bodies standing unglued on the roof
    double height = 0;           // roof level
    double width = 0, depth = 0; // footprint along x and z
};

Apartment buildApartment(PhysWorld& w, const ApartmentSpec& s, const PhysMaterial& concrete, const PhysMaterial& brick);
ApartmentSpec apartmentSpecFromConfig(const Config& cfg);
PhysMaterial brickFromConfig(const Config& cfg);
