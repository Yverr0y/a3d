# main -> Tutorial_01 -> ESP-IDF -> examples -> a3d
set(A3D_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

# The model. Built from low-poly_car.glb by
#
#     python3 tools/a3d_export.py --check examples/ESP-IDF/Tutorial_01/low-poly_car.glb
#
# and committed beside it, so this project builds without a Python step. Run
# that command again after editing the .glb - it rewrites the .a3d in place,
# prints what came out, and says whether it will run on your part.
set(A3D_TUT_MODEL "${CMAKE_CURRENT_LIST_DIR}/../low-poly_car.a3d")
if(NOT EXISTS "${A3D_TUT_MODEL}")
    message(FATAL_ERROR
        "Tutorial_01: no low-poly_car.a3d beside the .glb. Build it with\n"
        "  python3 tools/a3d_export.py --check "
        "examples/ESP-IDF/Tutorial_01/low-poly_car.glb\n"
        "from the root of this repository. It needs Pillow for the texture: "
        "without it the car imports untextured and comes out plain white.")
endif()

idf_component_register(
    SRCS "main.cpp" "tutorial_steps.cpp"
    # Two include roots: this directory for tutorial_steps.h, and a3d's `src`,
    # which is the ONE root all of a3d hangs off - <a3d/...> for the core,
    # "viewer/...", "backends/..." and "loaders/..." for the rest. a3d is
    # header-only, so that is the whole of adding it: nothing is compiled and
    # nothing is linked.
    INCLUDE_DIRS "." "${A3D_ROOT}/src"

    # esp_lcd_touch is here although this tutorial reads no touch at all. The
    # shared board.h declares board_touch_init() in terms of
    # esp_lcd_touch_handle_t, so including board.h needs the header on the
    # path. The board component keeps it PRIVATE, which is right for the
    # component and means every consumer asks for it.
    #
    # a3d itself is NOT in this list, and that is not an omission: it is
    # header-only, so the include root above is the whole of adding it. What
    # IS listed is what a3d's own headers reach for - freertos and log for the
    # executor, esp_lcd for the display adapter - plus this board's.
    REQUIRES p4_nano_board esp_lcd esp_lcd_touch esp_timer heap freertos log

    # Embedding puts the container in .rodata, which on an ESP32 is
    # memory-mapped flash. a3d samples the texture straight out of it - no
    # copy, no partition to write, no esptool step. See the `asm(...)` names
    # at the top of main.cpp for how the symbol gets spelled.
    EMBED_FILES "${A3D_TUT_MODEL}"
)
