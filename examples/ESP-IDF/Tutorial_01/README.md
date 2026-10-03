# Tutorial 01 — from an empty screen to an endless track

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/endless_track.png" alt="A low-poly car on a scrolling race track, drawn by a3d on an ESP32-P4" width="80%">
</p>

Six lessons, one number to change between them. You start with a black screen
and finish with a car driving down a track that never ends, past trees that
never repeat, under colours that keep changing.

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/steps.png" alt="The six lessons" width="100%">
</p>

| | Lesson | What it is actually about |
|---|---|---|
| 1 | **Put the object on the screen** | `openAsset()` and `frameModel()`. There is almost no code, and that is the point |
| 2 | **Make a background** | the overlay hook, and the one trick that lets you draw *behind* a model a3d never buffers |
| 3 | **Add light** | the three separate things that decide how a surface looks, and which of them a `.a3d` brings with it |
| 4 | **Change the camera view** | the orbit vs. `setCamera()`, and why "distance 2.5" means something different on every panel |
| 5 | **Play the animation** | it was already playing. This is about controlling it |
| 6 | **The endless track** | all five at once, plus the idea that makes it endless: **the car never moves** |

---

## What you need

- An **ESP32-P4** with a panel you already have working. This project is
  written for a **Waveshare ESP32-P4-Nano with the 10.1" 800x1280 JD9365 DSI
  panel**, and borrows that board's bring-up from the benchmark example next
  door rather than copying it. [Any other board](#your-board-is-not-that-one)
  is a three-line change.
- **ESP-IDF 5.5.x.** 6.x builds and runs, and the project warns you at
  configure time; a3d measured about 9% slower on the P4 under 6.0.2.
- Nothing else. a3d is header-only and pulls in no third-party library.

```bash
cd examples/ESP-IDF/Tutorial_01
idf.py set-target esp32p4
idf.py -p <port> flash monitor
```

## The one line to change

Near the top of [`main/main.cpp`](main/main.cpp):

```c
#define TUTORIAL_STEP 6
```

1 to 6. Change it, reflash, and read the matching lesson in
[`main/tutorial_steps.cpp`](main/tutorial_steps.cpp). **Every lesson calls the
one before it**, so the new idea is always the code *after* that call —
read a lesson bottom-up and you read exactly what it added.

The lessons are also full of commented-out lines marked `TRY THIS:`.
Uncomment one, reflash, and see what changes. They are the fastest way to
learn what each call actually does:

```cpp
// TRY THIS: light from below. It looks wrong in a way that is worth seeing
//           once - it is how you learn to recognise a sign error in a
//           light direction.
// viewer.setLightDirection(0.35f, 0.86f, 0.38f);
```

## What each lesson looks like on the panel

These are frames of the 1280x800 picture the firmware draws, not photographs
of the glass. Each one was rendered on a laptop from the same
`tutorial_steps.cpp`, driven by a copy of `main.cpp`'s loop: the slow turn in
lessons 1 to 3, the clocks, twenty frames a second. From lesson
2 on, the clock in the top left is the moment the frame was taken. One line
differs: the board writes `a3d  NN fps` in the bottom left once it has been
running a second, and these show only `a3d`.

### 1. Put the object on the screen

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step1.png" alt="Lesson 1: the red car, framed on a near-black background" width="80%">
</p>

The car, framed by a3d on near-black, and nothing else: no track, no clock.
It turns slowly by itself, a full turn about every 16 seconds. That turn is
`main.cpp`, not the lesson.

### 2. Make a background

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step2.png" alt="Lesson 2: the car, small and at an angle, over a still track with trees" width="80%">
</p>

The track comes in behind the car, with trees, the run clock and — in orange —
a lap clock that restarts every 20 seconds. Nothing scrolls yet. The car keeps
turning where it is and is not lined up with the road: it is floating over a
picture, and lesson 6 is what fixes that.

Comment out the `setOverlay()` line and this is what is left. Every magenta
pixel is one the car did not cover, and repainting exactly those is
[the whole trick](#the-trick-worth-knowing-drawing-behind-a-model):

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step2_key.png" alt="Lesson 2 without its overlay: the car on a solid magenta screen" width="49%">
</p>

### 3. Add light

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step3.png" alt="Lesson 3: the same scene as lesson 2, with the car lit from above" width="80%">
</p>

At a glance it is lesson 2. Close up, the light is the difference: a3d's
default light falls mostly on the back of the car and leaves the roof dark;
lesson 3 aims it down, so the roof — the face this camera sees — is lit. The
same moment in both lessons, pixels doubled:

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step3_light.png" alt="The car close up in lesson 2, roof in shadow, and in lesson 3, roof lit" width="100%">
</p>

### 4. Change the camera view

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step4_views.png" alt="Lesson 4's four camera views: overhead, lower and closer, turning, and aimed to one side" width="100%">
</p>

Four views, 3.5 seconds each, round and round. The car no longer turns by
itself: the lesson places the camera every frame, and would undo anything else
that tried. The fourth view is the one the orbit
cannot give you — `setCamera()` aimed beside the car rather than at it.

### 5. Play the animation

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step5.png" alt="Lesson 5: the car from behind and above, close, on a still track" width="80%">
</p>

One camera, held still, behind and above the car. What moves is the wheels —
the clip stored in the `.a3d` — and every 3 seconds the lesson changes how it
plays: as authored, slow motion, fast, backwards, paused. It is easy to miss at
a standstill; lesson 6 puts a road under it.

### 6. The endless track

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step6.png" alt="Lesson 6: the car turned into a bend on the scrolling track" width="80%">
</p>

The road slides down the screen, the bends swing round the car, the car turns
to follow them and the wheels spin at the road's speed — and still
[the car never moves](#why-the-car-never-moves). Every 11 seconds the colours
change, fading over the last 2.5:

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/step6_themes.png" alt="The six colour themes lesson 6 cycles through: circuit, dusk, night, desert, snow and neon" width="100%">
</p>

## Two files, and the split matters

| File | Knows about |
|---|---|
| [`main/main.cpp`](main/main.cpp) | your **board** — the DSI panel, the rotation, FreeRTOS, `heap_caps`, where the model bytes are |
| [`main/tutorial_steps.cpp`](main/tutorial_steps.cpp) | **a3d**, and nothing else. No vendor header appears in it |

That is why the lessons can be pasted into your own project whatever your
board is — and why every picture on this page was rendered on a laptop by
compiling `tutorial_steps.cpp` against a `Display` that writes to memory.

---

## The panel is sideways, and the lessons never find out

<p align="center">
  <img src="https://raw.githubusercontent.com/0015/a3d/main/examples/ESP-IDF/Tutorial_01/docs/rotation.png" alt="The landscape render, and the portrait framebuffer it is rotated into" width="88%">
</p>

This panel scans **800 across by 1280 down**. The tutorial is landscape, so it
draws a **1280x800** picture and `main.cpp` turns it a quarter turn on the way
to the glass.

**You cannot ask this driver to rotate.** A MIPI-DSI panel in video mode has no
rotation to ask for: the DPI peripheral scans the framebuffer out continuously
in hardware, in the order the DSI timing says, and `esp_lcd_panel_swap_xy()` is
not implemented for it at all. RGB parallel is the same. A panel with its own
controller RAM — most SPI and QSPI parts — *can* do it in a register, and if
yours can, use that and set `PANEL_IS_PORTRAIT` to 0: it is free and this is
not.

**What makes it three lines instead of a port** is a3d's own shape. An
`a3d::Display` is a size and a function that moves a rectangle of pixels.
Nothing above it can tell whether that function talks to a panel, to a file,
or — as here — to *another Display* with the pixels turned:

```cpp
class RotatedDisplay            // main.cpp
    {
    // render (rx, ry)  ->  panel ((kRenderH - 1) - ry, rx)
    //
    // so a full-width strip of the landscape render becomes a narrow
    // full-height COLUMN of the portrait panel
    };
```

The viewer hands it full-width strips; each one comes out as a column. The
whole of "this app is landscape on a portrait panel" is that class, and not one
line of the six lessons knows about it.

If the picture comes out upside down, flip `ROTATE_CLOCKWISE`. That is the
whole adjustment; there is no third case.

---

## The trick worth knowing: drawing *behind* a model

a3d never holds a whole frame. The screen is cut into horizontal strips, and a
strip is cleared, drawn into and pushed to the panel before the next one is
started — that is how a million-pixel panel runs without a 2 MB framebuffer.
So there is no "draw the track first" step, because there is nothing to draw it
into.

But look at the order a3d does things to each strip:

1. fill it with `ViewerConfig::background`
2. rasterize whatever 3D lands in it
3. **call your overlay function**
4. send it to the panel

By step 3, every pixel that is *still* the background colour is a pixel the
model did not cover. So the overlay can paint a background after all — it just
has to leave everything else alone:

```cpp
if (pixel == kBackdropKey) pixel = whateverTheGroundIsHere;
```

That is chroma-keying — the green-screen trick — and it costs one compare per
pixel. It has the catch every green screen has: **an actor in a green shirt
disappears.** The key here is pure magenta, which this car cannot produce
(full red *and* full blue *and* zero green). If your own model contains
saturated magenta you will see the ground straight through it, and the fix is
to change `kBackdropKey` to a colour your model does not use.

Comment out the `setOverlay()` line in lesson 2 and you get a magenta screen,
which is the clearest possible demonstration of what is going on.

## How the track works

Nothing in the track is geometry, and nothing in it is stored. Every row of
the screen asks the same two questions and gets its answer from a function of
position:

```cpp
float bendAt(float wy)          // how far the middle of the track is from straight ahead
    {
    return _bend1 * valueNoise(wy / kBend1Span)         // the long sweeps
         + _bend2 * valueNoise(wy / kBend2Span + 13.7f); // and the kinks in them
    }
```

`valueNoise` is one integer hash, eased between samples. Two octaves rather
than one because a single wave reads as a slalom and two read as a road
somebody designed.

The same idea places the trees: a grid over *world* space, one hash per cell
deciding whether there is a tree, where in the cell it stands and how big it
is. A tree that leaves the bottom of the screen and comes back an hour later
comes back in exactly the same place, and nothing was remembered to do it.

**A low-poly pine seen from above is a triangle, not a disc.** The first
attempt drew circles with a soft highlight and they read as bushes; what says
"cone" is the straight taper from a point and a hard edge down the lit side.
Each tree is two spans per row — lit face, shaded face — which is also what
lets the whole row be walked with one cursor instead of testing every pixel
against every tree.

## Why the car never moves

It sits at the same world position for the entire run, and **it does not even
steer.** The track is re-centred on the car every frame:

```cpp
const float anchor = bendAt(worldY(_carRow));   // where the track is AT THE CAR
r.centre = (int16_t)(mid + bendAt(wy) - anchor);
```

so the bends swing around the car instead of the car moving between them. What
the car *does* do is point the right way, and that is the camera:

```cpp
viewer.setOrbit(g_backdrop.headingAtCar(), kTopDownPitch, kTopDownDist);
```

The camera orbits the model, so turning its **yaw** turns the car on screen —
and the 2D track underneath does not turn with it, because it is drawn in
screen space. The car leans into the bend and the road stays put. The angle is
a numeric derivative of the same `bendAt()` that draws the tarmac, so the car
cannot point somewhere the road does not go.

## One gotcha that looks like the model vanishing

An overhead camera has to be a long way back, and a3d derives the near and
**far** planes from the *orbit* distance — even when `setCamera()` is driving.
The default limits stop at 3.2x `fitDistance()`, about 7.7 model radii here.
Ask for a camera at 15 radii without raising the limit and the car sits beyond
the far plane and is clipped away entirely:

```cpp
viewer.setDistanceLimits(0.5f, 60.0f);   // before any overhead camera
```

## The threading rule, which is easy to get wrong

`paintTile()` is called **from the tile workers**, so on a two-worker build two
threads are inside it at once, on different strips of the same frame. It
therefore only reads. Everything that changes — the scroll, the theme blend,
the row table, the tree list — is written by `advance()`, which runs on the
main task between frames when no worker is running.

Move one line from `advance()` into `paintTile()` and you get a picture that
tears between strips.

---

## What to expect on the panel

**1280x800 is 13x the pixels of a 240x320 panel, and a software rasterizer is
paid per pixel.** That is the dominant cost here, not the 500-triangle car.

**On the board this was written for, lesson 6 runs at 14 fps** — about 71 ms
a frame. [a3d's own table](../../../README.md#performance) measures **49.0 ms,
20.4 fps** on the same panel — ESP32-P4 with the JD9365 at a million pixels,
animated, two workers — for a bare 576-triangle model. So roughly 20 ms of
every frame here is what this tutorial adds on top of a3d: two full-screen
passes. Which of the two is the expensive one was measured **on a laptop, not
on the board**:

| pass | host, 1280x800 |
|---|---|
| the 2D track, trees and HUD | 3.4 ms |
| the rotation onto the portrait panel | 0.5 ms |

Those two are memory-bound walks over the same million pixels, so on a P4 they
will be a great deal slower than that in absolute terms, and the transpose is
the more cache-hostile of the two. **Do not read the host numbers as P4
times** — they are here to say which pass is the expensive one, which is the
2D track and not the rotation.

Your board prints its own figure once a second, tagged `a3d_tut01`: frames in
that second, then the last frame's time and where it went — `anim`, `skin`,
`bin`, `draw`. On a different panel or a different lesson, that line is the
number that counts.

If you want it faster, in the order that pays:

1. **Render smaller.** `kRenderW` / `kRenderH` below the panel size is the
   single biggest lever, because every cost on this page is per pixel.
2. **Rotate in the panel instead**, if your controller can (`PANEL_IS_PORTRAIT 0`).
3. **Two workers, not one.** Already on; `FreeRtosExecutor(1, ...)` shows what
   it is worth.
4. **`Shading::Unlit`**, and fewer trees — `kTreeCellPx` larger thins them out.
5. A smaller model is *last*, and on this scene it is nearly free already.

---

## Your board is not that one

You have an `esp_lcd_panel_handle_t` from your own bring-up — a vendor example,
a BSP, an LVGL port, your own code — and that is the only thing a3d wants. In
`main/main.cpp`, replace the three `board_*` calls with whatever produces your
handle and name the wait mode for your bus:

```cpp
a3d::EspLcdConfig glass;
glass.panel  = my_panel;
glass.io     = my_panel_io;     // SPI/QSPI/I80 need this; a DSI panel has none
glass.width  = 320;
glass.height = 240;
glass.wait   = a3d::EspLcdWait::IoCallback;   // DpiCallback for MIPI-DSI,
                                              // Synchronous for RGB parallel
```

Set `PANEL_IS_PORTRAIT` to 0 if your glass is already landscape, or if your
controller can rotate in a register. Delete the `EXTRA_COMPONENT_DIRS` line in
the top-level `CMakeLists.txt` and drop `p4_nano_board` from
`main/CMakeLists.txt`.

**Nothing in any of the six lessons changes** — the track, the trees, the HUD
and the camera are all sized in fractions of the render size and in model
radii, so they follow whatever panel they are given.

What the esp_lcd adapter buys you is the *wait*:
`esp_lcd_panel_draw_bitmap()` usually does not finish with your buffer before
it returns, and a send that does not wait puts half of one tile and half of the
next on the glass — which reads as a renderer bug rather than a driver one. See
[`docs/DISPLAY_ESP_IDF.md`](../../../docs/DISPLAY_ESP_IDF.md) for the long
version.

## Your model is not this one

```bash
python3 tools/a3d_export.py --check your_model.glb --target esp32p4 --viewport 1280x800
```

It writes the `.a3d` beside the `.glb`, says whether it will run and on what,
and draws a contact sheet so you can look at it before flashing. Point
`A3D_TUT_MODEL` in `main/CMakeLists.txt` at the result and fix the `asm(...)`
symbol names at the top of `main.cpp` — they are the file name with every
character that is not a letter, digit or underscore replaced by one.

> **Install Pillow first.** Without it the exporter skips the texture with a
> warning and your model imports plain white. That is how this car first came
> out, and it is easy to read as a lighting problem.

Two things to check in the export output:

- **`0 texture(s)`** when you expected one — see above.
- **`no animation in this file`** — lesson 5 and the turning wheels in lesson 6
  go quiet. Everything else still works; `clipCount()` returns 0 and every
  animation call becomes silently inert rather than an error.

---

## What is verified, and what is not

| | |
|---|---|
| Configures and builds to a flashable binary, **ESP-IDF 5.5.4, esp32p4**, zero warnings, all six lessons and both `PANEL_IS_PORTRAIT` settings | **yes** |
| Every lesson rendered at 1280x800, off-screen on a host, and looked at | **yes** — that is what the pictures on this page are |
| The rotation checked **pixel for pixel** against an independent 90-degree rotation, both directions, with tile heights that do and do not divide the screen | **yes** |
| **Flashed and run on the physical board** | **yes** — the Waveshare ESP32-P4-Nano with the 10.1" JD9365 panel it is written for |
| The frame rate above | **measured** — 14 fps for lesson 6 on that board |
| What the two full-screen passes cost on the P4 | **not measured.** The split above is from a laptop |

## Files

```
Tutorial_01/
├── low-poly_car.glb        the source model
├── low-poly_car.a3d        built from it by tools/a3d_export.py, embedded in the app
├── target_img.jpg          the composition this was built to match
├── CMakeLists.txt          borrows the board component from the example next door
├── partitions.csv
├── sdkconfig.defaults      P4 revision, PSRAM, the L2 cache settings that are worth 28%
└── main/
    ├── main.cpp            the board half - TUTORIAL_STEP, and the rotation
    ├── tutorial_steps.h    the six lessons, declared
    └── tutorial_steps.cpp  the six lessons, and the 2D track
```

## Credits

`low-poly_car.glb` is **"Low-poly Car" by [Devvux](https://sketchfab.com/Devvux),
[CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode)**.

That licence requires attribution wherever the asset is distributed, and this
project links the model into the firmware image — so if you ship something
built from this directory, the obligation comes with it.
[`ATTRIBUTION.md`](ATTRIBUTION.md) has the details, and shows how to read the
same information out of any `.glb` you are handed.
