// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// import_image.h — a picture made elsewhere, opened as a document.
//
// Everyone arriving at Fast has sprites already, as PNGs and GIFs. Opening one
// has to give back something Fast can work on as if it had been drawn here, not
// a flattened picture the tools can only paint over.
//
// So the picture is taken apart by colour. Every distinct colour becomes an ink:
// a region holding exactly the pixels of that colour, and a fill that colours
// them. When the whole picture uses no more colours than a palette holds, those
// colours *become* the palette, in the order they first appear, and every pixel
// is painted through its slot -- so the first thing a person can do with an
// imported sprite is recolour it by editing a swatch, which is the thing this
// program is for. A picture with more colours than that keeps them as colours
// of their own; one with a great many more is a photograph, and is refused with
// the suggestion to import it as a reference instead, which is what it is.
//
// An animated GIF opens as frames, each holding for as long as the GIF said.

#include "app/document.h"

#include <livesprite/livesprite.h>

#include <string>
#include <vector>

namespace fast {

// More distinct colours than this and the picture is not pixel art: every
// colour is an element to compile, and ten thousand of them is a photograph
// being treated as a drawing.
constexpr size_t kMaxImportColours = 4096;

// Up to this many colours become the palette, and every pixel paints through
// a slot.
constexpr size_t kMaxImportSlots = 256;

struct ImportReport {
    size_t frames = 0;
    size_t colours = 0;
    bool   throughPalette = false;    // every pixel paints through a slot
};

// Builds `doc` afresh from frames that share one size. `holdsMs` gives each
// frame's hold; zero or a missing entry takes the default.
bool documentFromFrames(Document& doc, const std::string& name,
                        const std::vector<ls::RasterBuffer>& frames,
                        const std::vector<int>& holdsMs,
                        ImportReport* report, std::string* error);

// Reads an image file -- PNG, GIF (every frame), JPEG or BMP -- and builds `doc`
// from it. The document has no path afterwards: saving asks where to put the
// .lsprite, and the image it came from is never written over by a save.
bool openImageAsDocument(Document& doc, const std::string& path,
                         ImportReport* report, std::string* error);

// Slices one image into frames on a grid of `cellWidth` x `cellHeight`, row by
// row, skipping cells that are entirely transparent at the end -- a sheet whose
// last row is half full. Cells that do not fit whole are left out.
bool sliceSheet(const ls::RasterBuffer& sheet, uint32_t cellWidth, uint32_t cellHeight,
                std::vector<ls::RasterBuffer>* cells, std::string* error);

} // namespace fast
