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
//     drag 3 3 20 20 [steps] [right]
//     hold shift|ctrl|alt|space   a key held down until `let`
//     let shift|ctrl|alt|space
//     key ctrl+z                  a chord pressed and released
//     type hello                  text into whatever has the keyboard
//     wait 5                      frames
//     expect drawn 20 [40]        pixels drawn in the frame on show: at least, at most
//     expect pixel 3 4 e08f40     a pixel's colour, or `empty`
//     expect elements Rectangle 1 how many of an element kind the active layer has
//     expect selected 16 [20]     pixels selected: exactly, or between
//     expect layers 2 / expect frames 3 / expect tool pencil
//     expect slices 1 / expect zoom 12 / expect brush 3
//     shot path.bmp               a screenshot of the next frame

#pragma once

#include "ui/editor.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace fast {

class CanvasView;

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

    // A screenshot asked for by `shot`, once the frame is drawn; empty when
    // none is due.
    std::string takeShot();

private:
    struct Step {
        enum Kind { Pos, Button, Key, Text, Wait, Tool, Colour, Expect, Shot } kind = Wait;
        float       x = 0.f, y = 0.f;      // Pos: canvas pixels
        int         button = 0;
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
    float       pointerX_ = 0.f, pointerY_ = 0.f;   // canvas pixels
    std::string shot_;
    std::string path_;
};

} // namespace fast
