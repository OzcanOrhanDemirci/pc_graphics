// Scene 7 "Ölçek", second option "Hız": the side view's own 2D layer over the rest frame's picture (the map, PLAN §3.2;
// Özcan 2026-10-06: the side view is the rest frame's, the camera follows the rocket).
//   - the interstellar medium: points fixed in the rest frame (their density a representation, their motion real), drawn
//     along the way they make against the rocket while the eye's shutter is open (1/60 s of the passenger's time):
//     specks at rest, streaks at speed, and at the highest speeds steady lines across (the streaks of a row merge)
//   - the leg's way between its two stops, their names and distances (at the screen's edge when beyond it)
//   - the rocket: a photon rocket with its passenger, its length along the motion as the rest frame measures it, gamma
//     times shorter; its drive's light (brighter as the acceleration grows), and turned round to brake (the light must
//     point ahead to slow it)
//   - beside it the speed, and the way it makes in one of the passenger's seconds
// Drawn with the interface kit (crisp at any scale), as part of the picture: the interface's H does not hide it.
#pragma once
#include "scenes/olcek/olcek_hiz.h"
#include "ui/ui.h"

namespace olcek::hiz {

struct SideLayer {
    const Ship* ship = nullptr;
    const Route* route = nullptr;
    V3 right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, 1};   // the camera's axes (rest frame): right the motion, fwd into the screen
    V3 focus;                    // where the camera looks, from the rocket (m)
    double D = 1.0;              // the camera's distance from its focus (m)
    double W = 1.0;              // the screen's width at the rocket (m)
    double tanHalfX = 0.364;     // tan of half the camera's field across
    ui::Rect view;               // where the picture is (window pixels)
    double dt = 0.0;             // the passenger's seconds since the last frame (the rocket's turning round)
};
struct SideState {
    double facing = 1.0;         // the rocket's nose: +1 along the motion (thrust, coast), -1 turned round (braking)
    double turn = 0.0;           // the turn round in progress, 0..1 (its cosine is the drawing's facing)
    bool turned = false;         // where the turn is headed: true towards braking
    double glow = 0.0;           // the drive's light, eased (0 off .. 1 the highest acceleration)
};
void drawSide(const SideLayer& L, SideState& st, float S);

}   // namespace olcek::hiz
