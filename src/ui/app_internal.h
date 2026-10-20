// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// app_internal.h — what the window's source files share with one another.
//
// The window is several translation units, not one: app_window.cpp is the shape of
// the window and the main loop; menu_bar.cpp the menus; dialogs.cpp and
// preferences_panel.cpp the windows that open from them; file_actions.cpp the
// questions in front of New, Open, Save and Quit; stroke_input.cpp what the mouse
// does on the canvas; shortcuts.cpp what the keys do; headless.cpp and
// self_test.cpp the ways of running it with nobody there. They once were one
// file, and what was private to it is declared here, once, for the others.
//
// Nothing here is for a program built on Fast: that is app_extension.h. This header
// is for the window's own files.

#include "app/import_image.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/preferences.h"
#include "ui/editor.h"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace fast {

class CanvasView;
struct AppExtension;

// What the program running this window adds to it (app_extension.h), or null.
const AppExtension* currentExtension();

// --- the menus and the windows that open from them ---------------------------------

void drawMenuBar(Editor& editor, CanvasView& canvas, SDL_Window* window);

void drawHistoryPanel(Editor& editor, CanvasView& canvas);
void drawTextPanel(Editor& editor, CanvasView& canvas);
void drawSheetImportPanel(Editor& editor, CanvasView& canvas, SDL_Window* window);
void drawCanvasSizePanel(Editor& editor, CanvasView& canvas);
void drawAdjustPanel(Editor& editor, CanvasView& canvas);
void drawModifyPanel(Editor& editor);
void drawSpriteSizePanel(Editor& editor, CanvasView& canvas);

void drawPreferencesPanel(Editor& editor, CanvasView& canvas, SDL_Window* window);
// The canvas as the preferences have it: grid on or off, the chequer's colours and
// size, and the grid's colour.
void applyViewPreferences(const Preferences& prefs, CanvasView& canvas);

// --- files -----------------------------------------------------------------------------

// A document that has just replaced the old one from somewhere other than its own
// file: everything held about the old one goes, and the view fits the new canvas.
void adoptImported(Editor& editor, CanvasView& canvas, const ImportReport& report,
                   const std::string& from);
bool openPath(Editor& editor, CanvasView& canvas, const std::string& path);
bool saveTo(Editor& editor, const CanvasView& canvas, const std::string& path);
// Save, or Save As when this document has never been written. False when the
// answer is not known yet because a dialog is open.
bool saveOrAsk(Editor& editor, CanvasView& canvas, SDL_Window* window);
// Repeats the last export, to the same file, as if its dialog had just been answered.
void exportAgain(Editor& editor);
// Picks up whatever a native dialog came back with, once a frame.
void processDialogResult(Editor& editor, CanvasView& canvas, SDL_Window* window);
// What was found waiting from a session that did not end normally, and the question in
// front of anything that would discard unsaved work.
void drawRecoveryPrompt(Editor& editor, CanvasView& canvas);
void drawUnsavedPrompt(Editor& editor, CanvasView& canvas, SDL_Window* window);

// --- the canvas ----------------------------------------------------------------------------

// What a pencil or a bucket paints into: the ink, or a picked element as itself.
bool beginPaint(Editor& editor, const PaintLayer& layer, bool back, InkStroke* out);
// The tools, once a frame: strokes, fills, picking, dragging a reference.
void handleStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel);
// The symmetry axes, as thin dashed lines across the canvas.
void drawSymmetryAxes(Editor& editor, const CanvasView& canvas, ImDrawList* draw,
                      ImVec2 origin, float zoom);

// --- the keys -----------------------------------------------------------------------------

void handleShortcuts(Editor& editor, CanvasView& canvas, SDL_Window* window);

// --- headless runs ------------------------------------------------------------------
//
// A GUI is the part of a program usually tested by someone looking at it. These
// flags make it testable instead: --frames runs a fixed number and exits, --shot
// writes what was drawn, and --demo-stroke draws through the same calls the
// tools use. Under SDL's dummy video driver that runs with no display at all,
// which is what lets CI notice an editor that no longer starts.

struct Options {
    int         frames = 0;          // 0 = run until the user quits
    std::string screenshot;
    bool        demoStroke = false;
    bool        expectIdle = false;
    std::string sheetPath;
    bool        showSheetPanel = false;
    std::string select;                // --select x,y,w,h: a marquee, for a capture
    std::string tool;                  // --tool name: the tool in hand, for a capture
    bool        tiled = false;         // --tiled: tiled mode both ways and a tile grid
    bool        isometric = false;     // --isometric: an isometric tile grid
    bool        light = false;         // --light: the light theme, for captures
    bool        symmetry = false;      // --symmetry: both axes on
    bool        play = false;            // start playback, for a headless run
    bool        library = false;         // open the library window
    bool        preferences = false;     // open the preferences window
    bool        preferencesKeys = false; // ... on its Keys tab
    bool        layerProperties = false; // the active layer's properties, tagged
    bool        paths = false;           // --paths: a polygon and a curve, handles showing
    bool        tabs = false;            // --tabs: two more documents open beside the first
    bool        start = false;           // --start: the start screen up, as at a person's start
    bool        kit = false;             // --kit: the interface kit up (ui/widgets.h)
    bool        adjust = false;          // --adjust: the colour window, hue turned
    bool        shadow = false;          // --shadow: the figure casts a shadow
    bool        tween = false;           // --tween: a figure turned and moved across five frames
    // --drag X0,Y0,X1,Y1: a left-button drag between two canvas pixels, fed
    // through ImGui's own input queue over several frames -- the path a real
    // mouse takes -- so what a drag does can be captured and checked.
    float       drag[4] = { -1.f, -1.f, -1.f, -1.f };
    std::string script;                  // --script FILE: see ui_script.h
    // --stay: when the script has run, the window stays open and is the
    // person's -- a scripted start for trying something by hand.
    bool        stay = false;
    int         expectDrawn = -1;        // --expect-drawn N: fail unless N pixels are drawn
    int         expectAtMost = -1;       // --expect-at-most N: fail if more than N are
    float       rectangle[4] = { -1.f, -1.f, -1.f, -1.f };  // --rectangle X0,Y0,X1,Y1: a shape to start with
    bool        slices = false;          // --slices: two slices, the window open
    bool        rotsprite = false;       // --rotsprite: one sprite turned two ways, side by side
    std::string language;                // --language CODE: the interface in that language
    float       zoom = 0.f;              // --zoom N: the zoom after the first fit
    // Copy (after --select) or paste through the real system clipboard at
    // start: a headless check of the clipboard both ways. Overwrites the
    // clipboard of whoever runs it, so only when asked by name.
    bool        systemCopy = false;
    bool        systemPaste = false;
    uint32_t    autosaveSeconds = 0;     // override the interval, for testing
    bool        selfTest = false;
    std::string openPath;
    std::string reference;               // --reference FILE: imported at start
};

Options parseOptions(int argc, char** argv);

// A frame written as a PNG when the name says so, and as a BMP otherwise.
bool saveShot(SDL_Surface* surface, const std::string& path);

// A figure to look at: layers, frames, a palette, a few elements -- what the
// captures and the gallery draw on.
void drawDemoContent(Editor& editor);

// The state management the interface does, driven without the interface. 0 when
// every check passed.
int runSelfTest();

} // namespace fast
