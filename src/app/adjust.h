// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// adjust.h — hue, saturation, lightness, brightness, contrast and invert.
//
// In a bitmap editor these are filters: every pixel is read, changed and
// written back, and what the picture was made of is gone. Here nothing is a
// pixel to begin with. A layer's colours live in its elements -- a fill's
// colour, a stroke's, an outline's, the two ends of a dither -- or in palette
// slots the elements paint through. An adjustment changes those, and only
// those: every element is still the element it was, a slot is still a slot,
// and a second adjustment starts from colours rather than from a picture
// already rounded once.

#include "app/document.h"
#include "app/palette.h"

#include <string>
#include <vector>

namespace fast {

struct ColourAdjust {
    float hue = 0.f;            // degrees, round the wheel
    float saturation = 0.f;     // -1 grey .. +1 as vivid as it goes
    float lightness = 0.f;      // -1 black .. +1 white, in proportion to the room left
    float brightness = 0.f;     // -1 .. +1, added to every channel
    float contrast = 0.f;       // -1 flat grey .. +1, about the middle
    bool  invert = false;

    bool identity() const {
        return hue == 0.f && saturation == 0.f && lightness == 0.f && brightness == 0.f &&
               contrast == 0.f && !invert;
    }
};

// One colour adjusted. Alpha is kept.
ls::Color adjustColour(ls::Color colour, const ColourAdjust& adjust);

// What an adjustment of some layers touches, as it was: every colour an
// element paints in its own right, and -- when asked -- the palette slots
// those elements paint through, which every layer using them will follow.
struct AdjustBase {
    struct Site {
        ls::LayerId     layer;
        ls::OperationId op;
        ls::RegionId    region;
        bool            dither = false;
        std::string     param;           // the colour parameter, for a plain one
        ls::Color       colour;          // or a dither's first end
        ls::Color       second;          // and its second
        bool            firstOwn = true; // a dither end painted in its own colour
        bool            secondOwn = true;
    };
    std::vector<Site>         sites;
    ls::PaletteId             palette;
    std::vector<PaletteEntry> slots;
};

AdjustBase adjustBase(Document& doc, const std::vector<ls::LayerId>& layers, bool includeSlots);

// Sets everything `base` recorded to its recorded colour adjusted -- always
// from the record, so a slider dragged back to zero returns exactly what was
// there. Does not bracket an action.
bool applyAdjust(Document& doc, const AdjustBase& base, const ColourAdjust& adjust);

} // namespace fast
