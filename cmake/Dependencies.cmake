# Dependencies.cmake — SDL3 and Dear ImGui, fetched and pinned.
#
# Both are pinned to exact tags rather than a branch. A UI toolkit that moves
# under the build is a class of failure worth spending two lines to prevent, and
# "it worked yesterday" is not a thing anyone should have to debug here.
#
# Licences, since Fast is Apache-2.0 and meant to be forked freely:
#   SDL3       zlib
#   Dear ImGui MIT
#
# Both impose nothing on a fork. This is the reason Qt was not chosen: its LGPL
# path puts relinking obligations on everyone who ships a fork of Fast, and its
# commercial path costs money per developer.

include(FetchContent)

set(FAST_SDL_TAG   "release-3.4.16" CACHE STRING "SDL3 tag to build against")
# The docking branch's tag of the same release: panels that dock, tab, float
# and resize like every other editor's. (A new variable name, so a build tree
# that cached the plain tag picks this one up.)
set(FAST_IMGUI_DOCKING_TAG "v1.92.9-docking" CACHE STRING "Dear ImGui (docking) tag to build against")

# ------------------------------------------------------------------- SDL3 ----

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        ${FAST_SDL_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(SDL3)

# ------------------------------------------------------------- Dear ImGui ----
#
# Dear ImGui ships no build system on purpose: you compile the sources into your
# own target. The backend pair here is SDL3 for platform and SDL_Renderer3 for
# drawing, which avoids taking on an OpenGL loader for a 2D editor that does not
# need one.

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${FAST_IMGUI_DOCKING_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# The test engine's hooks, which the UI scripts (src/ui/ui_script.cpp) use to
# find a widget by its label. They are called only while a script turns them
# on, so an ordinary run pays nothing for them.
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# Third-party code is not held to our warning settings; ours is.
if (MSVC)
    target_compile_options(imgui PRIVATE /w)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

add_library(fast::imgui ALIAS imgui)
