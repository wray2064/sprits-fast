// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// batch.h — Fast without a window: open, export, exit.
//
// A sprite's journey usually ends in a build script, not a menu. So the same
// exports the File menu has are reachable from the command line:
//
//     sprits_fast --export hero.lsprite --to hero.gif --scale 4 --cycle walk
//
// The output's extension decides what is written:
//
//   .png       one frame (--frame N, the first by default); with --sheet a
//              sprite sheet and its manifest; with --sequence one file per step;
//              with --animated an animated PNG
//   .gif       the animation
//   .lsprite   the document itself -- which is how an .aseprite, a PNG or a GIF
//              is converted into one
//   .gpl .hex .pal .act   the palette
//
// The input is anything Open takes: .lsprite, .aseprite/.ase, or an image.
// Nothing here touches the interface; it is fast_core, and it is tested.

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

struct BatchJob {
    std::string input;
    std::string output;
    uint32_t    scale = 1;
    std::string cycle;          // a cycle's name; empty is every frame
    int         frame = 1;      // for a single-frame PNG, counted from 1
    bool        sheet = false;
    bool        sequence = false;
    bool        animated = false;
};

// Reads the batch options. False when --export is not among them -- this is
// not a batch run -- and false with `error` set when it is and they are wrong.
bool parseBatch(const std::vector<std::string>& args, BatchJob* out, std::string* error);

// Runs the job. Returns the process exit code: 0 for success. `message` says
// what was written, or why nothing was.
int runBatch(const BatchJob& job, std::string* message);

} // namespace fast
