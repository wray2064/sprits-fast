// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// selection_tests.cpp — which pixels, and moving them.
//
// The masks: a marquee selects both corners; an ellipse mirrors exactly; a
// lasso takes its outline as well as its inside; add, subtract and intersect
// do what the modifiers promise; a flip and a quarter turn map the grid onto
// itself, and four turns are none.
//
// The pixels: a lift takes them out of the layer and floats them over it
// without eating what they are dragged across; a drop puts each colour back
// into its own element, replacing what was under it; a copy and paste leaves
// the original; a clear takes pixels out of every colour and leaves shapes;
// a move undoes in one step; a transformed layer refuses.

#include "app/document.h"
#include "app/element.h"
#include "app/floating.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/selection.h"
#include "app/shape.h"
#include "app/transform.h"

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

constexpr uint32_t kSize = 16;

const ls::Color kRed   { 200, 30, 30, 255 };
const ls::Color kBlue  { 30, 30, 200, 255 };

ls::Color at(Document& doc, int x, int y) {
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = kSize;
    profile.outputHeight = kSize;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(doc.sprite(), profile);
    if (compiled.fail()) { return ls::Color{ 0, 0, 0, 0 }; }
    return ls::readPixel(compiled.value.raster, x, y);
}

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool paint(Document& doc, ls::LayerId layer, ls::Color colour,
           const std::vector<ls::Vec2i>& pixels) {
    Ink ink;
    ink.colour = colour;
    doc.beginAction("Pencil");
    InkStroke stroke;
    if (!beginInkStroke(doc, layer, ink, &stroke) || !strokeInk(doc, stroke, pixels)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

int64_t count(const ls::IntervalSet& set) { return ls::geom::pixelCount(set); }

// ------------------------------------------------------------------ masks --

void testMarqueeTakesBothCorners() {
    const ls::IntervalSet box = rectangleMask({ 5, 2 }, { 2, 4 });
    CHECK(count(box) == 4 * 3);
    CHECK(ls::geom::contains(box, { 2, 2 }) && ls::geom::contains(box, { 5, 4 }));
    CHECK(!ls::geom::contains(box, { 6, 4 }));
    CHECK(count(rectangleMask({ 3, 3 }, { 3, 3 })) == 1);
}

void testEllipseIsSymmetric() {
    for (int w = 1; w <= 9; ++w) {
        for (int h = 1; h <= 9; ++h) {
            const ls::IntervalSet oval = ellipseMask({ 0, 0 }, { w - 1, h - 1 });
            const ls::Rect2i box { { 0, 0 }, { w, h } };
            CHECK(count(oval) > 0);
            CHECK(ls::geom::xorSets(oval, flippedHorizontally(oval, box)).empty());
            CHECK(ls::geom::xorSets(oval, flippedVertically(oval, box)).empty());
            // Every row and column of the box has something in it: no gaps.
            const ls::Rect2i bounds = ls::geom::bounds(oval);
            CHECK(bounds.width() == w && bounds.height() == h);
        }
    }
}

void testLassoTakesOutlineAndInside() {
    const ls::IntervalSet triangle = lassoMask({ { 0, 0 }, { 8, 0 }, { 0, 8 } });
    CHECK(ls::geom::contains(triangle, { 1, 1 }));
    CHECK(ls::geom::contains(triangle, { 8, 0 }));      // the outline's corner
    CHECK(!ls::geom::contains(triangle, { 7, 7 }));
    // A lasso along a line is that line.
    const ls::IntervalSet line = lassoMask({ { 0, 5 }, { 6, 5 } });
    CHECK(count(line) == 7);
}

void testModifiersCombine() {
    const ls::IntervalSet a = rectangleMask({ 0, 0 }, { 3, 3 });
    const ls::IntervalSet b = rectangleMask({ 2, 2 }, { 5, 5 });
    CHECK(count(combine(a, b, SelectMode::Replace)) == 16);
    CHECK(count(combine(a, b, SelectMode::Add)) == 16 + 16 - 4);
    CHECK(count(combine(a, b, SelectMode::Subtract)) == 16 - 4);
    CHECK(count(combine(a, b, SelectMode::Intersect)) == 4);
    CHECK(count(clipToCanvas(rectangleMask({ -3, -3 }, { 2, 2 }), 16, 16)) == 9);
}

void testTurnsMapTheGridOntoItself() {
    const ls::IntervalSet l = lassoMask({ { 1, 1 }, { 1, 6 }, { 3, 6 } });
    const ls::Rect2i box = ls::geom::bounds(l);
    ls::IntervalSet turned = l;
    for (int i = 0; i < 4; ++i) {
        turned = rotatedQuarter(turned, ls::geom::bounds(turned), true);
        CHECK(count(turned) == count(l));
    }
    CHECK(ls::geom::xorSets(turned, l).empty());
    CHECK(ls::geom::xorSets(flippedHorizontally(flippedHorizontally(l, box), box), l).empty());
    const ls::IntervalSet once = rotatedQuarter(l, box, true);
    CHECK(ls::geom::xorSets(rotatedQuarter(once, ls::geom::bounds(once), false), l).empty());
}

void testOutlineClosesAroundAMask() {
    // A 2x2 square: four sides of two units each.
    const std::vector<MaskEdge> edges = maskOutline(rectangleMask({ 0, 0 }, { 1, 1 }));
    int length = 0;
    for (const MaskEdge& edge : edges) {
        length += std::abs(edge.to.x - edge.from.x) + std::abs(edge.to.y - edge.from.y);
    }
    CHECK(length == 8);
}

// ----------------------------------------------------------------- pixels --

void testLiftFloatsWithoutEating() {
    Document doc;
    REQUIRE(doc.create("move", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 1, 1 }, { 2, 1 }}));
    REQUIRE(paint(doc, layer.layer, kBlue, {{ 6, 1 }}));
    doc.clearHistory();

    doc.beginAction("Move");
    Floating floating;
    REQUIRE(liftPixels(doc, layer.layer, rectangleMask({ 1, 1 }, { 2, 1 }), &floating));
    CHECK(same(at(doc, 1, 1), kRed));               // still shown where it was

    // Across the blue pixel and beyond: the blue is untouched while it floats.
    REQUIRE(moveFloating(doc, floating, { 5, 0 }));
    CHECK(at(doc, 1, 1).a == 0);
    CHECK(same(at(doc, 6, 1), kRed));
    REQUIRE(moveFloating(doc, floating, { 9, 0 }));
    CHECK(same(at(doc, 6, 1), kBlue));
    CHECK(same(at(doc, 10, 1), kRed));

    // Dropped on the blue, it replaces it -- and red joins red's element.
    REQUIRE(moveFloating(doc, floating, { 5, 0 }));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();
    CHECK(!floating.active());
    CHECK(same(at(doc, 6, 1), kRed));
    CHECK(same(at(doc, 7, 1), kRed));
    CHECK(at(doc, 1, 1).a == 0);
    Ink red;
    red.colour = kRed;
    Ink blue;
    blue.colour = kBlue;
    CHECK(elementsWithInk(doc, layer.layer, red).size() == 1);
    CHECK(elementsWithInk(doc, layer.layer, blue).empty());    // painted out, and gone

    // One step undoes the whole move.
    REQUIRE(doc.undo());
    CHECK(same(at(doc, 1, 1), kRed));
    CHECK(same(at(doc, 6, 1), kBlue));
    CHECK(!doc.canUndo());
}

void testDropClipsToTheCanvas() {
    Document doc;
    REQUIRE(doc.create("edge", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 14, 3 }, { 15, 3 }}));
    Floating floating;
    doc.beginAction("Move");
    REQUIRE(liftPixels(doc, layer.layer, rectangleMask({ 14, 3 }, { 15, 3 }), &floating));
    REQUIRE(moveFloating(doc, floating, { 1, 0 }));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();
    CHECK(same(at(doc, 15, 3), kRed));
    // The pixel that went over the edge is gone, not hiding out there.
    const std::vector<Element> elements = elementsOf(doc, layer.layer);
    int64_t total = 0;
    for (const Element& element : elements) {
        auto set = doc.engine().getRegionIntervals(element.region);
        if (set.ok()) { total += count(set.value); }
    }
    CHECK(total == 1);
}

void testTurnInPlace() {
    Document doc;
    REQUIRE(doc.create("turn", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 2, 2 }, { 3, 2 }, { 4, 2 }}));   // a bar
    Floating floating;
    doc.beginAction("Turn");
    REQUIRE(liftPixels(doc, layer.layer, rectangleMask({ 2, 2 }, { 4, 2 }), &floating));
    REQUIRE(turnFloating(doc, floating, FloatTurn::Clockwise));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();
    // Upright now, through the middle pixel.
    CHECK(same(at(doc, 3, 1), kRed));
    CHECK(same(at(doc, 3, 2), kRed));
    CHECK(same(at(doc, 3, 3), kRed));
    CHECK(at(doc, 2, 2).a == 0);
}

void testCopyPasteLeavesTheOriginal() {
    Document doc;
    REQUIRE(doc.create("copy", kSize, kSize));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole slot = addPaletteEntry(doc, doc.sprite(), kBlue);
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    Ink through;
    through.colour = kBlue;
    through.role = slot;
    doc.beginAction("Pencil");
    InkStroke stroke;
    REQUIRE(beginInkStroke(doc, layer.layer, through, &stroke));
    REQUIRE(strokeInk(doc, stroke, {{ 1, 1 }, { 2, 2 }}));
    doc.endAction();

    PixelClip clip;
    REQUIRE(copyPixels(doc, layer.layer, rectangleMask({ 0, 0 }, { 3, 3 }), &clip));
    CHECK(clip.pieces.size() == 1 && clip.pieces[0].ink.role == slot);

    doc.beginAction("Paste");
    Floating floating;
    REQUIRE(floatClip(doc, layer.layer, clip, &floating));
    REQUIRE(moveFloating(doc, floating, { 8, 8 }));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();
    CHECK(same(at(doc, 1, 1), kBlue));
    CHECK(same(at(doc, 9, 9), kBlue));
    CHECK(same(at(doc, 10, 10), kBlue));

    // The paste went through the slot, so it follows the palette too.
    doc.beginAction("Slot");
    REQUIRE(setPaletteEntry(doc, slot, kRed));
    doc.endAction();
    CHECK(same(at(doc, 9, 9), kRed));
    CHECK(same(at(doc, 1, 1), kRed));
}

void testClearLeavesShapes() {
    Document doc;
    REQUIRE(doc.create("clear", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 1, 1 }, { 2, 1 }}));
    REQUIRE(paint(doc, layer.layer, kBlue, {{ 3, 1 }}));
    ShapeParams params;
    params.from = { 8, 8 };
    params.to = { 12, 12 };
    ShapeLayer rect;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, params, kBlue,
                       ls::kColorRoleNone, &rect));

    doc.beginAction("Delete");
    REQUIRE(clearPixels(doc, layer.layer, rectangleMask({ 0, 0 }, { 15, 15 })));
    doc.endAction();
    CHECK(at(doc, 1, 1).a == 0);
    CHECK(at(doc, 3, 1).a == 0);
    CHECK(same(at(doc, 10, 10), kBlue));        // the rectangle is still a rectangle
}

void testAShapeInsideGoesAlongAsAShape() {
    Document doc;
    REQUIRE(doc.create("shape-move", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 1, 1 }}));
    ShapeParams params;
    params.from = { 2, 2 };
    params.to = { 5, 5 };
    ShapeLayer inside, outside;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, params, kBlue,
                       ls::kColorRoleNone, &inside));
    ShapeParams far;
    far.from = { 10, 10 };
    far.to = { 14, 14 };
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, far, kBlue,
                       ls::kColorRoleNone, &outside));

    doc.beginAction("Move");
    Floating floating;
    REQUIRE(liftPixels(doc, layer.layer, rectangleMask({ 0, 0 }, { 7, 7 }), &floating));
    CHECK(floating.shapes.size() == 1);
    REQUIRE(moveFloating(doc, floating, { 3, 0 }));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();

    ShapeParams moved;
    REQUIRE(readShapeParams(doc, inside, &moved));
    CHECK(moved.from.x == 5.f && moved.to.x == 8.f);     // still a rectangle, moved
    ShapeParams stayed;
    REQUIRE(readShapeParams(doc, outside, &stayed));
    CHECK(stayed.from.x == 10.f);                         // outside the mask, untouched
    CHECK(same(at(doc, 4, 1), kRed));
    CHECK(at(doc, 1, 1).a == 0);
}

void testTransformedLayersRefuse() {
    Document doc;
    REQUIRE(doc.create("turned", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 4, 4 }}));
    doc.beginAction("Rotate");
    addRotate(doc, layer.layer, 30.f, { 8.f, 8.f });
    doc.endAction();
    Floating floating;
    CHECK(!layerTakesSelections(doc, layer.layer));
    CHECK(!liftPixels(doc, layer.layer, rectangleMask({ 0, 0 }, { 15, 15 }), &floating));
    CHECK(!floating.active());
}

} // namespace

int main() {
    testMarqueeTakesBothCorners();
    testEllipseIsSymmetric();
    testLassoTakesOutlineAndInside();
    testModifiersCombine();
    testTurnsMapTheGridOntoItself();
    testOutlineClosesAroundAMask();
    testLiftFloatsWithoutEating();
    testDropClipsToTheCanvas();
    testTurnInPlace();
    testCopyPasteLeavesTheOriginal();
    testClearLeavesShapes();
    testAShapeInsideGoesAlongAsAShape();
    testTransformedLayersRefuse();
    if (failures == 0) {
        std::printf("selection: all passed\n");
        return 0;
    }
    std::printf("selection: %d failure(s)\n", failures);
    return 1;
}
