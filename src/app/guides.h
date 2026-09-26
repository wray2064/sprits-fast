// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// guides.h — lines across the canvas to line things up against.
//
// A guide is a line between two columns or two rows of pixels, dragged out of
// a ruler. It draws nothing. It is kept with the document, moves with the
// canvas when the canvas is turned or cropped, and -- while snapping is on --
// is one more line that corners and moves snap to.

#include "app/document.h"

#include <string>
#include <vector>

namespace fast {

struct Guide {
    bool    vertical = true;     // a line down the canvas at x, or across it at y
    int32_t at = 0;              // the pixel boundary it is on
};

std::vector<Guide> readGuides(Document& doc);

// Replaces them. Does not bracket an action.
bool writeGuides(Document& doc, const std::vector<Guide>& guides);

std::string encodeGuides(const std::vector<Guide>& guides);
std::vector<Guide> decodeGuides(const std::string& text);

} // namespace fast
