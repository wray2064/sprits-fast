// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// start_art.h — a small picture for each preset on the start screen.
//
// A card for "Character 64 x 64" that shows an empty chequer says only how big the
// canvas is. These say what it is for: a heart for an icon, a brick wall for a tile,
// a figure, a face, a landscape, a handheld. They are drawn with the draw list in a few
// shapes, in colours of their own (decoration, like the logo, not part of the
// interface's palette), so they are as sharp at 150% as at 100% and cost nothing to ship.

#include <imgui.h>

#include <string>

namespace fast {

// Draws the picture for the preset called `name` ("Icon", "Tile", "Character",
// "Portrait", "Scene", "Handheld") inside the rectangle from..to, clipped to it.
// A name it does not know draws nothing.
void drawPresetArt(ImDrawList* draw, const std::string& name, ImVec2 from, ImVec2 to);

} // namespace fast
