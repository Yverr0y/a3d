// SPDX-FileCopyrightText: 2026 Eric Nam
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tutorial_steps.h
 * @brief The six lessons of Tutorial 01, one function each.
 *
 * WHY THE LESSONS LIVE HERE AND NOT IN main.cpp
 *
 *   main.cpp is the half that knows about your board: the DSI panel, the
 *   backlight, FreeRTOS, heap_caps. This file is the half that knows about
 *   a3d, and it names no vendor header at all - only `a3d::Viewer` and the
 *   2D canvas. That split is not tidiness:
 *
 *     - You can read a lesson without reading a panel driver.
 *     - You can paste a lesson into YOUR project, whatever your board is.
 *     - It compiles on a desktop, which is how the pictures in README.md
 *       were made without flashing anything.
 *
 * HOW A LESSON IS SHAPED
 *
 *   Each one is a pair:
 *
 *     stepN_...(viewer)              called ONCE, after the model is open
 *     stepN_..._update(viewer, dt)   called EVERY frame, before viewer.frame()
 *
 *   Lessons that change nothing over time have no update half, and that is
 *   the point of the shape: you can see at a glance whether a lesson is a
 *   setting or a behaviour.
 *
 *   Each setup function STARTS by calling the previous one. So everything
 *   above the call is inherited and everything below it is what this lesson
 *   is actually about. Read a lesson bottom-up and you read exactly the new
 *   idea.
 */
#ifndef TUTORIAL_STEPS_H_
#define TUTORIAL_STEPS_H_

#include "viewer/a3d_viewer.h"

namespace tut {

/**
 * Which lesson runs. main.cpp picks one and nothing else changes.
 *
 * They are numbered so `TUTORIAL_STEP` can be a plain 1..6 in main.cpp -
 * a number somebody can change without looking anything up.
 */
enum Step
    {
    kObject      = 1,   ///< 1. Put the car on the screen.
    kBackground  = 2,   ///< 2. Give it somewhere to be.
    kLight       = 3,   ///< 3. Light it.
    kCamera      = 4,   ///< 4. Move the camera.
    kAnimation   = 5,   ///< 5. Play the clip that is inside the model.
    kEndlessRoad = 6,   ///< 6. All five at once, forever.
    };

/** Human-readable name of a step, for the boot log. */
const char* stepName(int step);

// ===========================================================================
// The dispatcher. main.cpp calls these two and nothing else.
// ===========================================================================

/**
 * Configure the viewer for one lesson. Call once, AFTER openAsset() has
 * succeeded - several of the lessons read the model's size.
 *
 * @param screenW,screenH the panel, in pixels. The 2D backdrop needs to know
 *                        how big the picture is; the viewer already does.
 */
void setupStep(int step, a3d::Viewer& viewer, int screenW, int screenH);

/** Advance one lesson by `dtMs`. Call every frame, BEFORE viewer.frame(). */
void updateStep(int step, a3d::Viewer& viewer, float dtMs);

/**
 * The four corner strings the 2D backdrop draws, so they appear in lessons 2
 * to 6 and not in lesson 1, which has no backdrop.
 *
 *   timeBig     top left, large, white   - the run clock
 *   timeSmall   under it, amber          - the current lap
 *   line1,2     bottom left, small       - whatever is worth saying
 *
 * Any of them may be nullptr. The strings are NOT copied - they are read by
 * the tile workers while they draw - so hand over something that outlives the
 * frame. main.cpp uses static buffers.
 */
void setHud(const char* timeBig, const char* timeSmall,
            const char* line1, const char* line2);

/** How often the lap clock restarts, so main.cpp and the scene agree. */
float lapSeconds();

// ===========================================================================
// The six lessons, callable one at a time.
//
// Nothing stops you calling these directly instead of going through
// setupStep() - that is why they are here rather than hidden in the .cpp.
// ===========================================================================

void step1_putTheObjectOnScreen(a3d::Viewer& viewer);

void step2_makeABackground(a3d::Viewer& viewer, int screenW, int screenH);

void step3_addLight(a3d::Viewer& viewer, int screenW, int screenH);

void step4_changeTheCameraView(a3d::Viewer& viewer, int screenW, int screenH);
void step4_changeTheCameraView_update(a3d::Viewer& viewer, float dtMs);

void step5_playTheAnimation(a3d::Viewer& viewer, int screenW, int screenH);
void step5_playTheAnimation_update(a3d::Viewer& viewer, float dtMs);

void step6_endlessRoad(a3d::Viewer& viewer, int screenW, int screenH);
void step6_endlessRoad_update(a3d::Viewer& viewer, float dtMs);

} // namespace tut

#endif // TUTORIAL_STEPS_H_
