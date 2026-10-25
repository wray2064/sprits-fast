// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/limits.h"

#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fast {

namespace {

bool gLifted = false;

// The memory the machine has, in bytes; a modest guess where it cannot be asked.
uint64_t physicalMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<uint64_t>(status.ullTotalPhys);
    }
#endif
    return 8ull * 1024ull * 1024ull * 1024ull;
}

Limits makeLifted() {
    Limits lifted;
    // A pixel costs a few bytes in each layer's raster and in the compiled picture; a
    // quarter of the memory spread over 64 bytes a pixel keeps a canvas well within it.
    const uint64_t byMemory = physicalMemory() / 4 / 64;
    lifted.canvasPixels = std::max(lifted.canvasPixels, byMemory);
    lifted.canvasDimension = 32768;
    lifted.frames = 8192;
    lifted.cycles = 1024;
    lifted.framesPerCycle = 16384;
    lifted.parts = 4096;
    lifted.references = 128;
    lifted.referenceBytes = 64u * 1024u * 1024u;
    lifted.referenceTotalBytes = static_cast<size_t>(
        std::min<uint64_t>(physicalMemory() / 8, 1024ull * 1024ull * 1024ull));
    lifted.referenceTotalBytes = std::max(lifted.referenceTotalBytes, lifted.referenceBytes);
    lifted.brushSize = 256;
    lifted.outlineThickness = 32;
    lifted.shadowOffset = 64;
    return lifted;
}

} // namespace

const Limits& defaultLimits() {
    static const Limits defaults;
    return defaults;
}

const Limits& liftedLimits() {
    static const Limits lifted = makeLifted();
    return lifted;
}

const Limits& limits() {
    return gLifted ? liftedLimits() : defaultLimits();
}

bool limitsLifted() {
    return gLifted;
}

void setLimitsLifted(bool lifted) {
    gLifted = lifted;
}

static_assert(kReadCycles >= 1024 && kReadFramesPerCycle >= 16384 && kReadReferences >= 128,
              "a reader keeps more than any lifted limit");

} // namespace fast
