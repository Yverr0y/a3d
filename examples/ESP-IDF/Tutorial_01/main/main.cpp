// SPDX-FileCopyrightText: 2026 Eric Nam
// SPDX-License-Identifier: Apache-2.0

/**
 * @file main.cpp
 * @brief Tutorial 01 - the board half. Pick a lesson on the line marked below.
 *
 * WHAT TO CHANGE
 *
 *   One number, `TUTORIAL_STEP`, a few lines down. 1 to 6. Everything else in
 *   this file is the same for all six lessons, and the lessons themselves are
 *   in tutorial_steps.cpp, which names no vendor header at all.
 *
 * WHAT THIS FILE IS RESPONSIBLE FOR
 *
 *   The six things a3d cannot know about your board:
 *
 *     1. THE PANEL      bring it up, and hand a3d the handle
 *     2. THE ROTATION   the glass scans portrait; the app is landscape
 *     3. THE WORKERS    how many cores draw tiles
 *     4. THE MEMORY     which heap the tile buffers come from
 *     5. THE CLOCK      so the frame timings are real
 *     6. THE MODEL      where the bytes are
 *
 *   Then it loops: read the time, advance the lesson, draw.
 *
 * THE BOARD THIS IS WRITTEN FOR
 *
 *   A Waveshare ESP32-P4-Nano with the 10.1" 800x1280 JD9365 DSI panel. The
 *   bring-up is NOT copied in here - it is the `p4_nano_board` component that
 *   the benchmark example next door already owns, referenced where it lives.
 *
 * YOUR BOARD IS NOT THAT ONE
 *
 *   Then you have an `esp_lcd_panel_handle_t` from your own bring-up - a
 *   vendor example, a BSP, an LVGL port, your own code - and that is the only
 *   thing a3d wants. Replace the three board_* calls in app_main() with
 *   whatever produces your handle, set the width and height, and name the wait
 *   mode for your bus:
 *
 *       EspLcdWait::IoCallback   SPI, QSPI, I80  (pass cfg.io as well)
 *       EspLcdWait::DpiCallback  MIPI-DSI        (what this file uses)
 *       EspLcdWait::Synchronous  RGB parallel, and DSI built without DMA2D
 *
 *   If your glass is already landscape, set PANEL_IS_PORTRAIT to 0 and the
 *   whole rotation below compiles away. Nothing in any of the six lessons
 *   changes either way.
 */

#include "tutorial_steps.h"

#include "backends/esp_lcd/a3d_display_esp_lcd.h"
#include "backends/freertos/a3d_freertos_executor.h"
#include "viewer/a3d_viewer.h"

#include "board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

// ===========================================================================
//
//   >>>  THE ONE LINE TO CHANGE  <<<
//
//   1  put the object on the screen      4  change the camera view
//   2  make a background                 5  play the animation
//   3  add light                         6  the endless track
//
//   Each lesson includes every lesson before it, so 6 is the whole thing and
//   1 is the smallest complete program. Change the number, `idf.py flash
//   monitor`, and read tutorial_steps.cpp for what that lesson actually did.
//
// ===========================================================================
#define TUTORIAL_STEP 6

// ===========================================================================
//
//   THE PANEL IS SIDEWAYS
//
//   This panel scans 800 across by 1280 down. The tutorial is landscape, so
//   it draws a 1280x800 picture and this file turns it a quarter turn on the
//   way to the glass.
//
//   WHY NOT ASK THE DRIVER TO ROTATE. A MIPI-DSI panel in video mode has no
//   rotation to ask for: the DPI peripheral scans the frame buffer out
//   continuously in hardware, in the order the DSI timing says, and
//   `esp_lcd_panel_swap_xy()` is not implemented for it at all. The same is
//   true of RGB parallel. A panel with its own controller RAM - most SPI and
//   QSPI parts - CAN do it in a register, and if yours can, use that and set
//   PANEL_IS_PORTRAIT to 0: it is free and this is not.
//
//   WHY THIS IS THREE LINES OF a3d AND NOT A PORT. An a3d::Display is a size
//   and a function that moves a rectangle of pixels. Nothing above it can tell
//   whether that function talks to a panel, a file, or - as here - to another
//   Display with the pixels turned. So the whole of "this app is landscape on
//   a portrait panel" lives in RotatedDisplay below, and not one line of the
//   six lessons knows about it.
//
//   WHAT IT COSTS. One transpose of every pixel, every frame. That is not
//   free - see README.md - and it is the reason to prefer a driver-side
//   rotation when your panel has one.
//
//   IF THE PICTURE COMES OUT UPSIDE DOWN, flip ROTATE_CLOCKWISE. That is the
//   whole adjustment; there is no third case.
//
// ===========================================================================
#define PANEL_IS_PORTRAIT  1
#define ROTATE_CLOCKWISE   1

#if PANEL_IS_PORTRAIT
constexpr int kRenderW = BOARD_LCD_V_RES;   // 1280 - the long edge, across
constexpr int kRenderH = BOARD_LCD_H_RES;   // 800  - the short edge, down
#else
constexpr int kRenderW = BOARD_LCD_H_RES;
constexpr int kRenderH = BOARD_LCD_V_RES;
#endif

// Rows per render tile, and the memory knob. A worker needs THREE buffers of
// kRenderW * TILE_ROWS pixels: colour, depth, and the rotated copy - except
// the rotated one is shared, because the viewer already holds the bus mutex
// across sendTile and two workers can never be inside it at once.
//
// At 1280 wide and 20 rows that is 50 KB each: 200 KB for two workers plus one
// 50 KB shared. Lower it first if a buffer will not allocate.
constexpr int kTileRows = 20;

// ===========================================================================
// The model, embedded in the app image.
//
// EMBED_FILES in main/CMakeLists.txt puts the container in .rodata, which on
// an ESP32 is memory-mapped flash. a3d reads it IN PLACE - the vertex indices
// and the texture pixels are sampled straight out of flash and never copied
// into RAM, which is why a 51 KB model costs about 30 KB of heap rather than
// 81 KB.
//
// The symbol is the FILE NAME with every character that is not a letter, a
// digit or an underscore replaced by an underscore - the dot and the hyphen
// included. That is why it is spelled out here rather than guessed at:
// "low-poly_car.a3d" becomes "low_poly_car_a3d".
// ===========================================================================
extern const uint8_t car_start[] asm("_binary_low_poly_car_a3d_start");
extern const uint8_t car_end[]   asm("_binary_low_poly_car_a3d_end");

namespace
{

const char* TAG = "a3d_tut01";

esp_lcd_panel_handle_t g_panel = nullptr;

a3d::EspLcdDisplay g_glass;
a3d::Viewer        g_viewer;

// ---------------------------------------------------------------------------
// 2. THE ROTATION
// ---------------------------------------------------------------------------

/**
 * An a3d::Display that is the panel with the picture turned a quarter turn.
 *
 * The viewer hands it full-width strips of the LANDSCAPE render; each one
 * becomes a narrow full-height COLUMN of the portrait panel.
 *
 *   render (rx, ry), clockwise  ->  panel ((kRenderH - 1) - ry, rx)
 *
 * so a strip of `h` rows is a column `h` pixels wide spanning every panel row.
 */
class RotatedDisplay
    {
    public:

        bool begin(const a3d::Display& panel, int renderW, int renderH, bool clockwise)
            {
            _panel = panel;
            _rw = renderW;
            _rh = renderH;
            _cw = clockwise;

            // One buffer, not one per worker: the viewer takes the bus mutex
            // around sendTile, so only one worker is ever inside _send().
            _buf = (uint16_t*)heap_caps_malloc((size_t)_rw * kTileRows * sizeof(uint16_t),
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (_buf == nullptr) return false;

            _out = a3d::Display{};
            _out.width     = _rw;
            _out.height    = _rh;
            _out.name      = "jd9365_dsi_landscape";
            _out.user      = this;
            _out.sendTile  = &sendThunk;
            _out.fill      = _panel.fill      ? &fillThunk   : nullptr;
            _out.lockBus   = _panel.lockBus   ? &lockThunk   : nullptr;
            _out.unlockBus = _panel.unlockBus ? &unlockThunk : nullptr;
            _out.swapBytes = false;   // the inner Display already handled it
            _out.round     = _panel.round;
            return true;
            }

        a3d::Display& display() { return _out; }

    private:

        /**
         * Transpose the strip, then hand the panel one column.
         *
         * In 8x8 blocks rather than a plain double loop. One of the two walks
         * is against the grain whichever way round it is written, and blocking
         * keeps both inside the cache instead of missing on every pixel - it
         * is the single cheapest thing that can be done to a transpose.
         */
        void send(const uint16_t* src, int y, int w, int h)
            {
            if ((w != _rw) || (h <= 0) || (h > kTileRows)) return;

            uint16_t* dst = _buf;               // h wide, _rw tall
            constexpr int B = 8;
            for (int rx0 = 0; rx0 < w; rx0 += B)
                {
                const int rxEnd = (rx0 + B < w) ? (rx0 + B) : w;
                for (int ly0 = 0; ly0 < h; ly0 += B)
                    {
                    const int lyEnd = (ly0 + B < h) ? (ly0 + B) : h;
                    for (int rx = rx0; rx < rxEnd; rx++)
                        {
                        const uint16_t* s = src + (size_t)ly0 * w + rx;
                        for (int ly = ly0; ly < lyEnd; ly++, s += w)
                            {
                            const int col = _cw ? (h - 1 - ly) : ly;
                            const int row = _cw ? rx : (w - 1 - rx);
                            dst[(size_t)row * h + col] = *s;
                            }
                        }
                    }
                }

            // Where that column lands on the panel.
            const int px0 = _cw ? (_rh - y - h) : y;
            _panel.sendTile(_panel.user, dst, px0, 0, h, _rw);
            }

        static RotatedDisplay* self(void* u) { return (RotatedDisplay*)u; }

        static void sendThunk(void* u, const uint16_t* s, int, int y, int w, int h)
            { self(u)->send(s, y, w, h); }
        static void fillThunk(void* u, uint16_t c)
            { const a3d::Display& p = self(u)->_panel; p.fill(p.user, c); }
        static void lockThunk(void* u)
            { const a3d::Display& p = self(u)->_panel; p.lockBus(p.user); }
        static void unlockThunk(void* u)
            { const a3d::Display& p = self(u)->_panel; p.unlockBus(p.user); }

        a3d::Display _panel{};
        a3d::Display _out{};
        uint16_t* _buf = nullptr;
        int  _rw = 0, _rh = 0;
        bool _cw = true;
    };

RotatedDisplay g_rotated;

// --- 4. THE MEMORY ---------------------------------------------------------
//
// TWO heaps, and the split matters more than it looks.
//
// The TILE buffers are touched for every pixel of every frame and handed to
// the DSI DMA, so they have to be internal SRAM and DMA-capable.
//
// The BULK arrays - the binner's per-tile lists, the decoded vertex buffer -
// are read once per triangle rather than once per pixel, so PSRAM is the right
// place for them and taking them out of internal SRAM by accident is how you
// run out of it.
void* dmaAlloc(size_t n)
    { return heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }

void* bulkAlloc(size_t n)
    {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return (p != nullptr) ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

void capsFree(void* p) { heap_caps_free(p); }

// --- 5. THE CLOCK ----------------------------------------------------------
// Without this every time in ViewerStats stays zero, and the frame-rate line
// below would have nothing to print.
uint64_t nowUs(void*) { return (uint64_t)esp_timer_get_time(); }

/**
 * Turn the model slowly, so every side of it gets seen - but only for the
 * lessons that are not already driving the camera.
 *
 * Lessons 4, 5 and 6 decide the camera themselves, so a turn added here would
 * fight them for it. Fighting the application for the camera is a real bug in
 * real firmware and it is worth seeing the shape of it here.
 */
constexpr bool kAutoTurn = (TUTORIAL_STEP <= 3);

void autoTurn(float dtMs)
    {
    if (!kAutoTurn) return;
    // Scaled by dt, so the speed does not change with the frame rate.
    g_viewer.orbit(dtMs * 0.0004f, 0.0f);
    }

/** mm:ss:mmm, the way a lap timer reads. */
void formatClock(char* out, size_t n, uint32_t ms)
    {
    snprintf(out, n, "%02u:%02u:%03u",
             (unsigned)(ms / 60000u), (unsigned)((ms / 1000u) % 60u),
             (unsigned)(ms % 1000u));
    }

} // namespace

extern "C" void app_main(void)
    {
    // --- 1. THE PANEL ------------------------------------------------------
    //
    // It comes up FIRST, because it is what knows the size. A viewer sized
    // from a constant that disagrees with the glass draws a correct picture
    // into the wrong window, and that looks like a projection bug.
    ESP_ERROR_CHECK(board_display_init(&g_panel));

    a3d::EspLcdConfig glass;
    glass.panel  = g_panel;
    glass.io     = nullptr;              // a DSI panel's pixels do not go through the DBI IO
    glass.width  = BOARD_LCD_H_RES;      // the PANEL's own size, 800 x 1280,
    glass.height = BOARD_LCD_V_RES;      // whatever orientation the app draws in
    glass.name   = "jd9365_dsi";

    // NAMED, not left on Auto. esp_lcd_dpi_panel_register_event_callbacks()
    // does not check that the handle it is given is really a DPI panel, so
    // Auto refuses to guess this one rather than write through a pointer into
    // the wrong struct. Naming it is your assertion that you looked.
    glass.wait = a3d::EspLcdWait::DpiCallback;

    // This panel reads little-endian, so no software byte swap. Getting this
    // wrong does not look like "the colours are slightly off" - a near-black
    // background comes out PINK.
    glass.swapBytes = false;

    ESP_ERROR_CHECK(g_glass.begin(glass));

    // Black first, backlight second. The other order shows whatever the panel
    // was holding from the previous boot.
    if (g_glass.display().fill != nullptr)
        g_glass.display().fill(g_glass.display().user, 0x0000);
    ESP_ERROR_CHECK(board_backlight_set(100));

    // --- 2. THE ROTATION ---------------------------------------------------
#if PANEL_IS_PORTRAIT
    if (!g_rotated.begin(g_glass.display(), kRenderW, kRenderH, ROTATE_CLOCKWISE))
        {
        ESP_LOGE(TAG, "no DMA memory for the %dx%d rotation buffer", kRenderW, kTileRows);
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    a3d::Display& screen = g_rotated.display();
    ESP_LOGI(TAG, "panel %dx%d portrait, drawing %dx%d landscape",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, kRenderW, kRenderH);
#else
    a3d::Display& screen = g_glass.display();
#endif

    // --- 3. THE WORKERS ----------------------------------------------------
    //
    // Built BEFORE the viewer, because it is what decides how many sets of
    // tile buffers get allocated: one worker allocates one set, not two. One
    // worker spawns no task and draws every tile inline, and that is a
    // supported target rather than a fallback.
    //
    // TRY THIS: 1 instead of 2. It halves the tile memory and roughly halves
    //           the frame rate, and the log line below prints both.
    static a3d::FreeRtosExecutor exec(2, 8192, 5);

    a3d::ViewerConfig cfg;
    cfg.tileHeight = kTileRows;

    g_viewer.setExecutor(exec);
    g_viewer.setAllocator(dmaAlloc, capsFree);
    g_viewer.setLargeAllocator(bulkAlloc, capsFree);
    g_viewer.setClock(&nowUs, nullptr);

    if (!g_viewer.begin(screen, cfg))
        {
        ESP_LOGE(TAG, "viewer would not start: %s", g_viewer.error());
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
        }

    // --- 6. THE MODEL ------------------------------------------------------
    if (!g_viewer.openAsset(car_start, (size_t)(car_end - car_start)))
        {
        ESP_LOGE(TAG, "model would not load: %s", g_viewer.error());
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
        }

    ESP_LOGI(TAG, "lesson %s", tut::stepName(TUTORIAL_STEP));
    ESP_LOGI(TAG, "%u vertices, %u triangles, %u clip(s) - %s",
             (unsigned)g_viewer.vertexCount(), (unsigned)g_viewer.triangleCount(),
             (unsigned)g_viewer.clipCount(),
             (g_viewer.clipCount() > 0) ? g_viewer.clipName() : "static");
    ESP_LOGI(TAG, "%d worker(s), %d tiles of %d rows at %dx%d",
             g_viewer.slotCount(), g_viewer.tileCount(), g_viewer.tileHeight(),
             g_viewer.viewportWidth(), g_viewer.viewportHeight());
    ESP_LOGI(TAG, "internal free %u KB, PSRAM free %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));

    // Everything a3d-specific about this program happens in here.
    tut::setupStep(TUTORIAL_STEP, g_viewer, kRenderW, kRenderH);

    // --- the loop ----------------------------------------------------------
    static char hudRun[16] = "00:00:000";
    static char hudLap[16] = "00:00:000";
    static char hudFps[32] = "a3d";
    static char hudStep[40];
    snprintf(hudStep, sizeof(hudStep), "STEP %d", TUTORIAL_STEP);
    tut::setHud(hudRun, hudLap, hudFps, hudStep);

    const uint64_t bootMs = (uint64_t)esp_timer_get_time() / 1000;
    const uint32_t lapMs  = (uint32_t)(tut::lapSeconds() * 1000.0f);

    uint64_t lastMs   = bootMs;
    uint64_t reportMs = bootMs;
    int frames = 0;

    while (true)
        {
        const uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        float dt = (float)(now - lastMs);
        lastMs = now;

        // Two guards that are not paranoia. A dt of 0 tells a3d "do not
        // advance the pose", which silently freezes the animation on a fast
        // frame; a huge dt after a stall teleports it.
        if (dt <= 0.0f)   dt = 1.0f;
        if (dt > 250.0f)  dt = 250.0f;

        const uint32_t runMs = (uint32_t)(now - bootMs);
        formatClock(hudRun, sizeof(hudRun), runMs);
        formatClock(hudLap, sizeof(hudLap), (lapMs > 0) ? (runMs % lapMs) : runMs);

        autoTurn(dt);
        tut::updateStep(TUTORIAL_STEP, g_viewer, dt);
        g_viewer.frame(dt);
        frames++;

        // Once a second, and read the TAIL rather than the first few seconds:
        // frame time falls for about five seconds after boot while the caches
        // warm, and a number read before that is not the number you have.
        if (now - reportMs >= 1000)
            {
            const a3d::ViewerStats& s = g_viewer.stats();
            ESP_LOGI(TAG, "%d fps  frame=%.1fms  anim=%.1f skin=%.1f bin=%.1f draw=%.1f  %u tris",
                     frames, s.frameUs / 1000.0, s.animUs / 1000.0, s.skinUs / 1000.0,
                     s.binUs / 1000.0, s.drawUs / 1000.0, (unsigned)s.trianglesDrawn);
            snprintf(hudFps, sizeof(hudFps), "a3d  %d fps", frames);
            frames = 0;
            reportMs = now;
            }

        // One tick, so the idle task on this core gets to run. The render loop
        // is otherwise a busy loop that never blocks, and that starves it.
        vTaskDelay(1);
        }
    }
