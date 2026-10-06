// Scene 9 "Ayna": the light that reaches a surface by way of the mirrors.
//
// Path tracing can not find it: from a wall, the way to a point light through three mirrors has zero measure. But the
// mirrors stand on lattice lines, upright, and the light's way is exact in plan: a beam from the light (or from its
// image in a mirror) through a window is a wedge, cut by the columns and the next windows, and a mirror makes a new
// image (Heckbert & Hanrahan's beam tracing, 1984; the image sources of room acoustics). Every beam fragment in a
// cell is a node: the light's image in that cell's frame and the wedge of directions from it. A surface point lit by
// a node sees the image through the windows the node crossed; its light is I W / d^2, with W the mirrors' measured
// reflectance at the point's own angles, d the unfolded distance. No sampling: no grain.
//
// The plan picture is exact for the columns and the jambs (the windows' straight sides, through the slab's whole
// depth). The arches are checked in 3D where a way rises above the jambs' top (the ceiling, the arch band); the
// horseshoe's bulge above the jambs is not given to the beams (a little light high in the arch is missed).
#pragma once
#include "scenes/ayna/ayna_kernels.h"
#include "scenes/ayna/ayna_maze.h"

#include <vector>

namespace ayna {

struct BeamBuildStats {
    int nodes = 0, cut = 0, maxDepth = 0;
    double ms = 0.0;
};

// the beams of one light; `level` scales its power bound, `eps` is the irradiance under which a beam is dropped
void buildBeams(const Maze& mz, const Optics& opt, const Light& L, int lightIndex, float eps, int maxNodes, std::vector<BeamNode>& out,
                BeamBuildStats* stats);

// all lights' nodes: the per-cell lists sorted by power (strongest first)
void sortIntoCells(const Maze& mz, std::vector<BeamNode>& nodes, std::vector<int>& cellStart, std::vector<int>& cellNodes);
void setBeamBulge(bool on);   // the horseshoe bulge's beams (tools)

}   // namespace ayna
