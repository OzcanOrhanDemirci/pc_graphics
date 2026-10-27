// Scene 11 "Orman": the ground (a height field with a stream's bed in it) and where every copy goes. Things are where
// they are for a reason (docs/orman/PLAN.md §3.4): ferns crowd the moist banks, broad leaves gather in patches, stones
// lie in the bed, a fallen trunk bridges the stream, the tall ones close the sky at the edges. Fixed seed: the same
// forest every time.
#pragma once
#include "core/config.h"
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_renderer.h"

#include <map>
#include <string>
#include <vector>

class OrmanWorld {
public:
    float size = 44.0f;       // the detailed ground's side (m), centred on the origin
    float walk = 30.0f;       // the side of the part one walks in
    float res = 0.05f;        // the ground's vertex spacing (m)
    float outer = 120.0f;     // the valley around it: a coarser ground rising into hills, so its end never shows (m)
    OProtoCPU ground;
    OProtoCPU water;          // the stream's surface (a ribbon along its line)
    OProtoCPU litter;         // the fallen leaves over the patch: one mesh, each leaf lying on the ground's own triangles
    struct LitterSet {
        int material;
        std::vector<float4> leaves;       // each leaf's place in the atlas (u0, v0, u1, v1)
        float share, length, curl;        // its share of the litter, a leaf's length (m), how far its edges curl up
    };
    std::vector<LitterSet> litterSets;
    int litterProto = -1;     // its prototype once uploaded (-1: none)
    std::vector<OPlace> places;
    struct Solid {
        float x, z, r, top;               // a disc the body does not walk into, up to its top (m)
    };
    std::vector<Solid> solids;
    struct Clearing {
        float x, z, r;                    // kept open of shrubs and saplings (m): the views' places
    };
    std::vector<Clearing> clearings;
    std::map<std::string, int> counts;   // copies per asset
    float3 lo{0, 0, 0}, hi{0, 0, 0};    // everything's bounds

    void configure(const Config& cfg);
    float height(float x, float z) const;
    float3 normal(float x, float z) const;
    // a body of radius r at (x, z) pushed out of the solids it overlaps (whose top is over its knees: feet + 0.45 m)
    void collide(float& x, float& z, float feet, float r) const;
    float streamDistance(float x, float z) const;   // to the stream's middle line (m)
    float streamCentre(float x) const;              // its z at x
    float waterLevel(float x) const;                // the stream's surface at x (smooth down the stream)
    // the ground (prototype `groundProto` once uploaded after the assets' own), then the water and the litter when there
    // are any, and the copies
    void build(OrmanAssets& assets, int groundMaterial, int groundProto, int waterMaterial);

private:
    float tile_ = 3.0f;
    unsigned seed_ = 11;
    float streamDepth_ = 0.55f, streamWidth_ = 1.1f, waterDepth_ = 0.16f;
    float outerRes_ = 0.25f;                       // the valley's vertex spacing (m)
    float hill_ = 9.0f, hillRun_ = 28.0f;          // the valley's sides: how high, over how far past the patch (m)
    float bend_ = 0.025f;                          // past the patch the stream bends away (1/m)
    int ringTrees_ = 20;                           // big trees around the patch, none between it and the sun
    int youngTrees_ = 10;                          // young ones inside it: the middle storey
    float dense_[5] = {1, 1, 1, 1, 1};             // the understory's layers: floor, shrubs, saplings, debris, litter
    float sunAzimuth_ = 15.0f;                     // degrees, +x towards +z (the gap the sun comes through)
};
