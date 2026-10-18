// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// start_screen.h — what greets the person when the program opens.
//
// Every editor of this kind opens on a choice rather than on a canvas: make
// something new, open something, go back to what was being worked on. Fast
// always has a document under the window (a blank one, which the first file
// opened or made replaces), so the start screen is a layer over it, not a
// state of its own: each choice goes through the same requestAction the File
// menu uses, so a new file, an opened one and a dropped one behave the same
// from here as from there.
//
// It shows at start when Preferences::showStart says so, and any time from
// File > Start screen. It never opens in a scripted or captured run unless
// asked (--start), so what those check does not depend on it.
//
// This file also holds what the program calls itself -- its name, version,
// links and logo -- because the start screen was the first thing to need
// them; Help > About reads them from here.

#include "ui/app_extension.h"
#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Texture;

namespace fast {

struct ProgramInfo {
    std::string title;
    std::string version;
    std::string homepage;
    std::string issues;
    std::string licence;
};

// The program as the window has it: the extension's name and links, and the
// logo (PNG bytes). Once, after the window exists.
void configureProgram(const AppExtension& extension, const std::vector<uint8_t>& logoPng);
const ProgramInfo& programInfo();

// The logo as a texture, made on first use; null when there is none or it
// will not decode.
SDL_Texture* programLogo(SDL_Window* window);

// Brings the start screen up: re-reads what the recent files are now, and
// remembers what was on screen so that any change of document closes it.
void openStartScreen(Editor& editor);

// Draws it when it is up. Waits while a recovery prompt is being answered,
// which is the more urgent question.
void drawStartScreen(Editor& editor, CanvasView& canvas, SDL_Window* window);

} // namespace fast
