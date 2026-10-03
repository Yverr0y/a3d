// SPDX-FileCopyrightText: 2026 Eric Nam
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tutorial_steps.cpp
 * @brief The six lessons. No vendor header appears in this file.
 *
 * READING ORDER
 *
 *   Skip to "LESSON 1" and read down. Everything above it is the 2D track the
 *   car drives on, and it will make more sense once you have seen who calls it.
 *
 * EVERYTHING HERE IS LANDSCAPE
 *
 *   This file only ever knows one coordinate system: a wide one, 1280x800 on
 *   the board it was written for. The panel it ends up on scans PORTRAIT, and
 *   not one line below cares - main.cpp turns the picture a quarter turn on
 *   its way to the glass. See "THE PANEL IS SIDEWAYS" in main.cpp for how, and
 *   for why that is a3d's design working rather than a workaround.
 *
 * THE ONE TRICK IN THIS FILE, AND IT IS WORTH KNOWING BEFORE ANYTHING ELSE
 *
 *   a3d has no "draw this behind the model" hook, and it cannot have one: it
 *   never holds a whole frame. The screen is cut into horizontal strips, and a
 *   strip is cleared, drawn into and pushed to the panel before the next one is
 *   started. There is no frame buffer to paint a track into.
 *
 *   But look at what the viewer does to each strip, in order:
 *
 *     1. fill the strip with ViewerConfig::background
 *     2. rasterize whatever 3D lands in it
 *     3. call your overlay function
 *     4. send it to the panel
 *
 *   By the time step 3 runs, every pixel that is STILL the background colour is
 *   a pixel the model did not cover. So the overlay can paint a background
 *   after all - it just has to leave everything else alone:
 *
 *       if (pixel == kBackdropKey) pixel = whateverTheGroundIsHere;
 *
 *   That is chroma-keying, the green-screen trick, and it costs one compare per
 *   pixel. The catch is the one every green screen has: an actor in a green
 *   shirt disappears. So the key has to be a colour the model cannot produce -
 *   see kBackdropKey for why pure magenta is safe for this car and what to do
 *   if your own model is magenta.
 */

#include "tutorial_steps.h"

#include "backends/soft/a3d_canvas.h"

#include <math.h>
#include <stdint.h>
#include <stddef.h>

namespace tut {
namespace {

// ===========================================================================
// SETTINGS
//
// Everything in this block is a decision, and every one of them is safe to
// change. Everything below it is machinery.
// ===========================================================================

// --- lesson 1: the empty stage ---------------------------------------------
constexpr uint16_t kPlainBackground = a3d::rgb565(16, 18, 26);   // near-black

// --- the chroma key --------------------------------------------------------
//
// WHY MAGENTA, AND WHEN IT WOULD BE THE WRONG CHOICE.
//
//   This is the colour that means "nothing was drawn here". It must be a
//   colour the rasterizer cannot produce for this model, because any pixel of
//   the car that happens to land on it gets a hole punched in it.
//
//   Pure magenta is r=31, g=0, b=31 in RGB565: full red AND full blue AND zero
//   green. Lighting scales all three channels together, so a lit surface
//   reaches full red only where it also has some green. This car is red, dark
//   blue, grey and black - none of it can get here.
//
//   If YOUR model does contain saturated magenta, the symptom is unmistakable:
//   the ground shows through the model in exactly the magenta parts. Pick
//   another colour the model does not use and change this one line.
constexpr uint16_t kBackdropKey = a3d::rgb565Raw(31, 0, 31);

// --- lesson 3: the light ---------------------------------------------------
//
// A DIRECTION, not a position: this is sunlight, so only which way it travels
// matters. It points FROM the light TOWARDS the scene, which is why y is
// negative - the light comes from above and falls on the car.
//
// Aimed for a camera that is nearly overhead, so the roof of the car is the
// lit face and the model reads against the track.
constexpr float kLightX = -0.35f;
constexpr float kLightY = -0.86f;
constexpr float kLightZ = -0.38f;

// --- the camera ------------------------------------------------------------
constexpr float kCameraFovDegrees = 45.0f;   // 30 is a long lens, 70 is wide
constexpr float kPresetSeconds    = 3.5f;    // how long each camera view holds

// Looking down at the track. 1.5708 (90 degrees) is straight down, which shows
// only the roof; backing off to 1.18 keeps a little of the car's side and is
// what the reference picture has.
constexpr float kTopDownPitch = 1.18f;
constexpr float kTopDownDist  = 15.0f;   // in MODEL RADII. See setDistanceLimits()
constexpr float kStillPitch   = 1.02f;   // lessons 2-5, which do not scroll

// --- lesson 6: the endless track -------------------------------------------
constexpr float kScrollPxPerSec = 520.0f;  // how fast the world slides past
constexpr float kWheelSpeed     = 2.2f;    // animation rate while driving
constexpr float kThemeSeconds   = 11.0f;   // how long a colour theme lasts
constexpr float kThemeFadeSecs  = 2.5f;    // and how long it takes to change
constexpr float kLapSeconds     = 20.0f;   // when the orange timer restarts

// The track, as fractions of the render width so a different panel keeps the
// same proportions.
constexpr float kTrackHalfFrac = 0.170f;   // tarmac, centre to edge
constexpr float kKerbFrac      = 0.022f;   // the red-and-white strip outside it
constexpr float kDashHalfFrac  = 0.008f;   // centre dashes

// How much the track snakes, and over what distance. Two octaves, because one
// sine reads as a slalom and two reads as a road somebody designed.
constexpr float kBend1Frac = 0.150f;   // of the render width
constexpr float kBend1Span = 560.0f;   // pixels of track per wiggle
constexpr float kBend2Frac = 0.052f;
constexpr float kBend2Span = 205.0f;

constexpr float kKerbBlockPx = 46.0f;   // one red or one white block
constexpr float kGrassBandPx = 68.0f;   // the mown stripes, which show speed
constexpr float kDashCyclePx = 132.0f;  // dash plus gap

// Trees. Scattered on a grid in WORLD space and jittered, never on the track.
constexpr float kTreeCellPx   = 186.0f;  // one candidate per cell of this size
constexpr float kTreeRadFrac  = 0.026f;  // base radius, of the render width
constexpr float kTreeTallness = 2.6f;    // height as a multiple of that radius
constexpr float kTreeClearPx  = 30.0f;   // keep this far off the kerb
constexpr int   kMaxTrees     = 112;

// ===========================================================================
// MACHINERY: a tiny colour type, so themes can be blended
//
// Blending two RGB565 values means unpacking them anyway, so the themes are
// stored as plain 8-bit RGB and packed once per frame.
// ===========================================================================

struct Rgb { uint8_t r, g, b; };

inline uint16_t pack(const Rgb& c) { return a3d::rgb565(c.r, c.g, c.b); }

inline Rgb mix(const Rgb& a, const Rgb& b, float t)
    {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const float u = 1.0f - t;
    return Rgb{ (uint8_t)(a.r * u + b.r * t + 0.5f),
                (uint8_t)(a.g * u + b.g * t + 0.5f),
                (uint8_t)(a.b * u + b.b * t + 0.5f) };
    }

struct Theme
    {
    const char* name;
    Rgb groundA, groundB;    ///< the two mown stripes either side of the track
    Rgb tarmacA, tarmacB;    ///< two shades of surface, same reason
    Rgb kerbA,   kerbB;      ///< the alternating blocks. Red and white, usually
    Rgb dash;                ///< centre line
    Rgb treeDark, treeLight; ///< the shaded and lit sides of a cone
    };

// Six, cycled in order. Add one and it joins the rotation; nothing else needs
// to change.
const Theme kThemes[] =
    {
    { "circuit",
      {  58, 142,  56 }, {  54, 134,  53 },   // grass, and its mown stripe
      { 138, 126, 130 }, { 134, 122, 126 },   // tarmac
      { 214,  58,  54 }, { 240, 240, 238 },   // kerb
      { 246, 246, 240 },                      // dash
      {  30,  88,  42 }, {  74, 150,  66 } }, // trees: shaded, lit

    { "dusk",
      {  62,  92,  64 }, {  57,  86,  60 },
      { 104,  92, 104 }, { 100,  88, 100 },
      { 198,  76,  70 }, { 226, 206, 190 },
      { 240, 214, 168 },
      {  28,  50,  42 }, {  66,  96,  64 } },

    { "night",
      {  22,  46,  32 }, {  19,  41,  29 },
      {  56,  56,  66 }, {  53,  53,  63 },
      { 160,  52,  54 }, { 196, 200, 214 },
      { 238, 230, 168 },
      {  13,  30,  25 }, {  38,  72,  50 } },

    { "desert",
      { 206, 174, 110 }, { 199, 167, 105 },
      { 124, 116, 112 }, { 120, 112, 108 },
      { 208,  84,  56 }, { 244, 238, 224 },
      { 250, 244, 210 },
      {  92, 112,  60 }, { 146, 166,  92 } },

    { "snow",
      { 226, 232, 240 }, { 218, 225, 235 },
      { 110, 112, 122 }, { 106, 108, 118 },
      { 200,  62,  58 }, { 250, 250, 252 },
      { 250, 250, 252 },
      {  34,  70,  56 }, {  82, 122,  88 } },

    { "neon",
      {  26,  14,  48 }, {  23,  12,  44 },
      {  46,  30,  66 }, {  43,  28,  62 },
      { 255,  62, 160 }, {  92, 248, 226 },
      {  92, 248, 226 },
      {  20,  52,  66 }, {  52, 132, 144 } },
    };
constexpr int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

// ===========================================================================
// MACHINERY: the shape of the track
//
// Not an array that was filled in at start-up - a FUNCTION of position. That
// is what makes it endless: there is no end of a buffer to wrap around at, and
// driving for an hour costs exactly what driving for a second does.
// ===========================================================================

/** One round of a cheap integer hash. Same input, same output, forever. */
inline uint32_t hash32(uint32_t x)
    {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
    }

/** -1 .. +1 from an integer. */
inline float hashSigned(int i)
    { return (float)(hash32((uint32_t)i) & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f; }

/**
 * Smooth noise: hash at every integer, eased between them.
 *
 * Smoothstep rather than a straight ramp, because a straight ramp gives the
 * track a visible corner at every sample and it reads as a folded paper strip
 * rather than as a bend.
 */
inline float valueNoise(float t)
    {
    const float fl = floorf(t);
    const int   i  = (int)fl;
    const float f  = t - fl;
    const float s  = f * f * (3.0f - 2.0f * f);
    const float a  = hashSigned(i);
    const float b  = hashSigned(i + 1);
    return a + (b - a) * s;
    }

// ===========================================================================
// MACHINERY: the backdrop
//
// One painter, two behaviours. Lessons 2 to 5 leave it still; lesson 6 calls
// advance() every frame and the world slides past.
//
// THREADING, WHICH IS EASY TO GET WRONG HERE
//
//   paintTile() is called from the tile workers, so on a two-worker build TWO
//   THREADS ARE INSIDE IT AT ONCE, on different strips of the same frame. It
//   therefore only reads. Everything that changes - the scroll, the theme
//   blend, the row table, the tree list - is written by advance(), which runs
//   on the main task between frames, when no worker is running. Move one line
//   from advance() into paintTile() and you get a picture that tears between
//   strips.
// ===========================================================================

constexpr int kMaxRenderW = 1536;   // the widest panel this is sized for
constexpr int kMaxRenderH = 1024;

/** What every pixel of one screen row needs, worked out once a frame. */
struct RowInfo
    {
    int16_t centre;    ///< where the middle of the track is on this row
    uint8_t kerb;      ///< which of the two kerb colours this row is in
    uint8_t band;      ///< which of the two ground/tarmac shades
    uint8_t dash;      ///< is a centre dash present on this row
    uint8_t pad;
    };

struct Tree
    {
    int16_t x, y;      ///< screen position of the top of the cone
    int16_t r;         ///< radius of the base
    };

class Backdrop
    {
    public:

        void configure(int renderW, int renderH)
            {
            _w = (renderW < kMaxRenderW) ? renderW : kMaxRenderW;
            _h = (renderH < kMaxRenderH) ? renderH : kMaxRenderH;

            _trackHalf = _w * kTrackHalfFrac;
            _kerbOuter = _trackHalf + _w * kKerbFrac;
            _dashHalf  = _w * kDashHalfFrac;
            _bend1     = _w * kBend1Frac;
            _bend2     = _w * kBend2Frac;
            _treeRad   = _w * kTreeRadFrac;

            _themeIndex = 0;
            _themeAge   = 0.0f;
            _scroll     = 0.0f;
            _running    = false;
            _carRow     = _h / 2;

            rebuild();
            }

        void setRunning(bool run)        { _running = run; }
        void setCarRow(int y)            { _carRow = y; }
        void setTheme(int i)
            {
            _themeIndex = ((i % kThemeCount) + kThemeCount) % kThemeCount;
            _themeAge = 0.0f;
            rebuild();
            }

        const char* themeName() const { return kThemes[_themeIndex].name; }

        /**
         * Which way the track is heading where the car is, in radians, with 0
         * meaning straight up the screen.
         *
         * Lesson 6 turns the camera by this, which is what makes the car lean
         * into the bends. It is a plain numeric derivative of the same
         * function that draws the track, so the two cannot disagree.
         */
        float headingAtCar() const
            {
            const float wy = worldY(_carRow);
            const float d  = 44.0f;
            const float dx = bendAt(wy - d) - bendAt(wy + d);
            return atanf(dx / (2.0f * d));
            }

        /** Main task only, between frames. Never from paintTile(). */
        void advance(float dtMs)
            {
            if (!_running) return;
            const float dt = dtMs * 0.001f;

            _scroll += kScrollPxPerSec * dt;

            _themeAge += dt;
            if (_themeAge >= kThemeSeconds)
                {
                _themeAge -= kThemeSeconds;
                _themeIndex = (_themeIndex + 1) % kThemeCount;
                }
            rebuild();
            }

        /** Four short strings in the corners. nullptr draws nothing. */
        void setHud(const char* timeBig, const char* timeSmall,
                    const char* line1, const char* line2)
            { _hudBig = timeBig; _hudSmall = timeSmall; _hudL1 = line1; _hudL2 = line2; }

        /**
         * Paint one strip, behind whatever the rasterizer already put in it.
         *
         * `tileTop` is the screen row of the strip's first row, so a thing at
         * absolute screen row y is drawn at row (y - tileTop) here. Rows
         * outside the strip are simply not visited - there is no clipping to
         * do, because the loop never leaves the strip.
         */
        void paintTile(uint16_t* tile, int tileTop, int rows, int width) const
            {
            for (int row = 0; row < rows; row++)
                {
                const int y = tileTop + row;
                if ((y < 0) || (y >= _h)) continue;
                paintRow(tile + (size_t)row * width, width, y);
                }
            paintHud(tile, tileTop, rows, width);
            }

        /** The a3d::Viewer overlay signature. `user` is the Backdrop. */
        static void overlay(void* user, uint16_t* tile, int tileTop,
                            int rows, int width, int /*height*/)
            { ((const Backdrop*)user)->paintTile(tile, tileTop, rows, width); }

    private:

        // -------------------------------------------------------------------
        // where the track is
        // -------------------------------------------------------------------

        /** World position of a screen row. Growing `_scroll` slides the world
            DOWN the screen, which is the car moving up it. */
        float worldY(int y) const { return (float)y - _scroll; }

        /** How far the track's middle is from straight ahead, in pixels. */
        float bendAt(float wy) const
            {
            return _bend1 * valueNoise(wy / kBend1Span)
                 + _bend2 * valueNoise(wy / kBend2Span + 13.7f);
            }

        // -------------------------------------------------------------------
        // one row
        // -------------------------------------------------------------------

        void paintRow(uint16_t* px, int width, int y) const
            {
            const RowInfo& ri = _rows[y];

            const uint16_t ground = ri.band ? _groundB : _groundA;
            const uint16_t tarmac = ri.band ? _tarmacB : _tarmacA;
            const uint16_t kerb   = ri.kerb ? _kerbB   : _kerbA;

            const int cx        = ri.centre;
            const int trackHalf = (int)_trackHalf;
            const int kerbOuter = (int)_kerbOuter;
            const int dashHalf  = (int)_dashHalf;

            // The trees that reach this row, as spans that do not overlap each
            // other. Built here rather than tested per pixel: the inner loop
            // then costs one compare in the common case.
            Span spans[kMaxSpans];
            const int nSpans = collectSpans(y, spans);

            int si = 0;
            for (int x = 0; x < width; x++)
                {
                while ((si < nSpans) && (x > spans[si].x1)) si++;

                if (px[x] != kBackdropKey) continue;   // the car is here; leave it

                if ((si < nSpans) && (x >= spans[si].x0)) { px[x] = spans[si].colour; continue; }

                const int adx = (x > cx) ? (x - cx) : (cx - x);
                uint16_t c;
                if      (adx <= dashHalf && ri.dash) c = _dash;
                else if (adx <= trackHalf)           c = tarmac;
                else if (adx <= kerbOuter)           c = kerb;
                else                                 c = ground;
                px[x] = c;
                }
            }

        // -------------------------------------------------------------------
        // trees, as spans
        // -------------------------------------------------------------------

        static constexpr int kMaxSpans = 20;
        struct Span { int16_t x0, x1; uint16_t colour; };

        /**
         * Every span this row needs, left to right and never overlapping.
         *
         * A LOW-POLY PINE FROM ABOVE IS A TRIANGLE, NOT A DISC. Drawing it as
         * a circle was the first attempt and it read as a bush: what says
         * "cone" is the straight taper from a point, and what says "lit from
         * over there" is a hard edge down it rather than a soft blob. So each
         * tree is two spans on every row - the lit face and the shaded one -
         * split at a fixed fraction across, and the width grows linearly from
         * the apex.
         *
         * Two spans that meet exactly, rather than two overlapping shapes, is
         * also what lets the caller walk the whole row with one cursor.
         */
        int collectSpans(int y, Span* out) const
            {
            int n = 0;

            for (int t = 0; (t < _nbTrees) && (n + 2 <= kMaxSpans); t++)
                {
                const Tree& tr = _trees[t];
                const int apex = tr.y;                 // the tip of the cone
                const int base = tr.y + (int)(tr.r * kTreeTallness);
                if ((y < apex) || (y > base)) continue;

                const int hw = (int)((float)tr.r * (float)(y - apex)
                                                 / (float)(base - apex));
                if (hw <= 0) continue;

                const int x0 = tr.x - hw, x1 = tr.x + hw;
                // The shade line sits left of centre, so the lit face is the
                // narrower one - which is what a cone lit from the upper left
                // actually looks like from above.
                const int split = tr.x - (int)(hw * 0.18f);

                out[n++] = Span{ (int16_t)x0,           (int16_t)split, _treeLight };
                out[n++] = Span{ (int16_t)(split + 1),  (int16_t)x1,    _treeDark  };
                }

            // Insertion sort by left edge. n is at most twenty and usually
            // under six, so this is cheaper than keeping anything ordered.
            for (int i = 1; i < n; i++)
                {
                const Span v = out[i];
                int j = i - 1;
                while ((j >= 0) && (out[j].x0 > v.x0)) { out[j + 1] = out[j]; j--; }
                out[j + 1] = v;
                }
            return n;
            }

        // -------------------------------------------------------------------
        // the HUD, which is NOT chroma-keyed
        //
        // It goes on top of everything, model included, so it draws straight
        // through the canvas with no key test. That contrast with the two
        // functions above is worth noticing.
        // -------------------------------------------------------------------

        void paintHud(uint16_t* tile, int tileTop, int rows, int width) const
            {
            if ((_hudBig == nullptr) && (_hudL1 == nullptr)) return;
            a3d::Canvas c(tile, width, rows, width);

            const int pad = _w / 48;
            if (_hudBig)   c.text(pad, 34 - tileTop, _hudBig,   a3d::kWhite, 1.0f, 3);
            if (_hudSmall) c.text(pad, 62 - tileTop, _hudSmall, _hudAmber,   1.0f, 2);
            if (_hudL1)    c.text(pad, _h - 34 - tileTop, _hudL1, a3d::kWhite, 0.72f, 2);
            if (_hudL2)    c.text(pad, _h - 12 - tileTop, _hudL2, a3d::kWhite, 0.72f, 2);
            }

        // -------------------------------------------------------------------
        // once a frame
        // -------------------------------------------------------------------

        void rebuild()
            {
            bakeColours();
            bakeRows();
            bakeTrees();
            }

        void bakeColours()
            {
            const Theme& a = kThemes[_themeIndex];
            const Theme& b = kThemes[(_themeIndex + 1) % kThemeCount];

            // Hold the theme, then cross-fade into the next over the last
            // kThemeFadeSecs of its life. A hard cut is visible and cheap; a
            // fade is barely more code and looks like the weather changing.
            float t = 0.0f;
            if (_running && (_themeAge > kThemeSeconds - kThemeFadeSecs))
                t = (_themeAge - (kThemeSeconds - kThemeFadeSecs)) / kThemeFadeSecs;

            _groundA   = pack(mix(a.groundA,   b.groundA,   t));
            _groundB   = pack(mix(a.groundB,   b.groundB,   t));
            _tarmacA   = pack(mix(a.tarmacA,   b.tarmacA,   t));
            _tarmacB   = pack(mix(a.tarmacB,   b.tarmacB,   t));
            _kerbA     = pack(mix(a.kerbA,     b.kerbA,     t));
            _kerbB     = pack(mix(a.kerbB,     b.kerbB,     t));
            _dash      = pack(mix(a.dash,      b.dash,      t));
            _treeDark  = pack(mix(a.treeDark,  b.treeDark,  t));
            _treeLight = pack(mix(a.treeLight, b.treeLight, t));

            _hudAmber = a3d::rgb565(255, 176, 64);
            }

        /**
         * The track, one row at a time.
         *
         * THE RE-CENTRING IS THE WHOLE TRICK. bendAt() is subtracted at the
         * CAR'S row as well as read at this one, so the track is always
         * exactly centred where the car is. The car therefore never has to
         * steer to stay on it - the road swings around the car instead, which
         * is what an endless runner is.
         */
        void bakeRows()
            {
            const float anchor = bendAt(worldY(_carRow));
            const float mid    = (float)_w * 0.5f;

            for (int y = 0; y < _h; y++)
                {
                const float wy = worldY(y);
                RowInfo& r = _rows[y];
                r.centre = (int16_t)(mid + bendAt(wy) - anchor);
                r.kerb   = (uint8_t)(((int)floorf(wy / kKerbBlockPx)) & 1);
                r.band   = (uint8_t)(((int)floorf(wy / kGrassBandPx)) & 1);

                float ph = wy / kDashCyclePx;
                ph -= floorf(ph);
                r.dash = (uint8_t)((ph < 0.5f) ? 1 : 0);
                r.pad  = 0;
                }
            }

        /**
         * Trees, from a grid in WORLD space rather than a list that scrolls.
         *
         * Each cell either has one or does not, and where it is and how big
         * are all read out of one hash of the cell's coordinates - so a tree
         * that leaves the bottom of the screen and comes back an hour later
         * comes back in exactly the same place, with no memory spent on it.
         */
        void bakeTrees()
            {
            _nbTrees = 0;

            const float anchor = bendAt(worldY(_carRow));
            const float mid    = (float)_w * 0.5f;
            const float margin = _treeRad + 4.0f;

            const int gy0 = (int)floorf((worldY(0)          - margin) / kTreeCellPx);
            const int gy1 = (int)floorf((worldY(_h - 1)     + margin) / kTreeCellPx);
            // World x is screen x minus the same re-centring the track gets.
            const float shift = mid - anchor;
            const int gx0 = (int)floorf((0.0f - shift - margin) / kTreeCellPx);
            const int gx1 = (int)floorf(((float)_w - shift + margin) / kTreeCellPx);

            for (int gy = gy0; gy <= gy1; gy++)
                for (int gx = gx0; gx <= gx1; gx++)
                    {
                    if (_nbTrees >= kMaxTrees) return;

                    const uint32_t h = hash32((uint32_t)(gx * 73856093) ^
                                              (uint32_t)(gy * 19349663));
                    if ((h & 7u) < 3u) continue;          // not every cell has one

                    const float jx = (float)((h >> 3) & 255u) * (1.0f / 255.0f);
                    const float jy = (float)((h >> 11) & 255u) * (1.0f / 255.0f);
                    const float sz = 0.62f + 0.55f * (float)((h >> 19) & 255u) * (1.0f / 255.0f);

                    const float wx = ((float)gx + jx) * kTreeCellPx;
                    const float wy = ((float)gy + jy) * kTreeCellPx;
                    const float r  = _treeRad * sz;

                    // Never on the track. The test is against the same bendAt()
                    // the tarmac is drawn from, so a tree cannot end up in the
                    // road however the bends are retuned.
                    const float off = wx - bendAt(wy);
                    const float keep = _kerbOuter + kTreeClearPx + r;
                    if ((off > -keep) && (off < keep)) continue;

                    const int sx = (int)(wx + shift);
                    const int sy = (int)(wy + _scroll);
                    if ((sx < -r - 2) || (sx > _w + r + 2)) continue;

                    Tree& t = _trees[_nbTrees++];
                    t.x = (int16_t)sx;
                    t.y = (int16_t)sy;
                    t.r = (int16_t)((r < 2.0f) ? 2 : r);
                    }
            }

        int _w = 0, _h = 0;
        bool _running = false;
        int _carRow = 0;

        float _trackHalf = 0, _kerbOuter = 0, _dashHalf = 0;
        float _bend1 = 0, _bend2 = 0, _treeRad = 0;

        int   _themeIndex = 0;
        float _themeAge = 0.0f;
        float _scroll = 0.0f;

        uint16_t _groundA = 0, _groundB = 0;
        uint16_t _tarmacA = 0, _tarmacB = 0;
        uint16_t _kerbA = 0, _kerbB = 0;
        uint16_t _dash = 0;
        uint16_t _treeDark = 0, _treeLight = 0;
        uint16_t _hudAmber = 0;

        RowInfo _rows[kMaxRenderH] = {};
        Tree    _trees[kMaxTrees]  = {};
        int     _nbTrees = 0;

        const char* _hudBig   = nullptr;
        const char* _hudSmall = nullptr;
        const char* _hudL1    = nullptr;
        const char* _hudL2    = nullptr;
    };

Backdrop g_backdrop;
int g_screenW = 0, g_screenH = 0;

// ===========================================================================
// MACHINERY: where the car is
// ===========================================================================

/**
 * The model's centre and radius, which a3d works out from the model itself.
 *
 * NOT AVAILABLE UNTIL THE FIRST frame() HAS RUN. For a container the bounds
 * are read from the posed vertices, which do not exist until something has
 * posed them. Every caller here checks stats().frames first and leaves the
 * default camera alone until then - which costs one frame at boot and saves a
 * camera silently framing a model of radius 1.0.
 */
bool carReady(const a3d::Viewer& viewer)
    { return (viewer.stats().frames > 0) && (viewer.modelRadius() > 1e-4f); }

/**
 * Let the camera go far enough away to look down on the track.
 *
 * THIS IS NOT OPTIONAL, AND LEAVING IT OUT LOOKS LIKE THE MODEL VANISHING.
 * a3d derives the near and FAR planes from the ORBIT distance - even when
 * setCamera() is driving - and the default distance limits stop at 3.2x
 * fitDistance(), which on this viewport is about 7.7 model radii. Ask for an
 * overhead camera at 13 radii without raising the limit and the car sits
 * beyond the far plane and is clipped away entirely.
 */
void allowDistantCamera(a3d::Viewer& viewer)
    { viewer.setDistanceLimits(0.5f, 60.0f); }

} // namespace

// ===========================================================================
//
//  LESSON 1 - PUT THE OBJECT ON THE SCREEN
//
//  There is almost nothing here, and that is the lesson. main.cpp has already
//  done the two calls that matter:
//
//      viewer.begin(panel, cfg);
//      viewer.openAsset(car_start, car_end - car_start);
//
//  and from that point a3d has framed the camera on the model's own bounding
//  box and is ready to draw. This function only says what the empty space
//  around it should look like.
//
// ===========================================================================
void step1_putTheObjectOnScreen(a3d::Viewer& viewer)
    {
    // Every strip is cleared to this before anything is drawn into it.
    viewer.setBackground(kPlainBackground);

    // No overlay yet. Passing nullptr here matters, because the lessons build
    // on each other and this is also how you turn one off again.
    viewer.setOverlay(nullptr, nullptr);

    // Point the camera at the model and back off until all of it is in frame.
    // a3d already did this when the model was opened; the call is here so you
    // can see it, and so you have somewhere to put the margin.
    //
    // TRY THIS: 1.0 fits the model exactly - it will touch the edge of the
    // screen. 2.5 leaves it small in the middle of a lot of nothing.
    viewer.frameModel(1.35f);

    // TRY THIS: the model with no lighting at all, flat texture colours.
    //           Useful when you cannot tell whether a dark model is unlit or
    //           simply not there.
    // viewer.setShading(a3d::Shading::Unlit, a3d::TextureMode::Perspective);

    // TRY THIS: the model with no texture, one solid colour, lighting only.
    // viewer.setShading(a3d::Shading::Gouraud, a3d::TextureMode::None);
    // viewer.setMaterialColor(0.85f, 0.15f, 0.15f);
    }

// ===========================================================================
//
//  LESSON 2 - GIVE IT SOMEWHERE TO BE
//
//  A model on black looks like a model. The same model on a road looks like a
//  thing in a place, and it costs one function pointer.
//
//  Read the note at the top of this file first if you have not: the overlay
//  runs AFTER the 3D, so the only way to get behind the model is to repaint
//  the pixels the model did not touch.
//
// ===========================================================================
void step2_makeABackground(a3d::Viewer& viewer, int screenW, int screenH)
    {
    step1_putTheObjectOnScreen(viewer);   // everything lesson 1 set up

    g_screenW = screenW;
    g_screenH = screenH;
    g_backdrop.configure(screenW, screenH);
    g_backdrop.setRunning(false);         // still: no scroll, no theme change
    g_backdrop.setHud(nullptr, nullptr, nullptr, nullptr);
    g_backdrop.setTheme(0);

    // THE TWO LINES THAT MATTER.
    //
    // The background colour stops being decoration and becomes a signal: it is
    // now "no 3D here", and the overlay is what decides what that means.
    viewer.setBackground(kBackdropKey);
    viewer.setOverlay(&Backdrop::overlay, &g_backdrop);

    // Look down at it, so the flat 2D track below reads as ground rather than
    // as a poster standing behind the car.
    allowDistantCamera(viewer);
    viewer.setFieldOfView(kCameraFovDegrees);
    viewer.setOrbit(0.0f, kStillPitch, kTopDownDist);

    // TRY THIS: any of the six themes, by index - "circuit", "dusk", "night",
    //           "desert", "snow", "neon". Lesson 6 cycles them on its own.
    // g_backdrop.setTheme(2);   // night

    // TRY THIS: comment out the setOverlay() line above and reflash. You get
    //           a magenta screen - which is the clearest possible proof of
    //           what the overlay is actually doing.

    // WHY THE CAR IS FLOATING ABOVE THE TRACK, which it is, and which is not a
    // bug in this lesson.
    //
    // The backdrop is a PICTURE. It knows nothing about the model, and nothing
    // has told the camera where the road is. Two things fix that and both
    // arrive in lesson 6: a camera aimed straight down at the car, and a
    // track that is re-centred under it every frame. Until then the car is
    // parked wherever a3d happened to frame it, which is worth looking at so
    // that the fix means something.
    }

// ===========================================================================
//
//  LESSON 3 - LIGHT IT
//
//  Three separate things decide how a surface ends up looking, and mixing
//  them up is the usual reason a model comes out flat or black:
//
//    THE SHADING MODEL   whether lighting is computed at all
//    THE LIGHT           which way it comes from
//    THE MATERIAL        how the surface answers it
//
//  This car carries its own texture and its own material inside the .a3d
//  container, so the material calls below are commented out - they would do
//  nothing. They are here because the moment you draw a cube, an STL or
//  anything else untextured, they are the three numbers you will want.
//
// ===========================================================================
void step3_addLight(a3d::Viewer& viewer, int screenW, int screenH)
    {
    step2_makeABackground(viewer, screenW, screenH);

    // 1. THE SHADING MODEL.
    //
    //    Gouraud lights every vertex and interpolates across the triangle,
    //    which is what gives a low-poly model its soft faceted look.
    //    Perspective is the texture mapping; on a surface at a steep angle
    //    Affine visibly warps, and it is the faster of the two.
    viewer.setShading(a3d::Shading::Gouraud, a3d::TextureMode::Perspective);

    // 2. THE LIGHT. One directional light, like the sun. Mostly from above,
    //    because the camera is mostly above - light the face you can see.
    viewer.setLightDirection(kLightX, kLightY, kLightZ);

    // 3. THE MATERIAL - for geometry that carries none. A .a3d container
    //    brings its own, so these are inert for this car.
    //
    //    AMBIENT is the floor: what a surface facing away from the light still
    //    shows. At 0 the dark side is pure black and the shape loses its
    //    outline. DIFFUSE is what the light adds. SPECULAR is the highlight
    //    and the exponent is how tight it is - a low one is a broad sheen, a
    //    high one a small hard dot, and 0 turns it off.
    //
    // a3d::Material m;
    // m.color[0] = 0.85f; m.color[1] = 0.16f; m.color[2] = 0.14f;
    // m.ambient = 0.28f;
    // m.diffuse = 0.80f;
    // m.specular = 0.45f;
    // m.specularExponent = 24;
    // viewer.setMaterial(m);

    // TRY THIS: light from below. It looks wrong in a way that is worth seeing
    //           once - it is how you learn to recognise a sign error in a
    //           light direction.
    // viewer.setLightDirection(0.35f, 0.86f, 0.38f);

    // TRY THIS: turn lighting off and compare. Unlit is also measurably
    //           faster, and for a texture-heavy model it is sometimes enough.
    // viewer.setShading(a3d::Shading::Unlit, a3d::TextureMode::Perspective);
    }

// ===========================================================================
//
//  LESSON 4 - MOVE THE CAMERA
//
//  a3d gives you two ways to aim, and they are not alternatives so much as
//  two different jobs:
//
//    THE ORBIT     yaw, pitch, distance, around the model's own centre. This
//                  is what you want for "let me look at this thing" - it
//                  cannot be pointed at nothing.
//
//    setCamera()   an explicit eye, target and up. This is what you want when
//                  the camera has a job of its own: following, framing a shot,
//                  putting the subject somewhere other than the middle.
//
//  This lesson cycles four views so you can see all of it in one flash.
//
// ===========================================================================
namespace {
float g_presetClock = 0.0f;
int   g_preset = 0;
}

void step4_changeTheCameraView(a3d::Viewer& viewer, int screenW, int screenH)
    {
    step3_addLight(viewer, screenW, screenH);

    // How wide the lens is. Narrow flattens the model and makes distances hard
    // to read; wide exaggerates them and bends the edges of the frame.
    //
    // TRY THIS: 28 for a long lens, 75 for a wide one.
    viewer.setFieldOfView(kCameraFovDegrees);

    // How far the orbit may tilt, in radians. The default stops just short of
    // straight up, where the view direction and the up vector become parallel
    // and the picture rolls over. An overhead camera lives near that edge, so
    // this is the one lesson that has to think about it.
    viewer.setPitchLimits(-0.20f, 1.40f);

    g_presetClock = 0.0f;
    g_preset = 0;
    }

void step4_changeTheCameraView_update(a3d::Viewer& viewer, float dtMs)
    {
    g_presetClock += dtMs * 0.001f;
    if (g_presetClock >= kPresetSeconds)
        {
        g_presetClock -= kPresetSeconds;
        g_preset = (g_preset + 1) & 3;
        viewer.clearCamera();   // undo preset 3, whichever preset comes next
        }

    switch (g_preset)
        {
        case 0:
            // OVERHEAD. Yaw and pitch in radians, distance in MODEL RADII - so
            // the same three numbers frame any model sensibly, whatever units
            // it was modelled in.
            viewer.setOrbit(0.0f, kTopDownPitch, kTopDownDist);
            break;

        case 1:
            // LOWER AND CLOSER, three-quarters from behind. Yaw 0 is directly
            // behind this car; 3.14 is head-on at the grille.
            //
            // WHY 5.5 AND NOT 2.5, which is the number you would guess. The
            // distance that exactly fits the model is fitDistance(), and it
            // depends on the ASPECT RATIO because a3d fits the narrower of the
            // two half-angles. Print viewer.fitDistance() once and the
            // guessing stops.
            viewer.setOrbit(0.45f, 0.55f, 5.5f);
            break;

        case 2:
            // TURNING. One call a frame, scaled by dt so the speed does not
            // change when the frame rate does.
            viewer.orbit(dtMs * 0.0011f, 0.0f);
            break;

        case 3:
            {
            // THE EXPLICIT CAMERA. Everything the orbit does for you, done by
            // hand - and the only way to aim at something that is NOT the
            // model's centre, which is how you put a subject off to one side.
            if (!carReady(viewer)) break;
            const float* c = viewer.modelCentre();
            const float  r = viewer.modelRadius();

            const float dist = r * 7.0f;
            const float eye[3]    = { c[0] - dist * 0.42f,
                                      c[1] + dist * 0.72f,
                                      c[2] + dist * 0.55f };
            // Aimed a little to one side of the car rather than at it, so the
            // car sits off centre. The orbit cannot do this at all.
            const float target[3] = { c[0] + r * 1.4f, c[1], c[2] };
            const float up[3]     = { 0.0f, 1.0f, 0.0f };
            viewer.setCamera(eye, target, up);
            break;
            }
        default: break;
        }
    }

// ===========================================================================
//
//  LESSON 5 - PLAY THE CLIP THAT IS INSIDE THE MODEL
//
//  The animation is not something this program adds. It came out of the .glb,
//  it is stored in the .a3d container, and a3d has been playing it since
//  lesson 1 - openAsset() selects clip 0 and starts it. Everything here is
//  about CONTROLLING a clip that is already running.
//
//  The car's clip is 2000 ms long and turns the wheels. It is subtle at a
//  standstill and obvious once lesson 6 puts a moving road under it.
//
// ===========================================================================
namespace {
float g_animClock = 0.0f;
int   g_animPhase = 0;
}

void step5_playTheAnimation(a3d::Viewer& viewer, int screenW, int screenH)
    {
    step4_changeTheCameraView(viewer, screenW, screenH);

    // Hold one camera, so that what moves is the model and not the view.
    viewer.clearCamera();
    viewer.setOrbit(0.0f, 0.75f, 6.5f);

    // A model can carry several clips - walk, run, idle. This one has one.
    // clipCount() is 0 on a model with no animation at all, and every call
    // below is then silently inert rather than an error.
    if (viewer.clipCount() > 0) viewer.selectClip(0);

    viewer.setPlaying(true);
    viewer.setSpeed(1.0f);

    g_animClock = 0.0f;
    g_animPhase = 0;
    }

void step5_playTheAnimation_update(a3d::Viewer& viewer, float dtMs)
    {
    // The camera is left alone on purpose - lesson 4's cycling would make it
    // impossible to tell which motion came from where.

    g_animClock += dtMs * 0.001f;
    if (g_animClock < 3.0f) return;
    g_animClock -= 3.0f;
    g_animPhase = (g_animPhase + 1) % 5;

    switch (g_animPhase)
        {
        case 0: viewer.setPlaying(true);  viewer.setSpeed(1.0f);  break;  // as authored
        case 1: viewer.setSpeed(0.25f);                           break;  // slow motion
        case 2: viewer.setSpeed(2.5f);                            break;  // fast
        case 3: viewer.setSpeed(-1.0f);                           break;  // backwards
        case 4: viewer.setPlaying(false);                         break;  // paused, still drawn
        default: break;
        }

    // TRY THIS: jump to a fixed moment in the clip instead of playing it.
    //           This is how you pose a model - a menu icon, a thumbnail, a
    //           character standing still in a particular way.
    // viewer.setPlaying(false);
    // viewer.seek(viewer.clipDurationMs() * 0.5f);
    }

// ===========================================================================
//
//  LESSON 6 - THE ENDLESS TRACK
//
//  Everything at once, and one idea that is not in any of the five lessons
//  above: THE CAR NEVER MOVES.
//
//  It sits at the same world position for the whole run. What moves is the
//  track under it, and the track is a 2D pattern scrolling in the overlay. It
//  does not even steer: the road is re-centred on the car every frame, so the
//  bends swing around the car instead. That is how every endless runner has
//  ever worked, and on a part with no GPU it is the difference between a scene
//  that costs 500 triangles and one that costs as many as you can afford.
//
//  What each of the five earlier lessons contributes:
//
//    1  the model, and a background colour that now means "track goes here"
//    2  the overlay, switched from still to scrolling
//    3  the light, aimed down because the camera is looking down
//    4  the orbit, driven every frame - the YAW is the car's steering
//    5  the wheel clip, with its speed tied to the speed of the road
//
// ===========================================================================
void step6_endlessRoad(a3d::Viewer& viewer, int screenW, int screenH)
    {
    step5_playTheAnimation(viewer, screenW, screenH);

    g_screenW = screenW;
    g_screenH = screenH;
    g_backdrop.configure(screenW, screenH);
    g_backdrop.setCarRow(screenH / 2);
    g_backdrop.setRunning(true);       // now it scrolls and changes theme
    g_backdrop.setTheme(0);

    viewer.setBackground(kBackdropKey);
    viewer.setOverlay(&Backdrop::overlay, &g_backdrop);

    viewer.setFieldOfView(kCameraFovDegrees);
    viewer.setLightDirection(kLightX, kLightY, kLightZ);
    allowDistantCamera(viewer);
    viewer.setPitchLimits(-0.20f, 1.40f);
    viewer.setOrbit(0.0f, kTopDownPitch, kTopDownDist);

    // The wheels turn at the speed the road moves. One number drives both, so
    // they cannot drift apart - change kScrollPxPerSec and the wheels follow.
    if (viewer.clipCount() > 0) viewer.selectClip(0);
    viewer.setPlaying(true);
    viewer.setSpeed(kWheelSpeed);
    }

void step6_endlessRoad_update(a3d::Viewer& viewer, float dtMs)
    {
    // 1. SCROLL THE WORLD. The only place any of this state is written, and
    //    the reason paintTile() can run on two cores at once.
    g_backdrop.advance(dtMs);

    if (!carReady(viewer)) return;   // first frame: let a3d frame the model

    // 2. STEER.
    //
    //    The camera orbits the car, so turning the camera's YAW turns the car
    //    on the screen - and the 2D track underneath does not turn with it,
    //    because it is drawn in screen space. The car therefore leans into the
    //    bends while the road stays where it is, which is exactly how a
    //    top-down racer reads.
    //
    //    The angle is a numeric derivative of the same function that draws the
    //    track, so the car cannot point somewhere the road does not go.
    viewer.setOrbit(g_backdrop.headingAtCar(), kTopDownPitch, kTopDownDist);

    // TRY THIS: hold the camera still and watch what survives. The road still
    //           scrolls, the wheels still turn, the trees still come past -
    //           the motion was never in the camera.
    // viewer.setOrbit(0.0f, kTopDownPitch, kTopDownDist);
    }

// ===========================================================================
// The dispatcher
// ===========================================================================

const char* stepName(int step)
    {
    switch (step)
        {
        case kObject:      return "1 - put the object on the screen";
        case kBackground:  return "2 - make a background";
        case kLight:       return "3 - add light";
        case kCamera:      return "4 - change the camera view";
        case kAnimation:   return "5 - play the animation";
        case kEndlessRoad: return "6 - the endless track";
        default:           return "unknown step";
        }
    }

void setupStep(int step, a3d::Viewer& viewer, int screenW, int screenH)
    {
    g_screenW = screenW;
    g_screenH = screenH;

    switch (step)
        {
        case kObject:      step1_putTheObjectOnScreen(viewer);                  break;
        case kBackground:  step2_makeABackground(viewer, screenW, screenH);     break;
        case kLight:       step3_addLight(viewer, screenW, screenH);            break;
        case kCamera:      step4_changeTheCameraView(viewer, screenW, screenH); break;
        case kAnimation:   step5_playTheAnimation(viewer, screenW, screenH);    break;
        case kEndlessRoad: step6_endlessRoad(viewer, screenW, screenH);         break;
        default:           step1_putTheObjectOnScreen(viewer);                  break;
        }
    }

void updateStep(int step, a3d::Viewer& viewer, float dtMs)
    {
    switch (step)
        {
        case kCamera:      step4_changeTheCameraView_update(viewer, dtMs); break;
        case kAnimation:   step5_playTheAnimation_update(viewer, dtMs);    break;
        case kEndlessRoad: step6_endlessRoad_update(viewer, dtMs);         break;
        default: break;   // lessons 1 to 3 are settings, not behaviour
        }
    }

void setHud(const char* timeBig, const char* timeSmall,
            const char* line1, const char* line2)
    { g_backdrop.setHud(timeBig, timeSmall, line1, line2); }

float lapSeconds() { return kLapSeconds; }

} // namespace tut
