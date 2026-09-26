// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// slices_tests.cpp — named rectangles of the canvas.
//
// Slices survive the text they are kept as, a bad line is dropped rather than
// guessed at, a nine-slice centre outside its bounds is no centre, they are
// kept by a file and undone like anything else, they move with the canvas,
// and both descriptions of a sheet carry them -- Aseprite's in its own words.

#include "app/animation.h"
#include "app/canvas_ops.h"
#include "app/document.h"
#include "app/file_io.h"
#include "app/guides.h"
#include "app/sheet.h"
#include "app/slices.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

namespace {

using namespace fast;

Slice button() {
    Slice s;
    s.name = "button";
    s.bounds = { { 2, 3 }, { 12, 9 } };
    s.nine = true;
    s.centre = { { 2, 2 }, { 8, 4 } };
    s.hasPivot = true;
    s.pivot = { 5, 6 };
    s.colour = { 10, 200, 30, 255 };
    return s;
}

bool same(const Slice& a, const Slice& b) {
    return a.name == b.name && a.bounds.min.x == b.bounds.min.x && a.bounds.min.y == b.bounds.min.y &&
           a.bounds.max.x == b.bounds.max.x && a.bounds.max.y == b.bounds.max.y && a.nine == b.nine &&
           a.centre.min.x == b.centre.min.x && a.centre.max.y == b.centre.max.y &&
           a.hasPivot == b.hasPivot && a.pivot.x == b.pivot.x && a.pivot.y == b.pivot.y &&
           a.colour.g == b.colour.g;
}

void testTheText() {
    Slice plain;
    plain.name = "hit\tbox";                   // a tab cannot live in a line
    plain.bounds = { { -4, 0 }, { 4, 16 } };
    const std::vector<Slice> back = decodeSlices(encodeSlices({ button(), plain }));
    REQUIRE(back.size() == 2);
    CHECK(same(back[0], button()));
    CHECK(back[1].name == "hit box" && !back[1].nine && !back[1].hasPivot);
    CHECK(back[1].bounds.min.x == -4 && back[1].bounds.height() == 16);

    // Bad lines go; a centre outside the bounds is no centre.
    const std::vector<Slice> hostile = decodeSlices(
        "no numbers\n"
        "zero\t0\t0\t0\t5\t0\t0\t0\t0\t0\t0\t0\t0\t000000\n"
        "wide\t0\t0\t4\t4\t1\t1\t1\t9\t2\t0\t0\t0\tffffff\n"
        "ok\t1\t1\t2\t2\t0\t0\t0\t0\t0\t0\t0\t0\t00ff00\n");
    REQUIRE(hostile.size() == 2);
    CHECK(hostile[0].name == "wide" && !hostile[0].nine);
    CHECK(hostile[1].name == "ok" && hostile[1].colour.g == 255);
    CHECK(freeSliceName({ button() }) == "Slice 1");
    Slice one;
    one.name = "Slice 1";
    CHECK(freeSliceName({ one }) == "Slice 2");
}

void testKeptAndUndone() {
    Document doc;
    REQUIRE(doc.create("slices", 16, 16));
    CHECK(readSlices(doc).empty());
    doc.beginAction("Slice");
    REQUIRE(writeSlices(doc, { button() }));
    doc.endAction();
    REQUIRE(readSlices(doc).size() == 1);
    CHECK(doc.undo() && readSlices(doc).empty());
    CHECK(doc.redo() && readSlices(doc).size() == 1);

    std::string error;
    const std::string path = "fast_slices.lsprite";
    REQUIRE(doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);
    const std::vector<Slice> back = readSlices(again);
    REQUIRE(back.size() == 1);
    CHECK(same(back[0], button()));
}

void testTheyMoveWithTheCanvas() {
    Document doc;
    REQUIRE(doc.create("slices", 16, 16));
    REQUIRE(writeSlices(doc, { button() }));
    std::string error;
    REQUIRE(enlargeSprite(doc, 2, &error));
    std::vector<Slice> s = readSlices(doc);
    REQUIRE(s.size() == 1);
    CHECK(s[0].bounds.min.x == 4 && s[0].bounds.max.x == 24 && s[0].bounds.max.y == 18);
    CHECK(s[0].centre.min.x == 4 && s[0].centre.max.x == 16);
    CHECK(s[0].pivot.x == 10 && s[0].pivot.y == 12);
    REQUIRE(cropCanvas(doc, { { 4, 4 }, { 28, 28 } }, &error));
    s = readSlices(doc);
    CHECK(s[0].bounds.min.x == 0 && s[0].bounds.min.y == 2);
}

void testTheyAreInTheDescriptions() {
    Document doc;
    REQUIRE(doc.create("slices", 16, 16));
    const std::vector<Frame> frames = readFrames(doc);
    SheetSettings settings;
    SheetPlan plan;
    std::string error;
    REQUIRE(planSheet(1, 16, 16, settings, &plan, &error));
    const std::string fast = sheetManifest(plan, frames, { 0 }, {}, "s.png", 2, { button() });
    CHECK(fast.find("\"slices\": [") != std::string::npos);
    CHECK(fast.find("\"name\": \"button\", \"bounds\": { \"x\": 4, \"y\": 6, \"width\": 20, \"height\": 12 }") !=
          std::string::npos);
    CHECK(fast.find("\"pivot\": { \"x\": 10, \"y\": 12 }") != std::string::npos);
    const std::string ase = asepriteManifest(plan, frames, { 0 }, {}, "s.png", 1, true, { button() });
    CHECK(ase.find("\"name\": \"button\", \"color\": \"#0ac81eff\", \"keys\": [{ \"frame\": 0, "
                   "\"bounds\": { \"x\": 2, \"y\": 3, \"w\": 10, \"h\": 6 }, "
                   "\"center\": { \"x\": 2, \"y\": 2, \"w\": 6, \"h\": 2 }, "
                   "\"pivot\": { \"x\": 5, \"y\": 6 } }] }") != std::string::npos);
    const std::string none = asepriteManifest(plan, frames, { 0 }, {}, "s.png", 1, true);
    CHECK(none.find("\"slices\": []") != std::string::npos);
}

// Guides: their text, a document keeping them, and the canvas turning them.
void testGuides() {
    const std::vector<Guide> back = decodeGuides(encodeGuides({ { true, 4 }, { false, -2 } }) +
                                                 ";x9;v;h1a;v7");
    REQUIRE(back.size() == 3);
    CHECK(back[0].vertical && back[0].at == 4);
    CHECK(!back[1].vertical && back[1].at == -2);
    CHECK(back[2].vertical && back[2].at == 7);

    Document doc;
    REQUIRE(doc.create("guides", 16, 8));
    REQUIRE(writeGuides(doc, { { true, 4 }, { false, 2 } }));
    std::string error;
    REQUIRE(rotateCanvas(doc, 1, &error));           // 16 x 8 turns to 8 x 16, clockwise
    const std::vector<Guide> turned = readGuides(doc);
    REQUIRE(turned.size() == 2);
    // Clockwise, (x, y) goes to (h - y, x): the line down at x = 4 comes out
    // across at y = 4, and the line across at y = 2 comes out down at x = 6.
    CHECK(!turned[0].vertical && turned[0].at == 4);
    CHECK(turned[1].vertical && turned[1].at == 6);
}

} // namespace

int main() {
    testTheText();
    testKeptAndUndone();
    testTheyMoveWithTheCanvas();
    testTheyAreInTheDescriptions();
    testGuides();
    if (failures == 0) {
        std::printf("slices: all passed\n");
        return 0;
    }
    std::printf("slices: %d failure(s)\n", failures);
    return 1;
}
