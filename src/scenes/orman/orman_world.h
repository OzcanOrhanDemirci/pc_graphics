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
    float size = 44.0f;       // the ground's side (m), centred on the origin
    float walk = 30.0f;       // the side of the part one walks in
    float res = 0.05f;        // the ground's vertex spacing (m)
    OProtoCPU ground;
    OProtoCPU water;          // the stream's surface (a ribbon along its line)
    std::vector<OPlace> places;
    struct Solid {
        float x, z, r, top;               // a disc the body does not walk into, up to its top (m)
    };
    std::vector<Solid> solids;
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
    // the ground (prototype `groundProto` once uploaded after the assets' own) and the copies
    void build(OrmanAssets& assets, int groundMaterial, int groundProto, int waterMaterial);

private:
    float tile_ = 3.0f;
    unsigned seed_ = 11;
    float streamDepth_ = 0.55f, streamWidth_ = 1.1f, waterDepth_ = 0.16f;
};
