// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// ui_script.h — driving the window the way a person does.
//
// Every other test calls the editor's functions directly, which is how a drag
// that painted only its first frame got past all of them: nothing went
// through ImGui's input. A script is a text file of mouse and keyboard
// actions fed into ImGui's own input queue, a frame at a time, with checks on
// what the document then holds. `--script FILE` runs one; CI runs the ones in
// tests/ui.
//
//     # a comment
//     tool pencil                 choose a tool by the name --tool takes
//     colour e08f40               the foreground colour
//     move 3 4                    the pointer to the middle of canvas pixel (3, 4)
//     press / release [right]     a button, where the pointer is
//     click 3 4 [right]
//     click-on + Frame            a widget by its label, as the last frame drew it:
//                                 a button, a menu, a menu item, a checkbox;
//                                 LABEL @2 is the second of several so called
//     menu File > Export PNG > 2x  a path through the menus, each label clicked in turn
//                                 (the first drawn of that name: the menu bar comes before
//                                 the panels), and a pause after for the menus to close
//     right-click-on Layer 1 / double-click-on Layer 1
//     hover-on timeline tab       the pointer onto a widget, left there, with no click
//     drag-onto Layer 2 => Layer 1   press on one widget, let go on another
//     drag-from Elements by -400 60  press on a widget and drag it by screen
//                                 pixels -- a panel's tab out of its dock
//     expect item Tween           a widget with that label was drawn
//     expect missing Tween        and one that was not
//     expect docked elements 1    a panel (its ID, see layout.h) docked, or 0: floating
//     expect width layers 200 300 a panel's width, in screen pixels
//     expect height timeline 150 250  a panel's height, in screen pixels
//     drag 3 3 20 20 [steps] [right]
//     drag-screen 40 0 [steps] [middle|right]  a drag by screen pixels from where
//                                 the pointer is -- a pan moves the canvas under it
//     wheel 1                     the mouse wheel, where the pointer is (negative: back)
//     hold shift|ctrl|alt|space   a key held down until `let`
//     let shift|ctrl|alt|space
//     key ctrl+z                  a chord pressed and released
//     type hello                  text into whatever has the keyboard
//     wait 5                      frames
//     pause 600                   milliseconds of real time, frames running -- for
//                                 what the interface does after a delay (a drawer
//                                 that opens when the pointer rests, and tucks)
//     expect drawn 20 [40]        pixels drawn in the frame on show: at least, at most
//     expect pixel 3 4 e08f40     a pixel's colour, or `empty`
//     expect elements Rectangle 1 how many of an element kind the active layer has
//     expect selected 16 [20]     pixels selected: exactly, or between
//     expect layers 2 / expect frames 3 / expect tool pencil
//     expect slices 1 / expect guides 2 / expect zoom 12 / expect brush 3
//     expect pointer 3 4          the canvas pixel under the pointer
//     expect frame 2              the frame being edited, from 1
//     expect rotation 90          the active layer's last Rotate, in degrees
//     expect scale 2 1            and its last Scale, across and down
//     expect offset 3 1           and its last Offset, in pixels
//     reference tests/ui/sheet.png  an image imported as a reference, as File >
//                                 Import reference does once the file is chosen
//     open shape_test.lsprite     a file opened, as a recent entry or a drop does
//     start                       the start screen brought up, as File > Start screen does
//     answer export-png FILE      a native file dialog answered, as the system's would:
//                                 open, save-as, save-copy, export-png, export-sheet,
//                                 export-animation, import-palette, export-palette,
//                                 import-reference, import-sheet, import-theme, export-theme,
//                                 project-folder, reference-folder. While a script runs the
//                                 dialogs are never shown: a click that asks for one only
//                                 records it
//     expect dialog export-png    ... and this says which was asked for (`none`: no
//                                 dialog was); reading it clears it
//     expect flag rulers on       a switch in the menus: rulers, guides, grid, snap,
//                                 symmetry-across, symmetry-down, preview, tile-grid,
//                                 isometric, autosave, history, slices, library, kit,
//                                 preferences, modified, tracks (the same layers in every
//                                 frame), onion, timeline, pinned -- `on` or `off`
//     expect tiled both           the tiled mode: off, across, down, both
//     role panel 1d2229           a role of the working theme set (what the picker
//                                 in Preferences > Appearance does); a program's own
//                                 role by its id: role demo.mark ff00ff
//     theme midnight              a theme chosen by its id, as Preferences does
//     scale 150                   the interface size, in percent (75 to 150)
//     language es                 the interface in a language the lang folder has (en:
//                                 English), as Preferences does
//     layout drawing              the dock rebuilt as drawing, animation or compact, as
//                                 Window > Layouts does -- by name, so it works in any language
//     expect fits all             no panel's contents wider than the panel (a label or a
//                                 button cut off at its edge): `all`, or one panel by its id
//     expect scrolls tool         a panel too narrow for what it holds has a sideways
//                                 scrollbar to reach it by
//     demo-roles                  registers three sample roles of a made-up program
//                                 (demo.mark, demo.label, demo.tint), as an
//                                 AppExtension would, so what they do can be checked
//     for-each theme              the lines up to `end-for`, once for every theme
//     for-each scale 100 150      ... or once for each value listed; inside, {theme}
//                                 or {scale} is the value, in any line. Loops nest.
//     end-for
//     remove FILE                 a file deleted (a test cleaning up after itself)
//     zoom 20                     the canvas zoom set outright (a fit depends on the
//                                 layout, so a script that needs 20 asks for it)
//     expect zoom 20 [24]         the zoom: exactly, or between
//     expect uiscale 150          the interface scale, in percent
//     expect theme oled           the theme in force, by its id
//     expect guide drawing        the user guide open at a topic, by its id; or `closed`
//     expect colour panel 1d2229  a role of the theme in force
//     expect file FILE            a file exists / expect nofile FILE: it does not
//     expect section 9 0 8 8      the picked reference's part shown: x y w h
//     expect playing yes|no
//     expect canvas 64 64 / expect tabs 2
//     shot path.png               a screenshot of the next frame (.png, or .bmp)

#pragma once

#include "ui/editor.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

class CanvasView;

// Gives the widget just drawn a label a script can find it by, for the ones
// ImGui does not name -- a combo -- so `click-on Ink mode` opens it.
void nameForScripts(const char* label);

// Counts what ImGui reports about how the interface was built, and keeps its red
// boxes and its asserts off the person's screen; a script fails on the first.
// Call once, after the context is made.
void watchInterfaceErrors();

class UiScript {
public:
    // Reads and parses a script. False, with the line that is wrong, when it
    // does not parse.
    bool load(const std::string& path, std::string* error);

    bool active() const { return loaded_ && !finished_; }
    bool finished() const { return finished_; }
    int  failures() const { return failures_; }
    int  checks() const { return checks_; }

    // Once a frame, after the platform backend has queued its own input and
    // before ImGui::NewFrame: runs the checks that are due and feeds the next
    // frame's input.
    void feed(Editor& editor, CanvasView& canvas);

    // For a script the program ended before its last step (a Quit): says what
    // was checked up to there, as the end of a script does.
    void finishEarly();

    // A screenshot asked for by `shot`, once the frame is drawn; empty when
    // none is due.
    std::string takeShot();

private:
    struct Step {
        enum Kind { Pos, Button, Key, Text, Wait, Tool, Colour, Expect, Shot, Widget, ScreenMove,
                    Reference, Open, Start, Answer, Remove, ZoomTo, Role,
                    Wheel, UseTheme, UseScale, DemoRoles, Pause, UseLanguage,
                    UseLayout } kind = Wait;
        float       x = 0.f, y = 0.f;      // Pos: canvas pixels, or a widget's label in text
        int         button = 0;
        int         index = 0;             // Widget: which of several, from 1; 0 the last drawn
        int         skip = 0;              // Widget: the steps after it that need it found
        bool        down = false;
        ImGuiKey    key = ImGuiKey_None;
        std::string text;                  // Text, Tool, Shot; Expect's what
        std::vector<std::string> args;     // Expect
        int         line = 0;
    };

    bool parse(const std::string& line, int number, std::string* error);
    void expect(const Step& step, Editor& editor, CanvasView& canvas);

    std::vector<Step> steps_;
    size_t      next_ = 0;
    bool        loaded_ = false;
    bool        finished_ = false;
    int         failures_ = 0;
    int         checks_ = 0;
    int         warmup_ = 3;               // frames for the view to fit first
    bool        pointerSet_ = false;
    float       pointerX_ = 0.f, pointerY_ = 0.f;   // canvas pixels, or the window's
    bool        pointerOnWindow_ = false;           // after a click-on: window coordinates
    bool        reportedInterfaceError_ = false;    // ImGui's error, said once
    int         waitedFor_ = 0;                     // frames a click-on has waited for its widget
    uint64_t    pauseUntil_ = 0;                    // when a `pause` ends, in SDL ticks; 0 if none
    std::string shot_;
    std::string path_;
};

} // namespace fast
