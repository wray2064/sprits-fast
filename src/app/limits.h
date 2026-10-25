// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// limits.h — how large the work may grow, and how a person lifts that.
//
// The defaults are chosen for practicality and for how the program feels: a canvas
// of 4096 x 4096, 512 frames, a brush of 32 pixels keep a compile, the timeline's
// strip and the interface quick on an ordinary machine. They are not a product
// tier: Preferences > Experimental lifts every one of them, to what the machine
// can hold, for a person who wants that and accepts it may be slow.
//
// What the limits govern is *making* more: a bigger canvas, another frame, another
// part. What a reader accepts of a file is separate (the kRead constants): always
// more than any lifted limit, so a document made with the limits lifted opens, and
// is saved again, whole in a session that has them on.

#include <cstddef>
#include <cstdint>

namespace fast {

struct Limits {
    uint32_t canvasDimension = 16384;                // a side, in pixels
    uint64_t canvasPixels = 4096ull * 4096ull;       // the area
    size_t   frames = 512;
    size_t   cycles = 64;
    size_t   framesPerCycle = 1024;                  // a cycle may repeat a frame
    size_t   parts = 256;                            // cutout parts in a document
    size_t   references = 16;
    size_t   referenceBytes = 8u * 1024u * 1024u;    // one reference's PNG
    size_t   referenceTotalBytes = 16u * 1024u * 1024u;
    int      brushSize = 32;
    int      outlineThickness = 8;
    int      shadowOffset = 16;
};

// What is in force: the defaults, or the lifted ones.
const Limits& limits();
const Limits& defaultLimits();
// The limits lifted: as large as the machine can hold (the canvas area follows the
// memory the machine has), and never less than the defaults.
const Limits& liftedLimits();

// Whether the lifted limits are in force. Takes effect for what is made or opened
// afterwards: a document already open keeps the canvas limit it was made under.
bool limitsLifted();
void setLimitsLifted(bool lifted);

// What a reader keeps of a file, however the limits are set (more than liftedLimits()).
constexpr size_t kReadCycles = 4096;
constexpr size_t kReadFramesPerCycle = 65536;
constexpr size_t kReadReferences = 256;

} // namespace fast
