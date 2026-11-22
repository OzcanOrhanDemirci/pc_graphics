# Demo

*Turkish: [GOSTERI.md](GOSTERI.md)*

## Opening
`build\pc_graphics.exe` (inside the repository) — double-click it. The **gallery** opens full screen, borderless, on
the primary monitor: the thirteen scenes in a numbered list on the left, the selected one's picture and short
description on the right. The Island, Scale, Rainforest, Fire and Mountain read open data that is not in the
repository; run the scripts under "Building" in the README once before opening them.

- **↑ / ↓** (or **W / S**, or a click with the mouse) to choose, **Enter** (or the **Open scene** button) to open. The
  first nine scenes have number keys (**1-9**), the tenth **0**; 11-13 are reached with the arrows or the mouse.
- **A** (or the **Details** button): how the scene is made, over its picture, under headings; **A** or **Esc** closes it.
- **L** (or **TR | EN** at the top right): the application's language, Turkish ↔ English; the choice is remembered.
  `--lang en` or `--lang tr` on the command line does the same.
- A scene being opened gets ready behind a loading screen: its GPU programs compile and its world is built. **Esc**
  gives up the loading. The Submarine bakes its light on the first opening, which takes a few minutes; later openings
  come from the cache.
- In every scene: **Esc** back (help → scene → gallery; in the gallery, quit) · **F1** the scene's keys · **H** hide the
  interface · **F12** screenshot (`captures/`) · **F5** reload the settings and GPU programs (every scene except
  Collapse).
- The interface over a scene: its name, state and any mouse controls at the bottom left (in Collapse the building, the
  ball, the magnitude and the length; in Laboratory the experiment and the time bar; in a few scenes the sample count),
  frame rate and timings at the top right, the **F1**, **H** and **Esc** hints at the bottom right.

## 01 · Golden Hour — glass and stone, sunlight split into its colours
A concrete room, a low sun through five narrow slits, fourteen crystal prisms hanging in its beams and spectra scattered
over the concrete. It starts **still**: the picture clears as samples build up. The denoiser works up to 8,192 samples,
then steps back and the raw picture remains.

| Key | What it does |
|---|---|
| *Mode and camera* | |
| **Space** | still ↔ **live** mode (live, the water ripples, dust and bubbles drift) |
| **C** | next camera (rainbows, prism still life, low, middle, corner, pool, plan) |
| **← / →** | focus nearer / farther |
| **↑ / ↓** | exposure up / down (¼ stop) |
| **R** | restart the accumulation |
| *Layers* | |
| **P** | photons on / off (caustics) |
| **V** | haze on / off |
| **N** | direct light sampling on / off |
| **G** | sunlit patches as lights (on / off) |
| **D** | denoiser on / off |
| *View* | |
| **1** | final image |
| **2 / 3** | albedo / normals |
| **4** | caustics only |
| **5** | cost heat map |
| **6** | raw image, not denoised |

**Try:** press **P**: the rainbows, the prism's spectrum and the water patterns disappear; all of them come from a
separate photon pass. Turn the denoiser off with **D** and wait a minute: without it the picture converges to the same
image. Go live with **Space**: it is drawn at 1280×720 and the AI scales it to 2560×1440, about 24 frames a second; the
difference shows most in the prism. Here **F12** also writes a linear EXR next to the PNG. Settings in
`scenes/altinsaat.cfg` (sun, haze, white balance, exposure, glow; **F5** applies them at once).

## 02 · Lighthouse — a lighthouse beam turning in the night fog
A night in the manner of Playdead's *Inside*: on a foggy sea a lighthouse turns, its beams cut through the fog, and
black firs and one warm window stand dark against them. It opens at view A, "Window"; the other is B, "Lookout".

| Key | What it does |
|---|---|
| *Scene* | |
| **Space** | walk A ↔ B through the forest (778 m, ~70 s); press it on the way to turn back |
| **B** | jump to the other view |
| **P** | pause / resume time |
| **← / →** | time 1 s back / forward |
| **↑ / ↓** | exposure ±0.25 EV |
| **1-5** | final, surface, fog, depth, normal |
| *Player* | |
| **Tab** | scene → player (on your own island) → lighthouse (at the foot of the tower) → scene |
| **W A S D** | walk |
| **Mouse** | look around |
| **Shift** | hold to run |
| **Space** | jump |

Player mode is a curiosity: the world was not built for it, but you bump into the trees, the tower and the cabin, and
swim in deep water. **Try:** walk into the forest with **Space**: the eye adapts halfway to the dark in 1.5 seconds,
so the forest stays dark but reads. Look at the surfaces and the fog on their own with **2** and **3**. Settings in
`scenes/fener.cfg` (**F5** applies them at once).

## 03 · Collapse — concrete buildings, earthquakes and fracture
**Building** at the top of the panel: **Apartment** (5 storeys, shops on the ground floor), **Tower** (30 storeys, 97 m,
glass lobby), **House** (one storey, gable roof, chimney), **Seismic** (26 storeys and a steel spire, ~123 m, on rubber
bearings: it sways and glides but stands even at 9). Choosing one rebuilds it and turns the camera to it. While
loading, the building settles under its own weight. The point is the physics.

| Key | What it does |
|---|---|
| *Earthquake* | |
| **3-9** | choose the magnitude (also with the mouse under **Magnitude** on the panel) |
| **← / →** | shorter / longer; the **Length** slider on the panel, its notch the magnitude's own length, its far right end **∞**: until you stop it |
| **Space** | start / stop the earthquake (the panel's button too); stopped, the ground calms down over 1.5 s |
| **T** | slow motion: ×1, ×0.25, ×0.1 |
| **P** | pause / resume time |
| *Ball and building* | |
| **Left click** | throw a cast steel wrecking ball at what you clicked (at most one a second); the physics decides what breaks and topples |
| **↑ / ↓** | the ball's power (the **Ball** slider on the panel): the left end half a tonne at 29 km/h, the notch balanced at 4.2 t, the right end a **meteor**, overdone on purpose (5 m across, 514 t, 396 km/h) |
| **R** | rebuild the building |
| *Camera* | |
| **Right click** + **Mouse** | hold to look around |
| **W A S D** | fly; **W** goes where you look (look up and you climb) |
| **E / Q** | rise / sink |
| **Shift** | hold to go fast |
| **Wheel** | zoom in / out |
| *Picture* | |
| **B** | show / hide the bars |
| **G** | denoising: our own filter → AI → raw image |

| Magnitude | Apartment | Tower | House |
|---|---|---|---|
| 3-5 | it sways and cracks; nothing falls | it sways | it sways |
| 6 | balcony parapets and some ground-storey infill fall | hairline cracks | two cracks |
| 7 | heavy damage: parapets and the ground storey's infill fall, the frame stands | the lobby storey fails, the tower topples whole | six cracks |
| 8 | soft-storey collapse: the ground storey goes, the block leans over and comes down | (not measured) | it collapses |
| 9 | a longer, harder shaking; in the end the block comes down entirely | it collapses | it collapses |

**Try:** the tower at 6 and at 7: at 6 it sways, at 7 it topples; watch it fall in slow motion with **T**. A ball at the
house's roof. 6 and 8 one after the other on the apartment block (rebuilding with **R** in between). 7 with **∞**, to
watch the damage build up over a long shaking. Balls at the ground-storey columns at different powers: the balanced
ball breaks a corner or an edge column, the lightest does not move it, the meteor brings down the apartment block or
the tower in one throw. After a collapse, fly up to the broken column ends and look at the bars with **B**. Settings in
`scenes/yikim.cfg` (buildings, materials, solver, camera, sun); this scene has no **F5**, its settings are read when it
opens again.

## 04 · Laboratory — liquids, sand and breaking solids in slow motion
A high-speed photography table: a dark set, a soft light panel behind. At the top of the panel **Experiment** (or
**1-8**). Everything is particles, whatever the physics says. A trigger plays at once on a coarse world (the panel says
**Coarse pass**). If you want, **Compute it finely** (**I**) has a fine world compute the same inputs in the background
(**Fine pass …%** and the bar at the bottom). Click the **Time** bar to watch what is computed so far; when it is done
the recording plays once at real speed, then it is yours to look at. No video: what is kept is the physics, drawn live
from any side.

| Experiment | What to do |
|---|---|
| **Water** | the **Height** slider (12-45 cm; let go and the beaker is set up again), **Space** or **Pour**: the beaker pours into the glass; from high up it splashes and overflows |
| **Honey** | live; **left-drag**: the dipper follows the cursor · the **Flow** and **Height** sliders · **Space** or **Run / Stop** |
| **Sand** | live; **left-drag**: the funnel follows the cursor (fill the beaker or heap the table) · the **Flow** and **Height** sliders · **Space** or **Pour / Stop** |
| **Water balloon** | **left click** the balloon (a needle where you click) or **Space** / **Prick it** (from the front) |
| **Watermelon** | **Gun**: pistol (cracks it open, knocks it off the stand) or rifle (blows it apart) · the **Round** slider (rifle: 5.56 → 7.62×51 → .50 BMG, real loads) · **left click** the melon (fired from the camera) or **Space** (from the side, through the middle) |
| **Gel** | the **Round** slider (pistol: .22 → 9 mm → .50 AE) · **left click** the block or **Space** (from the side, lengthwise), once a second, as often as you like: the track and the temporary cavity show inside the clear block. The fine pass computes every shot again in turn (a wait between two longer than 0.8 s is shortened to 0.8 s) |
| **Milk drop** | the **Height** slider (5-50 cm), **Space** or **Let a drop fall**: watch the crown at ×1/100 |
| **Cube** | the **Cubes** slider (64 → a million: 4³ … 100³; set up again when let go), the **Strength** slider (3 µg → 2 g of TNT; under it the speed a face's middle leaves at), **Space**, **Blow it up** or **left click** the cube. 64³ and more are slow live: **Compute it finely** (a million: ~6 min; the coarse cubes wait meanwhile), then watch it at real speed and at ×1/100 |

| Key | What it does |
|---|---|
| *Experiment* | |
| **1-8** | choose an experiment (panel order) |
| **Space** | trigger: pour, fire, blow up |
| **Left click** | click a target, drag to steer a stream |
| **I** | start / stop the fine pass |
| **R** | reset, or leave the fine recording for the coarse world (the recording stays on the time bar) |
| *Time* | |
| **T** | speed: ×1, ×1/10, ×1/100 (**Speed** on the panel); live slow motion is the same physics, only slower |
| **P** | pause / play (a finished recording plays from the start) |
| **← / →** | 10 ms back / on in a recording |
| **Shift** + **← / →** | 100 ms back / on in a recording |
| *Camera* | |
| **Right click** + **Mouse** | hold to look around |
| **W A S D** | move |
| **Q / E** | down / up |
| **Wheel** | forward / back |
| **Shift** | hold to move fast |
| **C** | camera back to the experiment |

**Try:** pop the balloon, **Compute it finely**; when the recording is done pull **Time** back to the start, pick
**×1/100** and **P**: the rubber peels back in 20-30 ms while the water keeps the balloon's shape for a moment. Pour the
water from the highest (45 cm), freeze the splash at ×1/10 and fly round it. Heap the sand in one place (a cone of
about 32°), then take the funnel to the beaker. Shoot the gel six or seven times in different places. Watch the melon
at ×1/100: first it swells, then the rind opens. Settings in `scenes/laboratuvar.cfg` (**F5**).

## 05 · The Island — from an island's pier into the blue
You start under the pier's concrete head, at the foot of the ladder: a dark hall, fouled piles, the sun coming down
through broken holes. Swim out along the lane of buoys, away from the island: shallow sand, amphorae at ~12 m, the
wreck at ~22 m, at the far end the mooring buoy and its rope going down. There is no surfacing; once the oxygen's
partial pressure (ppO₂) passes 1.6 bar (~66 m) the view narrows, goes black, and you start again at the ladder.

| Key | What it does |
|---|---|
| *Diver* | |
| **W A S D** | swim (where you look) |
| **Space** | rise |
| **C** | sink |
| **Shift** | hold for a strong kick |
| **Tab** | mouse look on / off |
| **Right click** | hold to look |
| *Scene* | |
| **P** | pause / resume time (paused, samples accumulate) |
| **E** | show / hide the edge pixels |
| **K** | caustics on / off |
| **↑ / ↓** | exposure up / down |

Top right, the depth, the pressure, the ppO₂ and the passes' timings; bottom left, the pixel and edge samples and the
texture quality. **Try:** under the head, go beneath the holes and watch the light sway on the sand. Enter the wreck
through its fallen starboard side. Follow the buoy's rope down: the floor appears at 35-40 m. Settings in
`scenes/koy.cfg` (**F5**).

## 06 · The Sculpture — a cut Menger sponge, ten thousand times closer
A white cube cut through its middle stands on a plinth in a dark room, six-pointed stars in the cut. The detail lives
in real geometry, not in textures, and does not end as you come closer. Every frame is drawn from the same fixed
samples: what you see in motion is what you see at rest. Top right, the zoom and how many millimetres wide the screen
is.

| Key | What it does |
|---|---|
| *View* | |
| **Wheel** | zoom to the cursor, and back out (that point stays put on the screen; to 10,000 times, 30 on the flat cut faces and the plinth) |
| **Right click** | drag: turn the sculpture |
| **R** | back to the first view |
| **T** | start / stop the dive tour: four dives, each stopping at 10, 100, 1,000 and 10,000 times (a key or the mouse stops it too) |
| *Light* | |
| **Left click** | aim the light at the spot clicked |
| **Left click** | drag: swing the light around it |
| **↑ / ↓** | exposure ±0.25 EV |
| **F** | depth of field: macro / all sharp |

**Try:** watch the tour first (**T**). Then wheel into the edge of a star, stopping at every decade; drag the light to a
raking angle. At ten thousand times the screen is 0.15 mm across, and the structure goes on. Settings in
`scenes/heykel.cfg` (**F5**).

## 07 · Scale — from a beach to the universe's edge and Planck
The scene of scale: 62 decades, with the wheel alone, without a cut. You start on the beach in front of the castle at
Çandarlı (Dikili, Izmir). Everything is where and as large as it truly was at noon on 5 October 2026, and the empty
stretches between are at their true length. Time flows at its own pace at every scale (faster outwards, slower inwards)
but changes nothing: you always come back to the beach at the same noon. Top right, what is looked at and the screen's
width.

| Key | What it does |
|---|---|
| *Scale* | |
| **Wheel** | zoom out / in (a notch is ×1.5; spun fast, up to 20 decades a second) |
| **R** | back to the beach |
| **T** | start / stop the tour: to the edge of the universe, back to the beach, to Planck, back (half a decade a second; the wheel stops it too) |

**Try:**
- Move out from the beach slowly: the beach in front of the castle (Sentinel-2), the Bay of Çandarlı, the Aegean, the
  Earth with that day's clouds.
- From the Earth to the Sun: the planets as points on their orbits, the asteroid belt, Jupiter's Trojans, the Kuiper
  belt. See the Milky Way from above, then Andromeda. Furthest out, 2MRS's real galaxies and the cosmic web.
- Back to the beach with **R**, and in: grains of sand, the crystal's steps, the atoms' honeycomb, the crystal's density
  map. Then the atom's electrons (they stream past), the nucleus's 28 nucleons in the empty atom, a proton and its
  three quarks.
- Further in: virtual pairs' loops about the quark at every scale. At the bottom foam bubbles and a golden string, both
  theory.

### The second option: Speed
**Option: Speed** on the panel. By rocket from the Earth to Proxima or to Andromeda: special relativity with exact
formulas, nothing teleported, time always real. Proxima is 15 minutes on your clock (4.25 years on the Earth's),
Andromeda 72 minutes (2.4 million years). The target is chosen on the panel at any time (**Target: Proxima |
Andromeda**, Proxima at first); a rocket at rest turns to it at once. Fly past it and the chosen target is still there.

| Key | What it does |
|---|---|
| *Speed · flight* | |
| **W** | hold to thrust: from 1 g, rising with the speed (1% of light speed in a minute, 21 nines in eight) |
| **S** | hold to brake |
| **Space** | automatic arrival at the chosen target on / off: brakes at the right moment and stops there (under way elsewhere it stops and turns first); **W** or **S** takes back the controls |
| *Speed · view* | |
| **Tab** | rocket's eye ↔ side view |
| **G** | graphs on / off |
| **E** | energy eye on / off (on at first) |
| **Left click** | hold and drag to look around |
| **Wheel** | zoom (binoculars, eye) / change scale (side) |
| **1-6** | the side view's scales: rocket, Earth and Moon, Solar System, nearby stars, Milky Way, Local Group |
| **R** | look ahead / back to the rocket |

**Try:**
- At the start drag to look back: the Earth with that day's clouds. Then **Space**, and watch; no hurry, the journey's
  length is the distance itself.
- As the speed grows the sky crowds ahead. Past half light's speed the Sun you left behind comes round to the front by
  aberration, a dazzling blue-white star. At about five nines a red sun rises ahead: the universe's first light, the
  cosmic background. Toggle the energy eye with **E**: when the light leaves the visible band the real eye goes dark,
  the energy eye shows it.
- **Tab** to the side view and **1-6** for the scale.
- At Proxima look around: the spotted red dwarf, Alpha Centauri A and B, its planet b, and behind you the Sun in
  Cassiopeia. Then **Target: Andromeda** on the panel, and **Space**. Braking into Andromeda, look ahead.

Plan and status in `docs/olcek/`, Speed's in `docs/olcek/hiz/` (Turkish). Settings in `scenes/olcek.cfg` (**F5**).

## 08 · The Submarine — a control room under night red
The control room of a Tench-class fleet submarine, lit by three red night lamps and the chart table's lamp. The scene of
atmosphere: the light, the haze and the reflections are baked beforehand, the graphics card only draws the picture. On
the first opening the light is baked (with its progress on the loading screen, about 5 minutes); later openings come
from the cache.

| Key | What it does |
|---|---|
| *Movement* | |
| **W A S D** | walk |
| **Shift** | hold to walk slowly (to look closely) |
| **Space** | jump |
| **C** | hold to crouch |
| **Mouse** | look around (no click) |
| **Right click** | hold for the binoculars |
| **Tab** | free the cursor for the panel / back |
| **Left click** | drag to look while the cursor is free |
| **R** | back to the start |
| *Image* | |
| **↑ / ↓** | exposure up / down |

On the panel **Resolution** (720p-8K), **SSAA** (1x-16x), **Tessellation**, **Graphics**, **Light**, **Effects** (Low,
Medium, High). Here **F12** takes a photo at the panel's resolution and SSAA (8K 16x included), under a "Computing the
photo" card, in tiles of 1920 × 1080. 8K 16x is drawn only for the photo; at that setting the live picture is 8K 4x.

**Try:**
- Walk forward from the start: the diving station to port (wheels, 16" depth gauges), the chart lamp's white pool in the
  middle.
- Lean over the chart table: an Aegean sea chart, the watch's course and fixes in pencil, the compass rose, a parallel
  rule, dividers.
- Go close to the starboard switchboards with **Shift**: under every rotary switch its circuit's name, OFF/ON marks on
  its chrome escutcheon.
- Look at a gauge through the binoculars: the dial's lettering, the room in its glass.
- Switch tessellation between Low and High; look at a nut's thread, the cork lining, an engraved brass plate.
- Switch SSAA between 1x and 16x; compare 720p 16x with 1440p 4x (the same number of samples).
- Take an 8K 16x photo with **F12** and zoom into the file.

Plan and status in `docs/denizalti/` (Turkish). Settings in `scenes/denizalti.cfg` (**F5**).

## 09 · The Mirror — reflections of reflections in a mirror maze
You are a chrome sphere floating through a mirror labyrinth of Moorish arches; the eye is at its centre, and you see
yourself only in the mirrors. The mirrors are computed from measured values (6 mm float glass with silver behind it);
a reflection is as sharp as a straight look.

| Key | What it does |
|---|---|
| *Movement* | |
| **Mouse** | look around (no click) |
| **W A S D** | forward, back, sideways |
| **Space** | rise |
| **C** | sink |
| **Shift** | hold to go fast |
| **Tab** | free / capture the cursor |
| **1-4** | views: corridor, mirror, hall, floor |
| **R** | back to the start |
| *Light* | |
| **Q / E** | light colour: warm white, daylight, red, orange, green, cyan, blue, violet |
| **Wheel** | light strength (8 steps) |
| **F** | light on / off |
| **Left click** / **G** | drop a lamp in front, in the current colour and strength (at most 8) |
| **X** | gather the lamps |
| **↑ / ↓** | exposure |

No settings panel: the quality is set for 2560 × 1440, 30-44 frames a second.

**Try:**
- At the start look along the corridor: arches, columns and lanterns go on without end in the mirrors.
- Turn to a mirror with **2**: our sphere and the light in its slit; in the tunnel of two parallel mirrors its copies
  shrink, darken and turn cyan-green (the glass's iron).
- Hold the light slanting at a mirror: the reflected cone falls on the far wall and into the haze.
- Drop three lamps of different colours, move away and look at the mirrors: each multiplies in every direction.
- Pass slowly through a doorway: the light on the jamb is sharp even close up.

Plan, measurements and status in `docs/ayna/` (Turkish). Settings in `scenes/ayna.cfg` (**F5**).

## 10 · The Well — an endless fall through mirrored rooms
You are an invisible observer falling at 1.5 m/s down a shaft between two columns of rooms. Every room is a dark glass
cube: a closed world whose light turns inside without end. The rooms' edges run on as endless bundles of threads.

| Key | What it does |
|---|---|
| *Moving* | |
| **Mouse** | look around (no click) |
| **W A S D** | drift level (you meet the rooms and the bundles) |
| **Space** | rise |
| **C** | sink |
| **Q / E** | slow / quicken the fall (still, 0.5, 1.5, 4, 10 m/s) |
| **Shift** | hold to go faster and faster: the speed grows e-fold every 1.3 s, up to 12,000 m/s; let go and it comes back within a few seconds |
| **Tab** | release / take back the cursor |
| *View* | |
| **1-4** | shaft, room (you stop before it), lattice, up |
| **R** | back to the start |
| **↑ / ↓** | exposure |

No settings panel: the quality is set for 2560 × 1440, 33-42 frames a second.

**Try:**
- At the start look straight down: the shaft's vanishing point, the rooms passing level after level.
- **2**: you stop before a room's glass. Inside, the lamp's images go on in a three-dimensional lattice; deep down they
  turn amber (thin silver reflects red more than blue).
- Drift up to a room's glass with **W**: from outside the glass reflects only 4%, the inside glows behind the dark glass.
- Stop with **Q** and move the mouse slowly: the small lights of distant rooms slide without twinkling.
- Quicken with **E E**: the rooms stream silently upwards.
- Hold **Shift** while falling: slowly at first, then faster and faster. Above some speed the lattice moves more than
  half a cell between two frames: its frame seems to stand, shiver or go back, and the rooms change every frame (the
  wagon wheel's illusion).

Plan, measurements and status in `docs/kuyu/` (Turkish). Settings in `scenes/kuyu.cfg` (**F5**).

## 11 · Rainforest — a dense rainforest, early morning after rain
You walk by a stream in a rainforest full of plants on every side: ferns, roots, moss, stones, a fallen trunk across
the stream. The sun filters through the leaves and mist lies on the ground. Every plant and stone is a real
open-licence scan. No wall ends the forest: walk straight on and you reach the stream again from the other side after
396 m. The first loading takes about half a minute.

| Key | What it does |
|---|---|
| *Movement* | |
| **Mouse** | look around (no click) |
| **W A S D** | walk |
| **Shift** | hold to run |
| **Space** | jump |
| **C** | hold to crouch |
| **Tab** | free the cursor for the panel / back |
| **R** | back to the start |
| *View* | |
| **1-4** | preset views: down the stream into the light, the fallen trunk, up into the leaves, close to the ground |
| **↑ / ↓** | exposure up / down |

On the panel **Samples**: **DLSS** (the default: one ray per pixel a frame, combined by NVIDIA DLSS Ray Reconstruction)
or, for comparison, 1, 4, 8 or 16 fixed rays per pixel. Bottom left, the number of copies and triangles.

**Try:**
- At **1** look down the stream into the light: sunbeams in the mist, wet stones.
- Go to the fallen trunk with **2**, look up into the canopy with **3**: leaves layer on layer up to the sky.
- At **4** crouch and look at the ground: the fallen leaves are laid on it one by one.
- While walking, switch the panel between **DLSS** and **4**: with fixed samples the twigs finer than a pixel shimmer,
  with DLSS they hold still.
- Walk straight in one direction: the forest does not end, and after a while you reach the same stream.

Plan, measurements and status in `docs/orman/` (Turkish). Settings in `scenes/orman.cfg` (**F5**).

## 12 · Fire — a fire burning in a stone pit in the dark
A wood fire in a stone pit, outdoors on a moonless night. The flame's shape, colour and light and its sparks are not
drawn by hand but computed from the physics of burning in real time: the gas flows, the wood heats and breaks down, the
char turns to embers. The sky the fire does not reach stays pure black.

| Key | What it does |
|---|---|
| *Movement* | |
| **Mouse** | look around (no click) |
| **W A S D** | walk round the pit |
| **C** | hold to crouch |
| **Tab** | free the cursor / back |
| *Scene* | |
| **1-4** | preset views: standing, crouched by the pit, low against the sky, down into the embers |
| **R** | lay the fire again |
| **↑ / ↓** | exposure up / down |

Bottom left, the fire's heat output, the wood and char left, and the hottest surface's temperature.

**Try:**
- At **1** look at the stones behind the flame: the hot air bends the light and they shimmer.
- Crouch by the pit with **2**: the blue at the flame's base, over the glowing embers.
- Look from low down with **3**: flames with blue bases against the night.
- Look into the embers with **4**: glowing char between the logs, ash covering it.
- Watch the sparks: they fly out when a pocket in the wood bursts, and the gas carries them up.
- Lay the fire again with **R** and watch it grow back.

Plan, measurements and status in `docs/ates/` (Turkish). Settings in `scenes/ates.cfg` (**F5**).

## 13 · Mountain — sunset on Kaçkar, the real Earth to the horizon
You stand on a small railed terrace on Kaçkar's 3,937 m summit, the sun setting over the Black Sea ahead, the Caucasus
to the right, Ararat behind. Every peak is real, the farthest about 390 km away. In the opening view the railing has a
gap: walk through it and down onto the mountain.

| Key | What it does |
|---|---|
| *Movement* | |
| **Mouse** | look around (no click) |
| **W A S D** | walk |
| **Shift** | hold to run |
| **Alt** | hold to move ten times faster |
| **Space** | jump |
| **C** | hold to crouch |
| **Tab** | free the cursor for the panel / back |
| **R** | back to the start |
| *View* | |
| **Right click** | hold for the binoculars |
| **Right click** + **Wheel** | binocular zoom (3x-30x) |
| **1-5** | preset views |
| **↑ / ↓** | exposure up / down |

On the panel **Samples**: 4, 8, 16, 32 or 64 rays per pixel (about 60 frames a second at 16). Through the binoculars
the panel names what you look at and how far it is.

**Try:**
- At the start the sun is ahead: its path on the sea, ridge after ridge. First look at the sun's path through the
  binoculars: waves appear.
- **2**, then binoculars at bearing 20°: Elbrus, 299 km, its two tops and snow. **3**, bearing 114°: Ararat, 295 km.
- Turn east with **3**: Kaçkar's own shadow in the air, a triangle out to the horizon.
- Lean on the railing and look down: rock, boulders, the valley below. Then go through the gap and down the mountain.

Plan, measurements and status in `docs/dag/` (Turkish). Settings in `scenes/dag.cfg` (**F5**).
