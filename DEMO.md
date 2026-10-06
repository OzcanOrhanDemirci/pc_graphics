# Demo

*Turkish: [GOSTERI.md](GOSTERI.md)*

## Opening
`C:\dev\pc_graphics\build\pc_graphics.exe` — double-click it. The **gallery** opens full screen, borderless, on the
primary monitor: the four scenes on the left, the selected one's picture and description on the right.

- **↑ / ↓** (or **1-4**, or the mouse) to choose, **Enter** (or the **Open scene** button) to open.
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
concrete core, glass lobby), **House** (one storey, gable roof, chimney), **Seismic** (26 storeys and a spire, base-isolated, glass: it stands even at 9). Choosing one rebuilds it and turns the camera
to it. On opening, the building settles under its own weight for two seconds, unseen. The point is the physics.

| Key | What it does |
|---|---|
| **3-9** | the earthquake's magnitude (also clickable on the panel at the bottom left) |
| **← / →** | length: shorter / longer. A slider on the panel; its notch is the magnitude's own length (the default), the far right **∞**: until you stop it |
| **Space** | start / stop the earthquake (the panel's button too); stopped, the ground calms down over 1.5 s |
| **left click** | a **wrecking ball** (cast steel) comes at what you clicked, at most one a second. The physics decides what breaks and topples |
| **↑ / ↓** | the ball's power (the **Ball** slider on the panel): the left end light and slow (0.5 t, it does not move a column), the notch balanced (4.2 t), the right end a **meteor** (5 m across, 520 t, 396 km/h: one brings the tower down) |
| **right click + mouse** | look · **WASD** fly (**W** where you look: look up and it climbs) · **E / Q** up / down · **Shift** faster · **wheel** zoom |
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

## 04 · Laboratory — a high-speed photography table: water, honey, sand, a water balloon, a watermelon, gel, a milk drop, a cube
A dark set, a soft light panel behind. At the top of the panel **Experiment** (or **1-8**). Everything is particles,
whatever the physics says. **Fast moments:** the trigger plays at once on a coarse world (**Coarse** at the top left);
if you want, **Compute it finely** (**I**) has a fine world compute the same inputs behind (**Fine pass …%**, the bar at
the bottom); click the **Time** bar to watch what is computed so far. When it is done it plays once at real speed, then
it is yours to look at. **R** goes from the fine recording back to the coarse world (the recording stays on the time
bar). No video: what is kept is the physics, drawn live from any view.

| Experiment | What to do |
|---|---|
| **Water** | the **Height** slider (12-45 cm; let go and the beaker is set up again), **Space** or **Pour**: the beaker pours into the glass; from high up it splashes and overflows |
| **Honey** | live; **left-drag**: the dipper follows the cursor · **Flow**, **Height** sliders · **Space** run / stop |
| **Sand** | live; **left-drag**: the funnel follows the cursor (fill the beaker or heap the table) · **Flow**, **Height** sliders · **Space** pour / stop |
| **Water balloon** | **left click** the balloon (a needle where you click) or **Space** (from the front) |
| **Watermelon** | **Gun**: pistol (cracks it open, knocks it off the stand) or rifle (blows it apart) · the **Round** slider (rifle: 5.56 → 7.62×51 → .50 BMG, real loads), **left click** the melon (fired from the camera) or **Space** (from the side, through the middle) |
| **Milk drop** | the **Height** slider (5-50 cm), **Space** or **Let a drop fall**: watch the crown at ×1/100 |
| **Gel** | the **Round** slider (pistol: .22 → 9 mm → .50 AE), **left click** the block or **Space** (from the side, lengthwise), **once a second, as often as you like**: the track and the temporary cavity inside the clear block; the fine pass computes every shot again in turn (a wait between two longer than 0.8 s shortened to 0.8 s) |
| **Cube** | the **Cubes** slider (64 → a million: 4³ … 100³; set up again when let go), the **Strength** slider (3 µg → 2 g of TNT; under it the speed a face's middle leaves at), **Space**, **Blow it up** or **left click** the cube. Watch it in slow motion; 64³ and more are slow live: **Compute it finely** (a million: ~6 min; the coarse cubes wait meanwhile, quicker with the camera at rest), then watch it at real speed and at ×1/100 |

| Key | What it does |
|---|---|
| **T** | speed: ×1, ×1/10, ×1/100 (live and in a recording; **Speed** on the panel). Live slow motion is the same physics, only slower |
| **I** | the fine pass: start / stop |
| **P** | pause / play (a finished recording plays from the start) · **← / →** 10 ms back / on in a recording (100 ms with **Shift**) |
| **right drag** | look · **WASD** move where you look · **E / Q** up / down · **Shift** fast · **wheel** forward / back · **C** camera back to the experiment |
| **R** | set up again (from a fine recording: back to the coarse world) |

**Try:** pop the balloon, **Compute it finely**, when the recording is done pull **Time** back to the start, pick **×1/100** and **P**: the
rubber peels back in 20-30 ms while the water keeps the balloon's shape for a moment. Pour the water from the highest
(45 cm), freeze the splash at ×1/10 and fly round it. Heap the sand in one place (a cone of ~32°), then take the funnel
to the beaker. Shoot the gel six or seven times in different places. Watch the melon at ×1/100: first it swells, then the rind opens.
Settings `scenes/laboratuvar.cfg` (F5).

## 05 · The Island — from an island's pier into the blue: amphorae, a galleon wreck, a rope going down
You start under the pier's concrete head, at the foot of the ladder: a dark hall, fouled piles, the sun coming down
through broken holes. Swim out along the lane of buoys (away from the island): shallow sand, amphorae at ~12 m, the
wreck at ~22 m, at the far end the mooring buoy and its rope going down. There is no surfacing; past ppO2 1.6 bar
(~66 m) the view narrows, goes black, and you start again at the ladder.

| Key | What it does |
|---|---|
| **W A S D** | swim (where you look) · **Space** up · **C** down · **Shift** a strong kick |
| **Tab** | mouse look (on/off) · **right button** hold to look |
| **F** | the diver's torch (inside the wreck, in the hall's dark; true colours close up: the red sea fans, orange sponges) |
| **P** | pause time (still: samples accumulate) · **K** caustics on/off · **E** edge pixels · **↑ / ↓** exposure |

Panel (top right): depth, pressure, the oxygen's partial pressure (ppO2), the passes' timings. **Try:** under the head go
beneath the holes and watch the light sway on the sand; enter the wreck through its fallen starboard side and switch the
torch on; follow the buoy's rope down and watch the floor appear at 35-40 m. Settings `scenes/koy.cfg` (F5).

## 06 · The Sculpture — a white Menger sponge cut through, on a plinth in a dark room; ten thousand times closer
The scene of geometry: the detail lives in real geometry, not in textures, and does not end as you come closer. As long
as nothing moves the picture accumulates and sharpens; top right, the zoom and how many millimetres wide the screen is.

| Key | What it does |
|---|---|
| **Wheel** | zoom towards the cursor, and out (that point stays put on the screen; to 10,000 times, 30 on the flat cut faces) |
| **T** | the dive tour: four dives to 10,000 times, stopping at every step to sharpen (a key or the mouse stops it) |
| **Left button** | point the light at what is clicked · **drag:** swing the light around that spot |
| **Right button** | drag: turn the sculpture · **R** the first view |
| **F** | depth of field: all sharp (the default) / macro · **↑ / ↓** exposure |

**Try:** watch the tour first (T). Then wheel into the edge of a star, stopping at every decade; drag the light to a
raking angle. Settings `scenes/heykel.cfg` (F5).

## 07 · Scale — from a beach in Izmir to the edge of the universe and to the Planck length
The scene of scale: 62 decades, with the wheel alone, without a cut. Everything where and as large as it truly was at noon
on 5 October 2026, and the empty stretches between at their true length. Time flows at its own pace at every scale (faster
outwards, slower inwards) but changes nothing: the way home always ends at the same noon. Top right, what is looked at and
the screen's width.

| Key | What it does |
|---|---|
| **Wheel** | move out, move in (a notch is x1.5; spun fast, up to 20 decades a second) |
| **R** | back to the beach |
| **T** | the tour: to the edge of the universe, back to the beach, to Planck, back (half a decade a second; the wheel or T stops it) |

**Try:**
- Move out from the beach slowly: the beach in front of the castle (Sentinel-2), the Bay of Çandarlı, the Aegean, the
  Earth with today's clouds.
- From the Earth to the Sun: the planets as points on their orbits, the asteroid belt, Jupiter's Trojans, the Kuiper
  belt. See the Milky Way from above, then Andromeda. Furthest out, 2MRS's real galaxies and the cosmic web.
- Back to the beach with R, and in: grains of sand, the crystal's steps, the atoms' honeycomb, the crystal's density
  map. Then the atom's electrons (they stream past), the nucleus's 28 nucleons in the empty atom, a proton and its
  three quarks.
- Further in: virtual pairs' loops at every scale about the quark (the colour mix follows the forces' strengths). At
  the bottom foam bubbles and a golden string.

### The second option: Speed
**Option: Speed** in the panel. By rocket from the Earth to Proxima or to Andromeda: special relativity complete, nothing
teleported, time always real. Proxima is 15 minutes on your clock (4.25 years on the Earth's), Andromeda 72 minutes
(2.4 million years). The target is chosen on the panel at any time (**Target: Proxima | Andromeda**, Proxima at first):
Space takes you to the chosen one, W at rest turns the rocket to it. Fly past it and the chosen target is still there.

| Key | What it does |
|---|---|
| **W** | thrust: from 1 g, rising with the speed (1% c in a minute, 21 nines in eight) |
| **S** | brake |
| **Space** | the automatic arrival at the chosen target: brakes at the right moment and stops there (under way elsewhere it stops and turns first) |
| **Tab** | the rocket's eye ↔ from the side |
| **Drag** | turn your head (the rocket's eye) · **R** look ahead |
| **Wheel** | binoculars (eye), scale (side) · **1-6** the side view's scales |
| **E** | the energy eye (on at first) · **G** the graphs |

**Try:**
- At the start drag to look back: the Earth with today's clouds. Then Space, and watch; no hurry, the journey's length
  is the distance itself.
- As the speed grows the sky crowds ahead. Past half light's speed the Sun you left behind comes round to the front by
  aberration, a dazzling blue-white star. At about five nines a red sun rises ahead: the universe's first light, the
  cosmic background. Toggle the energy eye with E: when the light leaves the visible band the real eye goes dark, the
  energy eye shows it.
- Tab to the side view and 1-6 for the scale: the rocket, the Earth and Moon, the Solar System, the nearby stars, the
  Milky Way, the Local Group.
- At Proxima look around: the spotted red dwarf, Alpha Centauri A and B, b, and behind you the Sun in Cassiopeia. Then
  Target: Andromeda on the panel, and Space. Braking into Andromeda, look ahead.

Plan and status in `docs/olcek/`, Speed's in `docs/olcek/hiz/` (Turkish). Settings `scenes/olcek.cfg` (F5).

## 08 · The Submarine — a fleet submarine's control room under night red
The scene of atmosphere: the light, the haze and the reflections are baked beforehand, the graphics card only draws the
picture. On the first opening the light is baked (with its progress on the loading screen, ~5 minutes); later
openings come from the cache.

| Key | What it does |
|---|---|
| **W A S D** | walk · **Space** jump · **C** crouch (hold) · **Shift** slow (look closely) |
| **Mouse** | look (no button; as the lighthouse's walk) · **Tab:** frees the cursor (for the panel), Tab again goes back to looking |
| **Right button** | binoculars (hold): a view four times narrower |
| **F12** | photograph at the panel's resolution and SSAA (8K 16x included; with a "computing" card) |
| **R** | back to the start · **↑ / ↓** exposure |

On the panel **Resolution** (720p-8K), **SSAA** (1x-16x), **Tessellation**, **Graphics**, **Light**, **Effects** (Low,
Medium, High). 8K 16x is drawn live at 8K 4x; the photograph is 16x.

**Try:**
- Walk forward from the start: the diving station to port (two 70 cm wheels, 16" depth gauges), the Christmas tree's
  green lights at its end, the chart lamp's white pool in the middle.
- Lean over the chart table: an Aegean sea chart, the watch's course and timed fixes in pencil, the compass rose, a
  parallel rule, dividers. The paper lies in waves, one corner lifting.
- Go close to the starboard switchboards with Shift: under every rotary switch its circuit's name ("STERN PLANE IND.",
  "1MC ANNOUNCING"), OFF/ON marks on its chrome escutcheon.
- Go close to a gauge: the dial's lettering, the room in its glass, the red lamp in the wheel's steel.
- Switch tessellation between Low and High; look at a nut's thread, the cork lining, an engraved brass plate.
- Switch SSAA between 1x and 16x; compare 720p 16x with 1440p 4x (the same number of samples).
- Take an 8K 16x photograph with F12 and zoom into the file.

Plan and status in `docs/denizalti/` (Turkish). Settings in `scenes/denizalti.cfg` (F5).

## 09 · The Mirror — a chrome sphere in a mirror labyrinth of Moorish arches

You are a chrome sphere; the eye is at its centre, you see yourself only in the mirrors. The mirrors are measured
(6 mm float glass + silver); a reflection is as sharp as a straight look.

| Key | |
| --- | --- |
| **Mouse** | look around (no click; **Tab** releases the cursor) |
| **W A S D** | forward, back, sideways · **Space / C** rise, sink · **Shift** fast |
| **Q / E** | our light's colour (warm white, daylight, red, orange, green, cyan, blue, violet) |
| **Wheel** | our light's strength (8 steps) · **F** on / off |
| **G** or **left click** | leave a lamp in front (in the light's colour and strength, at most 8) · **X** gather them |
| **R** | back to the start · **↑ / ↓** exposure |

No settings panel: the quality is set for 2560 × 1440 (30-40 frames a second).

**Try:**
- At the start look along the corridor: arches, columns and lanterns go on without end in the mirrors.
- Turn to the corridor's mirror: our sphere and the light in its slit; in the tunnel of the two parallel mirrors its copies
  shrink, darken and turn green (the glass's iron).
- Hold the light slanting at a mirror: the reflected cone falls on the far wall and into the mist.
- Leave three lamps of different colours, walk away and look at the mirrors: each multiplies in every direction.
- Pass slowly through a doorway: the light on the jamb is sharp even close up.

Plan, measurements and status in `docs/ayna/` (Turkish). Settings `scenes/ayna.cfg` (F5).
