# Demo

*Turkish: [GOSTERI.md](GOSTERI.md)*

## Opening
`C:\dev\pc_graphics\build\pc_graphics.exe` — double-click it. The **gallery** opens full screen, borderless, on the
primary monitor: the three scenes on the left, the selected one's picture and description on the right.

- **↑ / ↓** (or **1-3**, or the mouse) to choose, **Enter** (or the **Open scene** button) to open.
- **A** (or the **Details** button): how the scene was made, over its picture, under technical headings; **Esc** closes it.
- **L** (or **TR | EN** at the top right): the application's language, Turkish ↔ English; the choice is remembered.
  `--lang en` on the command line does the same.
- A scene being opened gets ready behind a loading screen for a few seconds (scene 1's programs take longer to compile
  the first time).
- Everywhere: **Esc** back (help → scene → gallery → quit) · **F1** the scene's keys · **H** hide the interface ·
  **F12** screenshot (`captures/`) · **F5** reload the settings and programs (scenes 1 and 2).
- The interface over a scene: its name and state at the bottom left (and the controls used with the mouse, where there
  are any: in Collapse the building, the ball's power, the magnitude, the length slider, start / stop; a progress bar),
  speed and timings at the top right, key hints at the bottom right.

## 01 · Golden Hour — glass and stone, sunlight split into its colours
A low sun coming into a dark room through 5 slits, 14 crystal prisms hanging in its beams, spectra scattered over the
concrete. It starts **still**: the picture sharpens as samples accumulate (the bar at the bottom left runs to 8,192
samples; after that the denoiser steps back and the raw picture remains).

| Key | What it does |
|---|---|
| **Space** | still ↔ **live** (live: the water ripples, bubbles and dust drift, ~22 fps) |
| **C** | next camera (rainbows, prism still life, low wide, middle, corner, pool, plan) |
| **↑ / ↓** | exposure ±¼ stop · **← / →** focus distance · **R** start accumulating over |
| **P / V / N** | photons (caustics) / haze / direct light sampling on and off: see what does what |
| **D** | denoiser on and off · **G** sun patch connection |
| **1-5** | view: beauty, albedo, normal, caustics only, cost heat map |

**Try:** press **P**: the rainbows, the prism's spectrum and the water patterns disappear (they come from a separate
photon pass). Turn **D** off and wait a minute: without the denoiser it converges to the same picture. Go live with
**Space**: it is drawn at 720p and the AI scales it to 1440p; the difference shows in the prism. **F12** writes a linear
EXR next to the PNG. Settings in `scenes/altinsaat.cfg` (F5 applies them at once): sun, haze, white balance, exposure,
cameras, glow.

## 02 · Lighthouse — a beam turning in the fog, night and forest
A lighthouse on a foggy night. It opens at view A, "Window".

| Key | What it does |
|---|---|
| **Space** | walk from A to B through the forest (778 m, ~70 s; press it on the way to turn back; progress at the bottom left) |
| **B** | jump to the other view · **P** pause time · **← / →** time ∓1 s · **↑ / ↓** exposure |
| **1-5** | view: final, surface only, volume only, depth, normal |
| **Tab** | player mode (a curiosity, the world was not built for it): 1st Tab on your own island at A, 2nd Tab at the foot of the lighthouse, 3rd Tab back to the scene |
| as the player | **WASD** walk · **mouse** look · **Shift** run · **Space** jump; you bump into trees, the tower and the hut, and swim in deep water |

Settings in `scenes/fener.cfg` (F5 applies them at once).

## 03 · Collapse — concrete buildings, earthquakes and fracture
**Building** at the top of the panel: **Apartments** (5 storeys, shops on the ground floor), **Tower** (30 storeys, 97 m,
concrete core, glass lobby), **House** (one storey, gable roof, chimney). Choosing one rebuilds it and turns the camera
to it. On opening, the building settles under its own weight for two seconds, unseen. The point is the physics.

| Key | What it does |
|---|---|
| **3-9** | the earthquake's magnitude (also clickable on the panel at the bottom left) |
| **← / →** | length: shorter / longer. A slider on the panel; its notch is the magnitude's own length (the default), the far right **∞**: until you stop it |
| **Space** | start / stop the earthquake (the panel's button too); stopped, the ground calms down over 1.5 s |
| **left click** | a **wrecking ball** (cast steel) comes at what you clicked, at most one a second. The physics decides what breaks and topples |
| **↑ / ↓** | the ball's power (the **Ball** slider on the panel): the left end light and slow (0.5 t, it does not move a column), the notch balanced (4.2 t), the right end a **meteor** (5 m across, 520 t, 396 km/h: one brings the tower down) |
| **right click + mouse** | look · **WASD** fly · **E / Q** up / down · **Shift** faster · **wheel** zoom |
| **T** | slow motion (×1, ×0.25, ×0.1) · **P** pause time · **R** rebuild the building |
| **B** | show / hide the reinforcing bars · **G** denoising (fast filter → AI → raw) |

| Magnitude | Apartments | Tower | House |
|---|---|---|---|
| 3-5 | it sways and cracks; nothing falls | it sways | it sways |
| 6 | balcony parapets and some ground-storey infill fall | hairline cracks | two cracks |
| 7 | heavy damage: parapets and the ground storey's infill fall, the frame stands | the lobby storey fails, the tower topples whole | six cracks |
| 8 | soft-storey collapse: the ground storey goes, the block leans over and comes down | (not measured) | it collapses |
| 9 | a longer, harder shaking; in the end the block comes down entirely | it collapses | it collapses |

**Try:** the tower at 6 and at 7 (at 6 it sways, at 7 it topples; watch it fall in slow motion with **T**); a ball at
the house's roof; 6 and 8 one after the other on the apartments (rebuilding with **R**); 7 with **∞** to watch the damage
build up over a long shaking; balls at the ground-storey columns at different powers (measured: the balanced ball breaks
a corner or an edge column, the lightest does not move it, the meteor brings down the apartments or the tower in one throw); a collapse in slow
motion with **T**, then fly up to the broken column ends and look at the bars. Settings in `scenes/yikim.cfg` (buildings,
materials, solver, camera, sun).
