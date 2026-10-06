// Scene 9 "Ayna": the maze on the lattice (which windows hold a mirror, which are passages) and the sphere's
// collisions with it. Host only; the kernels read the same tables from the card.
#pragma once
#include "scenes/ayna/ayna_geom.h"

#include <string>
#include <vector>

namespace ayna {

struct Maze {
    Lattice g;
    int radius = 3;                       // the hexagon's radius in edges, round the vertex V(0,0)
    std::vector<unsigned char> edges;     // edgeCount: kEdgeOut / kEdgeOpen / kEdgeMirror
    std::vector<unsigned char> cells;     // cellCount: 1 inside the maze
    std::vector<unsigned char> columns;   // vertexCount: 1 where a column stands

    void build(const Lattice& lattice, int hexRadius, unsigned seed, int loops, int layout);   // layout 1: two long corridors
    int edge(int fam, int i, int j) const {
        const int k = edgeIndex(g, fam, i, j);
        return k < 0 ? kEdgeOut : edges[k];
    }
    bool inside(int i, int j, int up) const {
        const int k = cellIndex(g, i, j, up);
        return k >= 0 && cells[k] != 0;
    }
    bool insidePoint(float x, float z) const {
        int i, j, up;
        locate(g, x, z, i, j, up);
        return inside(i, j, up);
    }
    // the centre of a cell (its incentre)
    float2 cellCentre(int i, int j, int up) const;
    // a sphere of radius r at p pushed out of the walls, columns, floor and ceiling; true if it touched any
    bool collide(float3& p, float r) const;
    // the window's signed distance in the slab's plane (negative inside)
    float windowDistance(float du, float y) const;
    // a picture of the plan, for looking at the maze (PPM)
    bool savePlan(const std::string& path, int pixelsPerMetre) const;
    int mirrorCount() const;
    int openCount() const;
};

}   // namespace ayna
