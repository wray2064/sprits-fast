// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// text.h — writing on a sprite, and changing what it says afterwards.
//
// In a bitmap editor text is typed once and becomes pixels: a typo means
// erasing and typing again. Here a piece of text is an element -- its glyphs'
// pixels as a region, coloured by an ink -- and the text, where it sits and
// its size are kept on the region. So the Element panel can retype it, move
// it or scale it, and the pixels are rebuilt from the words. Its colour is an
// ink like any other, so a slot recolours it.
//
// It behaves like a shape on its layer: fresh paint lands over it rather than
// into it, and the eraser leaves it for the element list to remove.

#include "app/document.h"
#include "app/ink.h"
#include "app/paint.h"

#include <string>

namespace fast {

constexpr size_t kMaxTextLength = 1024;

struct TextSpec {
    std::string text;
    ls::Vec2i   at { 0, 0 };     // top-left of the first line
    int         scale = 1;       // font pixels to canvas pixels
};

// Adds a text element on top of `layer`. Does not bracket an action.
bool addTextElement(Document& doc, ls::LayerId layer, const TextSpec& spec, const Ink& ink,
                    PaintLayer* out);

// What a region says, when it is text. False for any other region.
bool readTextElement(Document& doc, ls::RegionId region, TextSpec* out);

// Retypes, moves or rescales a text element: the region is rebuilt from the
// words. Does not bracket an action.
bool updateTextElement(Document& doc, ls::RegionId region, const TextSpec& spec);

} // namespace fast
