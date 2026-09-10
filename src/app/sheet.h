// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// sheet.h — many frames laid into one image.
//
// The promise this is built around, and the one worth testing:
//
//   **A cell in a sheet is byte-for-byte what exporting that frame alone would
//   have produced.**
//
// That is not automatic. The engine lets a pattern be anchored in *export*
// space, which means the lattice is pinned to wherever the output frame sits
// inside something larger -- so compiling a frame at cell (2, 1) rather than at
// the origin genuinely changes its pixels, by design. Proven in the engine's
// frame_tests: one pixel of export origin moves a dither, and a whole tile of it
// puts the dither back.
//
// So each cell is compiled at the origin, exactly as a single-frame export is,
// and then composited. The alternative is offered as a choice rather than
// happening by accident: `patternAcrossSheet` sets each cell's export origin to
// its position, which makes a screen run continuously across the whole sheet.
// Nothing else in this program can do that, and nothing else should do it
// without being asked.
//
// A cycle may play one frame twice, and a sheet exported from a cycle therefore
// repeats that cell. That is the useful behaviour: the sheet plays correctly by
// stepping through its cells in order, which is all a consumer wants to do.

#include "app/animation.h"
#include "app/document.h"
#include "app/export_png.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// How the cells are arranged.
enum class SheetLayout : uint8_t {
    Grid,      // `columns`, or as near square as the count allows
    Row,       // one row: the classic strip
    Column,
};

struct SheetSettings {
    uint32_t    scale = 1;                  // whole numbers, like every export here
    SheetLayout layout = SheetLayout::Grid;

    // Columns for Grid. Zero means choose: the arrangement closest to square,
    // which keeps a long animation from becoming an image no viewer will open
    // at a sensible size.
    int columns = 0;

    // Pin each cell's patterns to its position in the sheet rather than to the
    // cell. Off by default, because on means a cell no longer matches what the
    // editor showed. Only affects patterns anchored in Export space; everything
    // else ignores it.
    bool patternAcrossSheet = false;

    // Write a small description of the sheet beside the image: where each cell
    // is, how long it is held, and what the cycles are. Without it a consumer
    // has only the picture.
    bool writeManifest = true;
};

// Where the cells go. Worked out without an engine or a document, so the
// arithmetic that decides whether an image is 3 MB or 3 GB can be tested on its
// own.
struct SheetPlan {
    int      columns = 0;
    int      rows = 0;
    uint32_t cellWidth = 0;      // before scaling
    uint32_t cellHeight = 0;
    uint32_t width = 0;          // the finished image, after scaling
    uint32_t height = 0;
    int      cells = 0;

    // The top-left of cell `index` in the finished image, in scaled pixels.
    ls::Vec2i positionOf(int index) const;
};

// Fills `out`, or explains why not. Refuses a sheet too large to be an image
// somebody can open, rather than trying and failing on the allocation.
bool planSheet(int frameCount, uint32_t cellWidth, uint32_t cellHeight,
               const SheetSettings& settings, SheetPlan* out, std::string* error);

// Compiles each frame at export quality and composites the sheet.
//
// `frames` is the sprites to lay out, in the order they should appear -- every
// frame in the document, or the steps of one cycle, which is the caller's
// decision because only the caller knows which the person asked for.
bool composeSheet(Document& doc, const std::vector<ls::SpriteId>& frames,
                  const SheetSettings& settings, ls::RasterBuffer* out,
                  SheetPlan* plan, std::string* error);

// The manifest, as text. Separate from writing it so it can be tested without a
// filesystem, and so the exact bytes are something a test can state.
//
// `imageName` is the file name of the PNG, not a path: a manifest that names an
// absolute path stops being portable the moment the folder is moved or sent to
// somebody else.
std::string sheetManifest(const SheetPlan& plan, const std::vector<Frame>& frames,
                          const std::vector<int>& steps,
                          const std::vector<Cycle>& cycles,
                          const std::string& imageName, uint32_t scale);

// Compiles, composites and writes. The PNG is written atomically like every
// other file here; the manifest follows it, and a failure to write the manifest
// is reported without unwriting the image -- the picture is the thing somebody
// asked for.
//
// `steps` names, for each cell, which entry of `frames` it draws. It is what
// lets a sheet exported from a cycle repeat a frame and still describe itself.
bool exportSheetToPng(Document& doc, const std::vector<Frame>& frames,
                      const std::vector<int>& steps,
                      const std::vector<Cycle>& cycles,
                      const std::string& path, const SheetSettings& settings,
                      std::string* error);

} // namespace fast
