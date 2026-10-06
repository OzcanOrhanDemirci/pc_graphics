// Scene 8's printed and engraved faces: gauge dials and label plates, drawn on the processor at high resolution into
// one atlas (RGBA8: ink colour and coverage) the shader reads; plates engraved into brass are also displaced by it.
// Parts ask for a decal while the world is built (an id per kind or per text); the atlas is drawn after.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dz {

enum DialKind : int {
    DIAL_DEPTH16 = 0,   // the 16" shallow depth gauge: 0-165 ft, black face
    DIAL_DEPTH_DEEP,    // the 8.5" deep gauge: 0-600 ft, black face
    DIAL_AIR,           // air bank pressure: 0-4000 psi, black face
    DIAL_WHITE,         // hydraulic, trim: 0-3000 psi, white face
    DIAL_SMALL,         // small pressure gauges: 0-300 psi, white face
    DIAL_METER,         // electrical meters: amperes, a 90 degree arc
    DIAL_COMPASS,       // the gyro repeater's card
    DIAL_COUNT
};

enum PlateStyle : int {
    PLATE_BLACK = 0,    // black plate, white engraving (filled)
    PLATE_BRASS,        // polished brass, engraving filled black (and cut into the metal)
    PLATE_STENCIL,      // paint stencilled straight onto the surface (no plate)
    PLATE_CHART,        // a printed sea chart on paper, worked in pencil (the text is unused; drawn at 0.2 mm a pixel)
};

struct DecalSpec {
    int kind = -1;              // a dial kind, or -1: a plate
    std::string text;           // a plate's lines, '\n' between
    int style = PLATE_BLACK;
    float w = 0.0f, h = 0.0f;   // a plate's size (m)
};

struct DecalEntry {
    float u0, v0, u1, v1;       // the atlas rectangle (0..1)
    float halfW, halfH;         // the surface's half size it maps onto (m); a dial: its radius in both
    int mode;                   // 0 dial, 1 plate
    int style;
};

struct Decals;
extern Decals* activeDecals;   // set while a world is built (the parts ask it for their faces)

struct Decals {
    std::vector<DecalSpec> specs;
    std::vector<DecalEntry> entries;
    int width = 4096, height = 0;
    std::vector<uint8_t> rgba;

    int dial(DialKind k);                                        // one per kind
    int plate(const std::string& text, float w, float h, int style = PLATE_BLACK);
    // draws every requested decal (root: the project, for the fonts)
    bool draw(const std::string& root, std::string& err);
};

}   // namespace dz
