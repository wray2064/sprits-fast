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

void testPaintingOverReplaces() {
    Document doc;
    REQUIRE(doc.create("over", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, literal(kRed), {{ 1, 1 }, { 2, 1 }, { 3, 1 }}));
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 2, 1 }}));

    CHECK(same(at(doc, 1, 1), kRed));
    CHECK(same(at(doc, 2, 1), kBlue));
    CHECK(same(at(doc, 3, 1), kRed));

    // And the red is really gone from under it, not merely covered: take the
    // blue away and there is a hole, not the old red showing through.
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
    REQUIRE(paint(doc, layer.layer, literal(kBlue), {{ 6, 5 }}));
    CHECK(same(at(doc, 5, 5), kRed));

    // Change the slot: the pixel painted through it recolours, from the
    // drawing; the literal blue beside it does not move.
    doc.beginAction("Edit slot");
    REQUIRE(setPaletteEntry(doc, skin, kGreen));
    doc.endAction();
    CHECK(same(at(doc, 5, 5), kGreen));
    CHECK(same(at(doc, 6, 5), kBlue));

    // An ink through a slot is that slot whatever colour it remembers, so a
    // stroke picked up after the edit still lands in the same element.
    Ink later;
    later.colour = kGreen;
    later.role = skin;
    REQUIRE(paint(doc, layer.layer, later, {{ 7, 5 }}));
    CHECK(elementsWithInk(doc, layer.layer, ink).size() == 1);
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

} // namespace

int main() {
    testSeveralColoursOneLayer();
    testPaintingOverReplaces();
    testSlotInkFollowsThePalette();
    testPaintLandsOverAShape();
    testEraseTakesEveryColour();
    testOneStrokeOneUndo();
    testThePickerReadsTheSlot();
    testColoursSurviveAFile();
    if (failures == 0) {
        std::printf("ink: all passed\n");
        return 0;
    }
    std::printf("ink: %d failure(s)\n", failures);
    return 1;
}
