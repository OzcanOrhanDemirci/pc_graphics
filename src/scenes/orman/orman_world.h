// Scene 11 "Orman": the ground (a height field with a stream's bed in it) and where every copy goes. Things are where
// they are for a reason (docs/orman/PLAN.md §3.4): ferns crowd the moist banks, broad leaves gather in patches, stones
// lie in the bed, a fallen trunk bridges the stream, the tall ones close the sky at the edges. Fixed seed: the same
// forest every time.
//
// Around the stream's patch the forest goes on: copies of one tile of it, the patch's size, whose sides meet their
// neighbours' (its ground and its noise wrap, its distances go the shorter way round). The stream is not copied: it
// runs on through the row of cells it crosses, and there a band of each cell is its own (its bed, its banks, its
// stones) while the rest is the tile. Where the tile's tall trees would stand in the sun's way to the patch, that cell's
// copies are placed one by one without them.
#pragma once
#include "core/config.h"
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_renderer.h"

#include <map>
#include <random>
#include <string>
#include <vector>

class OrmanWorld {
public:
    float size = 44.0f;       // the stream's patch's side (m), centred on the origin; the tile's side
    float walk = 30.0f;       // the side of the part the patch was made for (its views, its edge of saplings)
    float res = 0.05f;        // the ground's vertex spacing (m)
    int cells = 4;            // the forest around the patch: this many tiles each way
    std::vector<OProtoCPU> extras;   // the world's own prototypes, after the assets': grounds, the water, the litters
    int groundProto = -1, litterProto = -1;   // the patch's
    struct AoJob {
        int proto;
        float3 at;            // where its vertices stand (the tile's: in the cell its light is baked in)
    };
    std::vector<AoJob> aoJobs;   // the grounds and litters whose occlusion is baked per vertex
    struct LitterSet {
        int material;
        std::vector<float4> leaves;       // each leaf's place in the atlas (u0, v0, u1, v1)
        float share, length, curl;        // its share of the litter, a leaf's length (m), how far its edges curl up
    };
    std::vector<LitterSet> litterSets;
    std::vector<OPlace> places;           // placed in the world: the patch, the stream's row, the cells in the sun's way
    OTile tile;                           // the rest: the tile, copied whole into its cells
    float3 tileLo{0, 0, 0}, tileHi{0, 0, 0};   // the tile's bounds (its middle at the origin)
    float3 tileBakeAt{0, 0, 0};          // the cell its light is baked in (deep in the forest, among its copies)
    struct Solid {
        float x, z, r, top;               // a disc the body does not walk into, up to its top (m)
        bool tall = false;                // a tree's (left out with it where it would stand in the sun's way)
    };
    std::vector<Solid> solids;
    struct Clearing {
        float x, z, r;                    // kept open of shrubs and saplings (m): the views' places
    };
    std::vector<Clearing> clearings;
    std::map<std::string, long long> counts;   // copies per asset, every cell's
    float3 lo{0, 0, 0}, hi{0, 0, 0};    // the patch's bounds (its baked light)

    void configure(const Config& cfg);
    float height(float x, float z) const;
    float3 normal(float x, float z) const;
    // the tile's ground at (x, z) in its own copy (its middle at the origin); the world's is the same in every cell but
    // the stream's band
    float tileHeight(float x, float z) const;
    // a body of radius r at (x, z) pushed out of the solids it overlaps (whose top is over its knees: feet + 0.45 m)
    void collide(float& x, float& z, float feet, float r) const;
    float limit() const;                            // how far from the middle one walks, each way (m)
    float streamDistance(float x, float z) const;   // to the stream's middle line (m)
    float streamCentre(float x) const;              // its z at x
    float waterLevel(float x) const;                // the stream's surface at x (smooth down the stream)
    // the patch's ground (prototype `firstProto`, the first of the extras), the water, the litter, the tile and the
    // stream's row
    void build(OrmanAssets& assets, int groundMaterial, int firstProto, int waterMaterial);

private:
    float tile_ = 3.0f;
    unsigned seed_ = 11;
    float streamDepth_ = 0.55f, streamWidth_ = 1.1f, waterDepth_ = 0.16f;
    int youngTrees_ = 10;                          // young ones inside it: the middle storey
    float dense_[5] = {1, 1, 1, 1, 1};             // the understory's layers: floor, shrubs, saplings, debris, litter
    float sunAzimuth_ = 15.0f, sunElevation_ = 25.0f;   // degrees (+x towards +z; over the horizon)
    float bandLo_ = -10.0f, bandHi_ = 6.0f;        // the stream's band in a cell of its row (z, m; the stream inside it)
    float sunReach_ = 0.0f;                        // how far from the patch a tall one could stand in the sun's way (m)

    using Kinds = std::map<int, std::vector<std::string>>;
    Kinds kinds_;                                  // the assets by what they are (the big ones first)
    std::map<std::string, float> widths_;
    int groundMaterial_ = 0;
    // a stretch of forest grown by the tile's rules: the tile itself (wrap: it meets its own copies) or the stream's
    // band in one cell of its row (stream: its banks and bed, the world's ground)
    struct Area {
        float x0, z0, sx, sz;
        bool wrap, stream;
        unsigned seed;
    };
    void grow(OrmanAssets& A, const Area& a, std::vector<OPlace>& out, std::vector<Solid>& sol, std::vector<unsigned char>& tall, float3& blo,
              float3& bhi);
    // a ground mesh over a rectangle (res apart), its heights from h
    template <class H> OMeshCPU groundMesh(float x0, float z0, int nx, int nz, H h) const;
    // the fallen leaves over a ground mesh (its vertices nx by nz from (x0, z0)): a cell of `cell` holds a leaf by
    // density(x, z) (below 0: something stands there); `piece(z)` picks the mesh a leaf goes into
    template <class D, class P>
    void layLitter(const OMeshCPU& g, float x0, float z0, int nx, int nz, bool wrap, float cell, std::mt19937& lr, D density, P piece,
                   std::vector<std::vector<OMeshCPU>>& meshes) const;
    bool towardSun(float x, float z) const;        // within 35 degrees of the sun's bearing from the patch's middle
};
