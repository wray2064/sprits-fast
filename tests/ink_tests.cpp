// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// ink_tests.cpp — many colours on one layer.
//
// The promises: a pencil can lay any number of colours on one layer; a pixel
// painted twice has the second colour, not both; a colour painted through a
// slot follows the slot; fresh paint lands over a shape already on the layer;
// the eraser takes a pixel out of every colour; an emptied colour leaves the
// element list; and the picker reads back the slot, not just the colour.

#include "app/document.h"
#include "app/element.h"
#include "app/file_io.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"
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

constexpr uint32_t kSize = 16;

const ls::Color kRed   { 200, 30, 30, 255 };
const ls::Color kBlue  { 30, 30, 200, 255 };
const ls::Color kGreen { 30, 200, 30, 255 };

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

Ink literal(ls::Color colour) {
    Ink ink;
    ink.colour = colour;
    return ink;
}

bool paint(Document& doc, ls::LayerId layer, const Ink& ink,
           const std::vector<ls::Vec2i>& pixels) {
    doc.beginAction("Pencil");
    InkStroke stroke;
    if (!beginInkStroke(doc, layer, ink, &stroke) || !strokeInk(doc, stroke, pixels)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

bool erase(Document& doc, ls::LayerId layer, const std::vector<ls::Vec2i>& pixels) {
    doc.beginAction("Eraser");
    InkStroke stroke;
    if (!beginEraseStroke(doc, layer, &stroke) || !strokeInk(doc, stroke, pixels)) {
        doc.abandonAction();
        return false;
    }
    pruneEmptyInks(doc, layer);
    doc.endAction();
    return true;
}

size_t freehandCount(Document& doc, ls::LayerId layer) {
    size_t n = 0;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind == ElementKind::Paint) { ++n; }
    }
    return n;
}

void testSeveralColoursOneLayer() {
    Document doc;
    REQUIRE(doc.create("inks", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));

    REQUIRE(paint(doc, layer.layer, literal(kRed),   {{ 1, 1 }, { 2, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kBlue),  {{ 3, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kGreen), {{ 4, 1 }}));

    CHECK(same(at(doc, 1, 1), kRed));
    CHECK(same(at(doc, 3, 1), kBlue));
    CHECK(same(at(doc, 4, 1), kGreen));

    // One layer, three colours -- and red reused the layer's own element
    // rather than starting a second one.
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    CHECK(info.ok() && info.value.layers.size() == 1);
    CHECK(freehandCount(doc, layer.layer) == 3);
    CHECK(elementsWithInk(doc, layer.layer, literal(kRed)).size() == 1);
}

void testPaintingOverCovers() {
    Document doc;
    REQUIRE(doc.create("over", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 1, 1 }, { 2, 1 }, { 3, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 2, 1 }}));

    CHECK(same(at(doc, 1, 1), kRed));
    CHECK(same(at(doc, 2, 1), kBlue));
    CHECK(same(at(doc, 3, 1), kRed));

    // The blue covers the red rather than cutting it away: remove the blue
    // and the red is there again.
    Element blue;
    for (const Element& element : elementsOf(doc, layer.layer)) {
        Ink ink;
        if (inkOfElement(doc, element.fill, &ink) && ink == literal(kBlue)) {
            blue = element;
        }
    }
    REQUIRE(removeElement(doc, layer.layer, blue));
    CHECK(same(at(doc, 2, 1), kRed));
    REQUIRE(doc.undo());
    CHECK(same(at(doc, 2, 1), kBlue));

    // The eraser takes everything under it: blue and the red beneath, a hole.
    REQUIRE(erase(doc, layer.layer, {{ 2, 1 }}));
    CHECK(at(doc, 2, 1).a == 0);
    CHECK(same(at(doc, 1, 1), kRed));

    // Blue now draws nothing, so its element went with it; red stays.
    CHECK(elementsWithInk(doc, layer.layer, literal(kBlue)).empty());
    CHECK(elementsWithInk(doc, layer.layer, literal(kRed)).size() == 1);

    // Painting red back over red is still one red element.
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 1, 1 }}));
    CHECK(elementsWithInk(doc, layer.layer, literal(kRed)).size() == 1);
}

void testSlotInkFollowsThePalette() {
    Document doc;
    REQUIRE(doc.create("slots", kSize, kSize));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole skin = addPaletteEntry(doc, doc.sprite(), kRed);
    REQUIRE(skin != ls::kColorRoleNone);

    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kBlue, &layer));
    Ink ink;
    ink.colour = kRed;
    ink.role = skin;
    REQUIRE(paint(doc, layer.layer, ink, {{ 5, 5 }}));
    // An ink through a slot is that slot whatever colour it remembers, so a
    // stroke with the slot's colour changed still lands in the same run.
    Ink stale;
    stale.colour = kGreen;
    stale.role = skin;
    REQUIRE(paint(doc, layer.layer, stale, {{ 5, 6 }}));
    CHECK(elementsWithInk(doc, layer.layer, ink).size() == 1);
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 6, 5 }}));
    CHECK(same(at(doc, 5, 5), kRed));

    // Change the slot: the pixel painted through it recolours, from the
    // drawing; the literal blue beside it does not move.
    doc.beginAction("Edit slot");
    REQUIRE(setPaletteEntry(doc, skin, kGreen));
    doc.endAction();
    CHECK(same(at(doc, 5, 5), kGreen));
    CHECK(same(at(doc, 6, 5), kBlue));

    // Painted again after the blue, it is a run of its own on top -- and
    // still through the slot.
    Ink later;
    later.colour = kGreen;
    later.role = skin;
    REQUIRE(paint(doc, layer.layer, later, {{ 7, 5 }}));
    CHECK(elementsWithInk(doc, layer.layer, ink).size() == 2);
    CHECK(same(at(doc, 7, 5), kGreen));
}

void testPaintLandsOverAShape() {
    Document doc;
    REQUIRE(doc.create("over-shape", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "belt", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 0, 0 }}));

    // A blue rectangle on the same layer, above the red pixels.
    ShapeParams params;
    params.from = { 4, 4 };
    params.to = { 12, 12 };
    ShapeLayer rect;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, params, kBlue,
                       ls::kColorRoleNone, &rect));
    CHECK(same(at(doc, 8, 8), kBlue));

    // Red across the rectangle has to show. The old red element is under the
    // rectangle, so the stroke gets a new one on top rather than vanishing.
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 8, 8 }}));
    CHECK(same(at(doc, 8, 8), kRed));
    CHECK(same(at(doc, 9, 8), kBlue));
    CHECK(same(at(doc, 0, 0), kRed));
    CHECK(elementsWithInk(doc, layer.layer, literal(kRed)).size() == 2);

    // The rectangle is still a rectangle -- the pencil never drew into it.
    const std::vector<Element> elements = elementsOf(doc, layer.layer);
    bool stillShape = false;
    for (const Element& element : elements) {
        stillShape = stillShape || element.kind == ElementKind::Rectangle;
    }
    CHECK(stillShape);
}

void testEraseTakesEveryColour() {
    Document doc;
    REQUIRE(doc.create("erase", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, literal(kRed),  {{ 1, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 2, 1 }}));
    REQUIRE(erase(doc, layer.layer, {{ 1, 1 }, { 2, 1 }}));
    CHECK(at(doc, 1, 1).a == 0);
    CHECK(at(doc, 2, 1).a == 0);

    // Both colours are empty now; one element stays so the layer is still a
    // layer that can be drawn on.
    CHECK(freehandCount(doc, layer.layer) == 1);
    CHECK(!elementsOf(doc, layer.layer).empty());
}

// The eraser on a shape: the rectangle loses the pixels and stays a
// rectangle, keeping what was erased as its own -- widen it and the erased
// patch is still erased; paint over the patch shows; a shape drawn afterwards
// is not erased; and one stroke is one undo step.
void testEraseThroughAShape() {
    Document doc;
    REQUIRE(doc.create("erase-shape", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "box", kBlue, &layer));
    ShapeParams params;
    params.from = { 2, 2 };
    params.to = { 10, 10 };
    ShapeLayer rect;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, params, kBlue,
                       ls::kColorRoleNone, &rect));
    doc.clearHistory();
    REQUIRE(same(at(doc, 4, 4), kBlue));

    REQUIRE(erase(doc, layer.layer, {{ 4, 4 }, { 5, 4 }, { 14, 14 }}));
    CHECK(at(doc, 4, 4).a == 0 && at(doc, 5, 4).a == 0);
    CHECK(same(at(doc, 6, 4), kBlue));
    // No erase of the layer's: the rectangle keeps it.
    const auto erasedShapes = [&]() {
        size_t n = 0;
        for (const Element& element : elementsOf(doc, layer.layer)) {
            CHECK(element.kind != ElementKind::Erase);
            auto erase = element.region.valid() ? doc.engine().getRegionErase(element.region)
                                                : ls::Result<ls::GeometryId>::ok(ls::GeometryId{});
            n += element.kind == ElementKind::Rectangle && erase.ok() && erase.value.valid() ? 1u : 0u;
        }
        return n;
    };
    CHECK(erasedShapes() == 1);
    auto kept = doc.engine().getRegionIntervals(rect.paint.region);
    CHECK(kept.ok() && ls::geom::pixelCount(kept.value) == 64 - 2);

    // Still a rectangle: made wider, it is wider, with the patch still gone.
    ShapeParams wider = params;
    wider.to = { 13, 10 };
    doc.beginAction("Resize");
    REQUIRE(updateShape(doc, rect, wider));
    doc.endAction();
    CHECK(same(at(doc, 12, 4), kBlue) && at(doc, 4, 4).a == 0);

    // Paint over the patch shows, above the erase.
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 4, 4 }}));
    CHECK(same(at(doc, 4, 4), kRed) && at(doc, 5, 4).a == 0);

    // A shape drawn after the erase is whole, and erasing again reaches it
    // through an erase of its own.
    ShapeParams small;
    small.from = { 5, 3 };
    small.to = { 7, 5 };
    ShapeLayer later;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Rectangle, small, kGreen,
                       ls::kColorRoleNone, &later));
    CHECK(same(at(doc, 5, 4), kGreen));
    REQUIRE(erase(doc, layer.layer, {{ 6, 4 }}));
    CHECK(at(doc, 6, 4).a == 0 && same(at(doc, 5, 4), kGreen));
    CHECK(erasedShapes() == 2);
    REQUIRE(doc.undo());
    CHECK(same(at(doc, 6, 4), kGreen));

    // The picker finds nothing where the rectangle was rubbed out.
    Ink picked;
    CHECK(!inkAt(doc, doc.sprite(), { 3, 3 }, &picked) || !same(picked.colour, kBlue) ||
          at(doc, 3, 3).a != 0);

    // Clearing what was erased from it gives the rectangle back whole.
    doc.beginAction("Unerase");
    REQUIRE(doc.engine().setRegionErase(rect.paint.region, ls::GeometryId{}).ok());
    doc.endAction();
    CHECK(same(at(doc, 5, 4), kGreen));
    CHECK(same(at(doc, 3, 3), kBlue));
}

// Erasing where no shape is keeps no erase at all: a layer of pixels stays
// a layer of pixels.
void testErasingPixelsOnlyMakesNoErase() {
    Document doc;
    REQUIRE(doc.create("pixels", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 1, 1 }, { 2, 1 }}));
    REQUIRE(erase(doc, layer.layer, {{ 1, 1 }}));
    for (const Element& element : elementsOf(doc, layer.layer)) {
        CHECK(element.kind != ElementKind::Erase);
    }
    CHECK(at(doc, 1, 1).a == 0 && same(at(doc, 2, 1), kRed));
}

void testOneStrokeOneUndo() {
    Document doc;
    REQUIRE(doc.create("undo", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    doc.clearHistory();

    // A stroke in a new colour makes an element and paints into it; undo
    // takes both back in one step.
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 3, 3 }}));
    CHECK(freehandCount(doc, layer.layer) == 2);
    REQUIRE(doc.undo());
    CHECK(freehandCount(doc, layer.layer) == 1);
    CHECK(at(doc, 3, 3).a == 0);
    CHECK(!doc.canUndo());
    REQUIRE(doc.redo());
    CHECK(same(at(doc, 3, 3), kBlue));
}

void testThePickerReadsTheSlot() {
    Document doc;
    REQUIRE(doc.create("pick", kSize, kSize));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole skin = addPaletteEntry(doc, doc.sprite(), kGreen);
    PaintLayer bottom, top;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "bottom", kRed, &bottom));
    REQUIRE(createPaintLayer(doc, doc.sprite(), "top", kRed, &top));

    Ink slot;
    slot.colour = kGreen;
    slot.role = skin;
    REQUIRE(paint(doc, bottom.layer, literal(kBlue), {{ 2, 2 }, { 3, 3 }}));
    REQUIRE(paint(doc, top.layer, slot, {{ 3, 3 }}));

    Ink found;
    REQUIRE(inkAt(doc, doc.sprite(), { 3, 3 }, &found));
    CHECK(found.role == skin);                 // the top layer, and its slot
    REQUIRE(inkAt(doc, doc.sprite(), { 2, 2 }, &found));
    CHECK(found.role == ls::kColorRoleNone && same(found.colour, kBlue));
    CHECK(!inkAt(doc, doc.sprite(), { 9, 9 }, &found));

    // A hidden layer is not what the person is looking at.
    doc.engine().setLayerVisibility(top.layer, false);
    REQUIRE(inkAt(doc, doc.sprite(), { 3, 3 }, &found));
    CHECK(same(found.colour, kBlue));
}

void testColoursSurviveAFile() {
    const std::string path = "ink_roundtrip" + std::string(kFileExtension);
    {
        Document doc;
        REQUIRE(doc.create("file", kSize, kSize));
        PaintLayer layer;
        REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
        REQUIRE(paint(doc, layer.layer, literal(kRed),  {{ 1, 1 }}));
        REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 2, 1 }}));
        std::string error;
        REQUIRE(doc.save(path, &error));
    }
    Document doc;
    std::string error;
    REQUIRE(doc.open(path, &error));
    CHECK(same(at(doc, 1, 1), kRed));
    CHECK(same(at(doc, 2, 1), kBlue));

    std::vector<PaintLayer> layers;
    ls::SpriteId sprite;
    REQUIRE(adoptPaintLayers(doc, &sprite, &layers));
    REQUIRE(layers.size() == 1);
    // Painting after the reload finds the same inks rather than new ones.
    REQUIRE(paint(doc, layers[0].layer, literal(kBlue), {{ 1, 1 }}));
    CHECK(same(at(doc, 1, 1), kBlue));
    CHECK(elementsWithInk(doc, layers[0].layer, literal(kBlue)).size() == 1);
    deleteFile(path);
}

void testInkModes() {
    Document doc;
    REQUIRE(doc.create("modes", kSize, kSize));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::PaletteId palette = paletteFor(doc, doc.sprite());
    for (const PaletteEntry& entry : paletteEntries(doc, palette)) {
        removePaletteEntry(doc, palette, entry.role);
    }
    // A three-step ramp, dark to light.
    setPaletteEntry(doc, palette, 0, { 40, 0, 0, 255 });
    setPaletteEntry(doc, palette, 1, { 120, 0, 0, 255 });
    setPaletteEntry(doc, palette, 2, { 220, 0, 0, 255 });
    std::vector<std::pair<ls::ColorRole, ls::Color>> ramp;
    for (const PaletteEntry& entry : paletteEntries(doc, palette)) {
        ramp.push_back({ entry.role, entry.color });
    }
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    Ink mid;
    mid.role = 1;
    mid.colour = { 120, 0, 0, 255 };
    REQUIRE(paint(doc, layer.layer, mid, {{ 1, 1 }, { 2, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 3, 1 }}));

    // Shading: forward steps a slot up the ramp, once per pixel per stroke;
    // a pixel with no slot, or nothing there, is left alone.
    doc.beginAction("Shade");
    InkModeState state;
    REQUIRE(beginInkMode(doc, layer.layer, InkMode::Shading, Ink{}, ramp, &state));
    InkStroke unused;
    REQUIRE(strokeInkMode(doc, state, unused, {{ 1, 1 }, { 3, 1 }, { 5, 5 }}, true));
    REQUIRE(strokeInkMode(doc, state, unused, {{ 1, 1 }}, true));      // again: no further
    doc.endAction();
    CHECK(same(at(doc, 1, 1), ls::Color{ 220, 0, 0, 255 }));
    CHECK(same(at(doc, 2, 1), ls::Color{ 120, 0, 0, 255 }));
    CHECK(same(at(doc, 3, 1), kBlue));
    CHECK(at(doc, 5, 5).a == 0);
    Ink top;
    top.role = 2;
    CHECK(elementsWithInk(doc, layer.layer, top).size() == 1);      // a slot, not a colour

    // Lock alpha: only where the layer already draws.
    doc.beginAction("Lock alpha");
    InkStroke green;
    REQUIRE(beginInkStroke(doc, layer.layer, literal(kGreen), &green));
    REQUIRE(beginInkMode(doc, layer.layer, InkMode::LockAlpha, Ink{}, ramp, &state));
    REQUIRE(strokeInkMode(doc, state, green, {{ 2, 1 }, { 6, 6 }}, true));
    doc.endAction();
    CHECK(same(at(doc, 2, 1), kGreen));
    CHECK(at(doc, 6, 6).a == 0);

    // Replace: only pixels of the second colour.
    doc.beginAction("Replace");
    InkStroke red;
    REQUIRE(beginInkStroke(doc, layer.layer, literal(kRed), &red));
    REQUIRE(beginInkMode(doc, layer.layer, InkMode::Replace, literal(kBlue), ramp, &state));
    REQUIRE(strokeInkMode(doc, state, red, {{ 2, 1 }, { 3, 1 }}, true));
    doc.endAction();
    CHECK(same(at(doc, 3, 1), kRed));
    CHECK(same(at(doc, 2, 1), kGreen));
}

} // namespace

int main() {
    testSeveralColoursOneLayer();
    testPaintingOverCovers();
    testSlotInkFollowsThePalette();
    testPaintLandsOverAShape();
    testEraseTakesEveryColour();
    testOneStrokeOneUndo();
    testEraseThroughAShape();
    testErasingPixelsOnlyMakesNoErase();
    testThePickerReadsTheSlot();
    testColoursSurviveAFile();
    testInkModes();
    if (failures == 0) {
        std::printf("ink: all passed\n");
        return 0;
    }
    std::printf("ink: %d failure(s)\n", failures);
    return 1;
}
