// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// clip_image.h — the clipboard other programs read.
//
// A copy inside Fast keeps colours as palette slots and dithers as rules
// (PixelClip), which no other program understands: they read a picture. So a
// copy also goes onto the system clipboard as an image, and an image another
// program put there pastes back as a clip -- one piece per colour, where a
// colour that is exactly one of the palette's takes that slot, so a sprite
// copied out of a browser or another editor lands already recolourable.
//
// Pasted pictures are not all pixel art. A photograph or a smoothed
// screenshot has thousands of colours, and a piece per colour would bury the
// layer, so past kMaxClipColours each pixel takes the nearest slot instead --
// said out loud, since it changes the picture.

#include "app/floating.h"
#include "app/palette.h"

#include <cstdint>
#include <vector>

namespace fast {

constexpr size_t kMaxClipColours = 256;

// A layer on its own, canvas-sized RGBA, as it draws: dithers and shapes
// included, the layer's opacity and blend not.
bool layerImage(Document& doc, ls::LayerId layer, ls::RasterBuffer* out);

// What `mask` covers of a canvas-sized image, cropped to the mask's bounds and
// transparent outside it. Empty when the mask misses the image.
ls::RasterBuffer imageOfMask(const ls::RasterBuffer& canvas, const ls::IntervalSet& mask);

struct ImageClipReport {
    size_t colours = 0;       // distinct colours in the clip
    size_t slots = 0;         // how many of them are palette slots
    bool   reduced = false;   // there were too many, and each took the nearest slot
};

// An image as a clip, its top-left at `at`. Fully transparent pixels are left
// out. False, with nothing written, when there is nothing opaque, or when the
// image needs reducing and `palette` is empty.
bool clipFromImage(const ls::RasterBuffer& image, ls::Vec2i at,
                   const std::vector<PaletteEntry>& palette, PixelClip* out,
                   ImageClipReport* report);

// Where a pasted image goes: centred on the canvas, or at the canvas's
// top-left when it is larger on that side.
ls::Vec2i pastePosition(uint32_t imageWidth, uint32_t imageHeight, uint32_t canvasWidth,
                        uint32_t canvasHeight);

// A 32-bit BMP file with alpha (a BITMAPV5 header, rows bottom-up), for the
// programs that read only the clipboard's bitmap format.
std::vector<uint8_t> encodeBmp(const ls::RasterBuffer& image);

} // namespace fast
