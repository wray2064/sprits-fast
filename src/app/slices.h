// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// slices.h — named rectangles of the canvas, for a game to read.
//
// A slice says "this part of the sprite is the button", "the hitbox is here",
// "stretch this panel by its middle": a name, a rectangle, and optionally a
// nine-slice centre and a pivot. It draws nothing. It rides in the document
// -- kept with the canvas when the canvas is cropped, turned or resized --
// and comes out in the sheet's description, in Aseprite's layout where that
// is the layout asked for, so importers built for Aseprite's slices read
// Fast's.
//
// Kept as text on the document, one slice a line, read back as untrusted:
// a line that does not parse is dropped rather than guessed at.

#include "app/document.h"

#include <string>
#include <vector>

namespace fast {

struct Slice {
    std::string name;
    ls::Rect2i  bounds;                 // canvas pixels, [min, max)
    bool        nine = false;           // a nine-slice centre, inside the bounds
    ls::Rect2i  centre;                 // relative to the bounds' corner
    bool        hasPivot = false;
    ls::Vec2i   pivot;                  // relative to the bounds' corner
    ls::Color   colour { 70, 140, 240, 255 };
};

// Every slice of the document, in the order they were made.
std::vector<Slice> readSlices(Document& doc);

// Replaces them. Does not bracket an action; call inside one for undo.
bool writeSlices(Document& doc, const std::vector<Slice>& slices);

// A name no slice has yet: "Slice 1", "Slice 2", ...
std::string freeSliceName(const std::vector<Slice>& slices);

// The text the slices are kept as, exposed for the tests.
std::string encodeSlices(const std::vector<Slice>& slices);
std::vector<Slice> decodeSlices(const std::string& text);

} // namespace fast
