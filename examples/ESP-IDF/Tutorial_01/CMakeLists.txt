cmake_minimum_required(VERSION 3.16)

# Tutorial_01 - a3d from nothing, in six steps.
#
# THE BOARD COMPONENT IS BORROWED, NOT COPIED.
#
#   The 10.1" JD9365 DSI bring-up already exists next door, in
#   Waveshare_ESP32-P4-Nano-Bench/components/p4_nano_board. A second copy of a
#   panel bring-up is a second thing to keep in step, and this tutorial is
#   about a3d rather than about MIPI-DSI - so it is referenced where it lives.
#
#   That does mean this project needs its sibling to be present, which is why
#   the check below exists: without it, a missing directory produces
#       Directory specified in EXTRA_COMPONENT_DIRS doesn't exist
#   which says nothing about what to do next.
set(A3D_TUT_BOARD "${CMAKE_CURRENT_LIST_DIR}/../Waveshare_ESP32-P4-Nano-Bench/components")
if(NOT EXISTS "${A3D_TUT_BOARD}/p4_nano_board/board.c")
    message(FATAL_ERROR
        "Tutorial_01: no p4_nano_board component at ${A3D_TUT_BOARD}.\n"
        "It is the 800x1280 JD9365 DSI bring-up, shared with the benchmark "
        "example beside this one rather than copied in.\n"
        "If you are building this for a DIFFERENT board: delete the "
        "EXTRA_COMPONENT_DIRS line below, drop your own panel bring-up in "
        "main/, and replace the three board_* calls in main/main.cpp. "
        "main.cpp says what to put in their place, and none of the six "
        "lessons changes.")
endif()
set(EXTRA_COMPONENT_DIRS "${A3D_TUT_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP-IDF 5.5.4 is the toolchain this tree is built and measured on. 6.x builds
# and runs, but a3d measured about 9% slower on the ESP32-P4 under 6.0.2. A
# warning rather than a hard bound: somebody who already has 6.x should not be
# stopped, only told.
if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
    message(WARNING
        "a3d: this example is built against ESP-IDF 5.5.4; you are on "
        "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. It builds and runs, "
        "but a3d measured ~9% slower on the ESP32-P4 under 6.0.2.")
endif()

# Only main and what it actually depends on. Without this the registry pulls
# the whole default component set into a build that needs a panel and a timer.
idf_build_set_property(MINIMAL_BUILD ON)

project(a3d_tutorial_01)
