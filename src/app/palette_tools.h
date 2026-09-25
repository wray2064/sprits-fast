// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// palette_tools.h — organising a palette, and building one.
//
// The palette panel already names, edits and swaps. What a pixel artist also
// does with a palette is arrange it -- sort it, drag a slot next to its
// neighbours, fill a ramp between two colours -- and push the whole thing
// warmer or darker at once. Here each of those is a palette edit, which is
// the point: every pixel painted through a slot follows, on every frame, and
// none of them is touched. Shifting a palette's hue is a recolour of the
// sprite that costs one undo step and can be dragged back.
//
// Order is presentation (see the engine's setPaletteOrder): sorting and
// dragging reorder the swatches without renumbering any slot, so nothing a
// sprite paints through notices.

#include "app/document.h"
#include "app/palette.h"

#include <string>
#include <vector>

namespace fast {

enum class PaletteSort { Hue, Saturation, Lightness, Reverse };

// Reorders the swatches. Does not bracket an action.
bool sortPalette(Document& doc, ls::PaletteId palette, PaletteSort by);

// Puts one slot at `index` in the display order.
bool moveSlot(Document& doc, ls::PaletteId palette, ls::ColorRole role, int index);

// Sets the display order outright: a permutation of the palette's slots.
bool setSlotOrder(Document& doc, ls::PaletteId palette, const std::vector<ls::ColorRole>& order);

// `steps` new slots between `from` and `to`, evenly spaced in RGB, placed
// between them in the display order. Returns the new slots.
std::vector<ls::ColorRole> addRampBetween(Document& doc, ls::PaletteId palette,
                                          ls::ColorRole from, ls::ColorRole to, int steps);

// Every slot of `base` moved by a hue turn in degrees and a saturation and
// lightness change in -1..1, into `palette`. `base` is the palette as it was
// when the adjustment began, so dragging a slider back returns it exactly.
bool adjustPalette(Document& doc, ls::PaletteId palette, const std::vector<PaletteEntry>& base,
                   float hueDegrees, float saturation, float lightness);

// Turns every colour painted as a value of its own -- on every layer of every
// frame -- into a palette slot: an existing slot of exactly that colour when
// there is one, a new one otherwise. After this the whole sprite recolours
// from the palette. Returns how many elements now paint through a slot.
// Does not bracket an action.
int slotsFromColours(Document& doc, ls::PaletteId palette);

// Palettes that ship with Fast. Each is generated here or drawn up for Fast,
// so none carries anybody else's licence; famous palettes from elsewhere load
// through .gpl, .hex or .pal files, where their authors' terms travel with
// them.
struct PalettePreset {
    std::string            name;
    std::vector<ls::Color> colours;
};
const std::vector<PalettePreset>& palettePresets();

// Shades of a colour the way pixel art ramps them: darker steps turn toward
// blue and gain a little saturation, lighter ones turn toward yellow and give
// some up, rather than only mixing in black and white. 2 * steps + 1 colours,
// darkest first, the middle one `base` exactly. A grey stays grey.
std::vector<ls::Color> shadesOf(ls::Color base, int steps, float hueShiftDegrees = 20.f);

// HSL, for sorting and adjusting. Hue in degrees 0..360, the rest 0..1.
void rgbToHsl(ls::Color c, float* h, float* s, float* l);
ls::Color hslToRgb(float h, float s, float l, uint8_t alpha);

} // namespace fast
