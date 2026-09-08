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
    src/app/document.cpp
    src/app/paint.cpp
)

target_include_directories(fast_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
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

    add_executable(fast_paint_tests tests/paint_tests.cpp)
    target_link_libraries(fast_paint_tests PRIVATE fast_core)
    add_test(NAME fast_paint COMMAND fast_paint_tests)

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
    endif()
endif()
