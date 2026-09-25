// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// canvas_ops_tests.cpp — the canvas itself.
//
// The promises: four quarter turns and two flips give back the same bytes; a
// quarter turn swaps the canvas's sides and sends each pixel where a turn
// sends it; resizing about an anchor moves the drawing as the anchor says, and
// shrinking keeps what falls off the edge so growing brings it back; crop and
// trim land on the right rectangle; enlarging is exact and reducing undoes it;
// a rounded rectangle stays a rounded rectangle through all of it; every frame
// changes, not only the one being looked at; and each is one undo step.

#include "app/animation.h"
#include "app/canvas_ops.h"
#include "app/document.h"
#include "app/element.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/shape.h"

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

const ls::Color kRed  { 200, 30, 30, 255 };
const ls::Color kBlue { 30, 30, 200, 255 };

ls::Vec2i size(Document& doc) {
    return doc.engine().getCanvasSize(doc.id()).value;
}

ls::RasterBuffer picture(Document& doc, ls::SpriteId sprite) {
    const ls::Vec2i s = size(doc);
    auto compiled = doc.engine().compileSprite(
        sprite, compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(s.x),
                               static_cast<uint32_t>(s.y)));
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

ls::Color at(Document& doc, int x, int y) {
    return ls::readPixel(picture(doc, doc.sprite()), x, y);
}

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A 5 x 3 document with red at (0,0) and (1,0), blue at (4,2): lopsided on
// purpose, so a wrong mapping cannot land on the right answer by symmetry.
bool build(Document& doc, PaintLayer* layer) {
    if (!doc.create("canvas", 5, 3) ||
        !createPaintLayer(doc, doc.sprite(), "Layer 1", kRed, layer)) {
        return false;
    }
    for (const auto& [colour, pixels] :
         std::vector<std::pair<ls::Color, std::vector<ls::Vec2i>>>{
             { kRed, {{ 0, 0 }, { 1, 0 }} }, { kBlue, {{ 4, 2 }} } }) {
        InkStroke stroke;
        Ink ink;
        ink.colour = colour;
        doc.beginAction("Pencil");
        if (!beginInkStroke(doc, layer->layer, ink, &stroke) ||
            !strokeInk(doc, stroke, pixels)) {
            return false;
        }
        doc.endAction();
    }
    doc.clearHistory();
    return true;
}

void testQuarterTurns() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    const std::vector<uint8_t> before = picture(doc, doc.sprite()).pixels;
    std::string error;

    REQUIRE(rotateCanvas(doc, 1, &error));
    CHECK(size(doc).x == 3 && size(doc).y == 5);
    // Clockwise: (x, y) goes to (height - 1 - y, x).
    CHECK(same(at(doc, 2, 0), kRed));
    CHECK(same(at(doc, 2, 1), kRed));
    CHECK(same(at(doc, 0, 4), kBlue));
    CHECK(doc.undoLabel() == "Rotate canvas 90");

    REQUIRE(rotateCanvas(doc, 1, &error));
    REQUIRE(rotateCanvas(doc, 1, &error));
    REQUIRE(rotateCanvas(doc, 1, &error));
    CHECK(size(doc).x == 5 && size(doc).y == 3);
    CHECK(picture(doc, doc.sprite()).pixels == before);

    REQUIRE(rotateCanvas(doc, 3, &error));        // anticlockwise
    CHECK(same(at(doc, 0, 4), kRed));
    CHECK(same(at(doc, 2, 0), kBlue));
    REQUIRE(rotateCanvas(doc, 2, &error));
    REQUIRE(rotateCanvas(doc, 3, &error));
    CHECK(picture(doc, doc.sprite()).pixels == before);
}

void testFlips() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    const std::vector<uint8_t> before = picture(doc, doc.sprite()).pixels;
    std::string error;
    REQUIRE(flipCanvas(doc, true, &error));
    CHECK(same(at(doc, 4, 0), kRed) && same(at(doc, 3, 0), kRed));
    CHECK(same(at(doc, 0, 2), kBlue));
    REQUIRE(flipCanvas(doc, false, &error));
    CHECK(same(at(doc, 4, 2), kRed) && same(at(doc, 0, 0), kBlue));
    REQUIRE(flipCanvas(doc, true, &error));
    REQUIRE(flipCanvas(doc, false, &error));
    CHECK(picture(doc, doc.sprite()).pixels == before);
}

void testResizeKeepsWhatFallsOff() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    std::string error;
    REQUIRE(resizeCanvas(doc, 7, 5, CanvasAnchor::Centre, &error));
    CHECK(size(doc).x == 7 && size(doc).y == 5);
    CHECK(same(at(doc, 1, 1), kRed));             // moved one right, one down
    CHECK(same(at(doc, 5, 3), kBlue));

    // Shrink from the top left: blue falls off the edge...
    REQUIRE(resizeCanvas(doc, 3, 3, CanvasAnchor::TopLeft, &error));
    CHECK(same(at(doc, 1, 1), kRed));
    // ...and is still there when the canvas grows back.
    REQUIRE(resizeCanvas(doc, 7, 5, CanvasAnchor::TopLeft, &error));
    CHECK(same(at(doc, 5, 3), kBlue));

    CHECK(!resizeCanvas(doc, 0, 5, CanvasAnchor::Centre, &error));
    CHECK(!error.empty());
}

void testCropAndTrim() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    std::string error;
    REQUIRE(resizeCanvas(doc, 11, 9, CanvasAnchor::Centre, &error));     // margin all round
    const ls::Rect2i content = contentBounds(doc);
    CHECK(content.width() == 5 && content.height() == 3);
    REQUIRE(trimCanvas(doc, &error));
    CHECK(size(doc).x == 5 && size(doc).y == 3);
    CHECK(same(at(doc, 0, 0), kRed) && same(at(doc, 4, 2), kBlue));

    REQUIRE(cropCanvas(doc, { { 1, 0 }, { 3, 1 } }, &error));
    CHECK(size(doc).x == 2 && size(doc).y == 1);
    CHECK(same(at(doc, 0, 0), kRed));
    CHECK(at(doc, 1, 0).a == 0);
}

void testEnlargeIsExactAndReduceUndoesIt() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    const std::vector<uint8_t> before = picture(doc, doc.sprite()).pixels;
    std::string error;
    REQUIRE(enlargeSprite(doc, 3, &error));
    CHECK(size(doc).x == 15 && size(doc).y == 9);
    CHECK(same(at(doc, 5, 2), kRed));             // (1,0) is now the block 3..5, 0..2
    CHECK(same(at(doc, 14, 8), kBlue));
    CHECK(at(doc, 6, 0).a == 0);
    REQUIRE(reduceSprite(doc, 3, &error));
    CHECK(picture(doc, doc.sprite()).pixels == before);
}

void testShapesStayShapes() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    std::string error;
    REQUIRE(resizeCanvas(doc, 20, 10, CanvasAnchor::TopLeft, &error));
    ShapeParams params;
    params.from = { 2, 1 };
    params.to = { 10, 5 };
    params.cornerRadius = 2.f;
    ShapeLayer rect;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, params, kBlue,
                       ls::kColorRoleNone, &rect));

    REQUIRE(rotateCanvas(doc, 1, &error));        // 20 x 10 becomes 10 x 20
    ShapeParams turned;
    REQUIRE(readShapeParams(doc, rect, &turned));
    // Clockwise, corners go to (height - y, x): x 2..10, y 1..5 -> x 5..9, y 2..10.
    CHECK(turned.from.x == 5.f && turned.to.x == 9.f);
    CHECK(turned.from.y == 2.f && turned.to.y == 10.f);
    CHECK(turned.cornerRadius == 2.f);

    REQUIRE(enlargeSprite(doc, 2, &error));
    ShapeParams bigger;
    REQUIRE(readShapeParams(doc, rect, &bigger));
    CHECK(bigger.from.x == 10.f && bigger.to.y == 20.f && bigger.cornerRadius == 4.f);
}

void testEveryFrameAndOneUndo() {
    Document doc;
    PaintLayer layer;
    REQUIRE(build(doc, &layer));
    REQUIRE(duplicateFrame(doc, 0) == 1);
    doc.clearHistory();
    std::string error;
    REQUIRE(flipCanvas(doc, true, &error));
    const std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 2);
    CHECK(same(ls::readPixel(picture(doc, frames[1].sprite), 4, 0), kRed));
    CHECK(doc.undo());
    CHECK(!doc.canUndo());
    CHECK(same(ls::readPixel(picture(doc, frames[1].sprite), 0, 0), kRed));
}

} // namespace

int main() {
    testQuarterTurns();
    testFlips();
    testResizeKeepsWhatFallsOff();
    testCropAndTrim();
    testEnlargeIsExactAndReduceUndoesIt();
    testShapesStayShapes();
    testEveryFrameAndOneUndo();
    if (failures == 0) {
        std::printf("canvas: all passed\n");
        return 0;
    }
    std::printf("canvas: %d failure(s)\n", failures);
    return 1;
}
