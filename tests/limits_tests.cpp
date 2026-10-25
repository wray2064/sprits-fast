// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// limits_tests.cpp — the size limits are comfortable defaults a person can lift, and
// what a file holds is never cut by them.

#include "app/animation.h"
#include "app/document.h"
#include "app/limits.h"
#include "app/preferences.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

using namespace fast;

void testLiftedIsNeverLessThanTheDefaults() {
    const Limits& d = defaultLimits();
    const Limits& l = liftedLimits();
    CHECK(l.canvasDimension >= d.canvasDimension);
    CHECK(l.canvasPixels >= d.canvasPixels);
    CHECK(l.frames > d.frames);
    CHECK(l.cycles > d.cycles);
    CHECK(l.framesPerCycle > d.framesPerCycle);
    CHECK(l.parts > d.parts);
    CHECK(l.references > d.references);
    CHECK(l.referenceBytes > d.referenceBytes);
    CHECK(l.referenceTotalBytes >= d.referenceTotalBytes);
    CHECK(l.brushSize > d.brushSize);
    CHECK(l.outlineThickness > d.outlineThickness);
    CHECK(l.shadowOffset > d.shadowOffset);
    // A reader keeps more than any lifted limit.
    CHECK(kReadCycles >= l.cycles && kReadFramesPerCycle >= l.framesPerCycle &&
          kReadReferences >= l.references);
}

void testTheSwitch() {
    setLimitsLifted(false);
    CHECK(!limitsLifted());
    CHECK(limits().frames == defaultLimits().frames);
    setLimitsLifted(true);
    CHECK(limitsLifted());
    CHECK(limits().frames == liftedLimits().frames);
    setLimitsLifted(false);
}

void testTheCanvasFollowsTheLimits() {
    // 8192 x 4096 is twice the default area: refused, then made once lifted.
    setLimitsLifted(false);
    {
        Document doc;
        CHECK(!doc.create("big", 8192, 4096));
    }
    setLimitsLifted(true);
    {
        Document doc;
        CHECK(doc.create("big", 8192, 4096));
    }
    setLimitsLifted(false);
}

void testCyclesAreCappedWhenMadeAndNotWhenRead() {
    setLimitsLifted(false);
    Document doc;
    CHECK(doc.create("c", 8, 8));
    for (size_t i = 0; i < limits().cycles + 8; ++i) {
        addCycle(doc, "c" + std::to_string(i), 1);
    }
    CHECK(readCycles(doc, 1).size() == limits().cycles);

    // With the limits lifted more are made, and a session without them still reads
    // and keeps every one.
    setLimitsLifted(true);
    for (size_t i = 0; i < defaultLimits().cycles + 40; ++i) {
        addCycle(doc, "d" + std::to_string(i), 1);
    }
    const size_t made = readCycles(doc, 1).size();
    CHECK(made > defaultLimits().cycles);
    setLimitsLifted(false);
    CHECK(readCycles(doc, 1).size() == made);
}

void testThePreferenceIsKept() {
    Preferences p;
    CHECK(!p.liftLimits);
    p.liftLimits = true;
    CHECK(loadPreferences(savePreferences(p)).liftLimits);
    p.liftLimits = false;
    CHECK(!loadPreferences(savePreferences(p)).liftLimits);
}

} // namespace

int main() {
    testLiftedIsNeverLessThanTheDefaults();
    testTheSwitch();
    testTheCanvasFollowsTheLimits();
    testCyclesAreCappedWhenMadeAndNotWhenRead();
    testThePreferenceIsKept();
    if (failures != 0) {
        std::printf("limits: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("limits: all checks passed\n");
    return 0;
}
