cmake_minimum_required(VERSION 3.20)
project(sprits_fast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------- the engine --
#
# The engine is a separate repository under a different licence, so it is
# referenced rather than copied in. Two copies of a deterministic engine is two
# sets of golden hashes that drift apart, and a BUSL source tree sitting inside
# an Apache-2.0 repository muddles a split that is worth keeping clean.
#
# Three ways to find it, tried in order:
#
#   1. -DLIVESPRITE_DIR=<path>   a source checkout, added directly. This is the
#                                development path: edit the engine and Fast in
#                                one build, no install step between them.
#   2. find_package(LiveSprite)  an installed copy.
#   3. FetchContent              a pinned tag, for a clean CI machine.
#
# The default is the sibling checkout, because that is where it is during
# development.

set(LIVESPRITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sprit s pract"
    CACHE PATH "Path to a LiveSprite engine source checkout")
set(LIVESPRITE_GIT_REPOSITORY "" CACHE STRING "Engine git URL, for the FetchContent fallback")
set(LIVESPRITE_GIT_TAG "main" CACHE STRING "Engine git tag to pin")

if (EXISTS "${LIVESPRITE_DIR}/CMakeLists.txt")
    message(STATUS "LiveSprite: using the source checkout at ${LIVESPRITE_DIR}")
    # The engine switches its own tests, testbed and install rules off when it is
    # not the top-level project, so this brings in the library and nothing else.
    add_subdirectory("${LIVESPRITE_DIR}" "${CMAKE_BINARY_DIR}/livesprite")
else()
    find_package(LiveSprite QUIET)
    if (LiveSprite_FOUND)
        message(STATUS "LiveSprite: using the installed package")
    elseif (LIVESPRITE_GIT_REPOSITORY)
        message(STATUS "LiveSprite: fetching ${LIVESPRITE_GIT_TAG}")
        include(FetchContent)
        FetchContent_Declare(livesprite
            GIT_REPOSITORY "${LIVESPRITE_GIT_REPOSITORY}"
            GIT_TAG        "${LIVESPRITE_GIT_TAG}")
        FetchContent_MakeAvailable(livesprite)
    else()
        message(FATAL_ERROR
            "Could not find the LiveSprite engine.\n"
            "Point -DLIVESPRITE_DIR at a checkout, install the engine so "
            "find_package(LiveSprite) succeeds, or set "
            "-DLIVESPRITE_GIT_REPOSITORY to fetch it.")
    endif()
endif()

# ------------------------------------------------------------------- the app --
#
# fast_core is everything that is not a user interface: the document, the undo
# history, the tools, file handling. It is deliberately a separate library from
# whatever draws the windows, so it can be tested without one and so the choice
# of toolkit stays replaceable.

add_library(fast_core STATIC
    src/app/animation.cpp
    src/app/bucket.cpp
    src/app/dither.cpp
    src/app/document.cpp
    src/app/palette.cpp
    src/app/palette_io.cpp
    src/app/layers.cpp
    src/app/element.cpp
    src/app/brush.cpp
    src/app/image_io.cpp
    src/app/ink.cpp
    src/app/library.cpp
    src/app/reference.cpp
    src/app/selection.cpp
    src/app/recovery.cpp
    src/app/export_png.cpp
    src/app/floating.cpp
    src/app/file_io.cpp
    src/app/paint.cpp
    src/app/recent_files.cpp
    src/app/shape.cpp
    src/app/sheet.cpp
    src/app/transform.cpp
    src/app/ui_state.cpp
)

target_include_directories(fast_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# stb is somebody else's code and is not held to our warning settings. Ours is:
# this only relaxes the file that includes it.
if (MSVC)
    set_source_files_properties(src/app/export_png.cpp PROPERTIES COMPILE_OPTIONS "/W3")
else()
    set_source_files_properties(src/app/export_png.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error")
endif()
target_link_libraries(fast_core PUBLIC livesprite::livesprite)

if (MSVC)
    target_compile_options(fast_core PRIVATE /W4 /WX)
else()
    target_compile_options(fast_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The smoke executable. It has no window: it proves that Fast links the engine
# and can drive a document end to end, which is the thing worth knowing before
# any user interface exists.
add_executable(fast_smoke src/main.cpp)
target_link_libraries(fast_smoke PRIVATE fast_core)

# -------------------------------------------------------------------- the UI --
#
# Off by default for a reason worth stating: turning it on fetches and builds
# SDL3 from source, which takes minutes the first time. Working on fast_core --
# the document, undo, tools, file handling -- does not need any of it, and that
# is where most of the work is.

option(FAST_BUILD_GUI "Build the windowed editor (fetches SDL3 and Dear ImGui)" OFF)

if (FAST_BUILD_GUI)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

    add_executable(sprits_fast WIN32
        src/ui/app_window.cpp
        src/ui/canvas_view.cpp
        src/ui/editor.cpp
        src/ui/frame_cache.cpp
        src/ui/reference_cache.cpp
        src/ui/selection_tools.cpp
        src/ui/panels.cpp
        src/ui/timeline.cpp
        src/ui/theme.cpp
        src/ui/file_commands.cpp
    )
    target_link_libraries(sprits_fast PRIVATE fast_core fast::imgui)

    if (MSVC)
        target_compile_options(sprits_fast PRIVATE /W4 /WX)
        # WIN32 above hides the console for a released build; during development
        # the printf diagnostics on start-up failure are worth more than that.
        set_target_properties(sprits_fast PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE")
    else()
        target_compile_options(sprits_fast PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

# ---------------------------------------------------------------- the tests --

option(FAST_BUILD_TESTS "Build the tests" ON)
if (FAST_BUILD_TESTS)
    enable_testing()

    add_executable(fast_document_tests tests/document_tests.cpp)
    target_link_libraries(fast_document_tests PRIVATE fast_core)
    add_test(NAME fast_document COMMAND fast_document_tests)

    add_executable(fast_ink_tests tests/ink_tests.cpp)
    target_link_libraries(fast_ink_tests PRIVATE fast_core)
    add_test(NAME fast_ink COMMAND fast_ink_tests)

    add_executable(fast_selection_tests tests/selection_tests.cpp)
    target_link_libraries(fast_selection_tests PRIVATE fast_core)
    add_test(NAME fast_selection COMMAND fast_selection_tests)

    add_executable(fast_paint_tests tests/paint_tests.cpp)
    target_link_libraries(fast_paint_tests PRIVATE fast_core)
    add_test(NAME fast_paint COMMAND fast_paint_tests)

    add_executable(fast_robustness_tests tests/robustness_tests.cpp)
    target_link_libraries(fast_robustness_tests PRIVATE fast_core)
    add_test(NAME fast_robustness COMMAND fast_robustness_tests)

    add_executable(fast_file_io_tests tests/file_io_tests.cpp)
    target_link_libraries(fast_file_io_tests PRIVATE fast_core)
    add_test(NAME fast_file_io COMMAND fast_file_io_tests)

    add_executable(fast_recent_tests tests/recent_files_tests.cpp)
    target_link_libraries(fast_recent_tests PRIVATE fast_core)
    add_test(NAME fast_recent COMMAND fast_recent_tests)

    add_executable(fast_ui_state_tests tests/ui_state_tests.cpp)
    target_link_libraries(fast_ui_state_tests PRIVATE fast_core)
    add_test(NAME fast_ui_state_format COMMAND fast_ui_state_tests)

    add_executable(fast_export_tests tests/export_tests.cpp)
    target_link_libraries(fast_export_tests PRIVATE fast_core)
    add_test(NAME fast_export COMMAND fast_export_tests)

    add_executable(fast_transform_tests tests/transform_tests.cpp)
    target_link_libraries(fast_transform_tests PRIVATE fast_core)
    add_test(NAME fast_transform COMMAND fast_transform_tests)

    add_executable(fast_bucket_tests tests/bucket_tests.cpp)
    target_link_libraries(fast_bucket_tests PRIVATE fast_core)
    add_test(NAME fast_bucket COMMAND fast_bucket_tests)

    add_executable(fast_animation_tests tests/animation_tests.cpp)
    target_link_libraries(fast_animation_tests PRIVATE fast_core)
    add_test(NAME fast_animation COMMAND fast_animation_tests)

    add_executable(fast_palette_io_tests tests/palette_io_tests.cpp)
    target_link_libraries(fast_palette_io_tests PRIVATE fast_core)
    add_test(NAME fast_palette_io COMMAND fast_palette_io_tests)

    add_executable(fast_sheet_tests tests/sheet_tests.cpp)
    target_link_libraries(fast_sheet_tests PRIVATE fast_core)
    add_test(NAME fast_sheet COMMAND fast_sheet_tests)

    add_executable(fast_dither_tests tests/dither_tests.cpp)
    target_link_libraries(fast_dither_tests PRIVATE fast_core)
    add_test(NAME fast_dither COMMAND fast_dither_tests)

    add_executable(fast_palette_tests tests/palette_tests.cpp)
    target_link_libraries(fast_palette_tests PRIVATE fast_core)
    add_test(NAME fast_palette COMMAND fast_palette_tests)

    add_executable(fast_shape_tests tests/shape_tests.cpp)
    target_link_libraries(fast_shape_tests PRIVATE fast_core)
    add_test(NAME fast_shape COMMAND fast_shape_tests)

    # The features together. Every file above proves one thing on its own;
    # this one builds a real document and holds it to a few promises through
    # everything at once -- palette swaps, frame surgery, undo, round trips.
    add_executable(fast_recovery_tests tests/recovery_tests.cpp)
    target_link_libraries(fast_recovery_tests PRIVATE fast_core)
    add_test(NAME fast_recovery COMMAND fast_recovery_tests)

    add_executable(fast_reference_tests tests/reference_tests.cpp)
    target_link_libraries(fast_reference_tests PRIVATE fast_core)
    add_test(NAME fast_reference COMMAND fast_reference_tests)

    add_executable(fast_brush_tests tests/brush_tests.cpp)
    target_link_libraries(fast_brush_tests PRIVATE fast_core)
    add_test(NAME fast_brush COMMAND fast_brush_tests)

    add_executable(fast_element_tests tests/element_tests.cpp)
    target_link_libraries(fast_element_tests PRIVATE fast_core)
    add_test(NAME fast_element COMMAND fast_element_tests)

    add_executable(fast_layers_tests tests/layers_tests.cpp)
    target_link_libraries(fast_layers_tests PRIVATE fast_core)
    add_test(NAME fast_layers COMMAND fast_layers_tests)

    add_executable(fast_system_tests tests/system_tests.cpp)
    target_link_libraries(fast_system_tests PRIVATE fast_core)
    add_test(NAME fast_system COMMAND fast_system_tests)

    add_test(NAME fast_smoke COMMAND fast_smoke)

    # The interface, checked without anyone looking at it. SDL's dummy video
    # driver needs no display, so this runs on a CI machine and catches a UI
    # that no longer starts, initialises or renders a frame -- the failure a
    # test suite otherwise never sees until someone opens the program.
    if (FAST_BUILD_GUI)
        add_test(NAME fast_ui_starts
                 COMMAND sprits_fast --frames 5 --demo-stroke)
        set_tests_properties(fast_ui_starts PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")

        # The window's own bookkeeping -- rebuilding the layer panel after undo,
        # redo and open. None of it is reachable from a fast_core test, and all
        # of it was wrong once.
        add_test(NAME fast_ui_state COMMAND sprits_fast --self-test)
    endif()
endif()
