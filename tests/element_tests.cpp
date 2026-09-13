// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// element_tests.cpp — several marks on one layer.
//
// A rectangle, a line and some pixels drawn with three tools land on one
// layer, each still its own kind of thing: the rectangle stays a rectangle,
// the pixels stay pixels, and the pencil never turns the rectangle into
// pixels by drawing into its region. One colour for the layer reaches all
// of them. Removing one leaves the others; the last cannot go.

#include "app/document.h"
#include "app/element.h"
#include "app/file_io.h"
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

ShapeParams box(float x0, float y0, float x1, float y1) {
    ShapeParams p;
    p.from = { x0, y0 };
    p.to = { x1, y1 };
    return p;
}

void testThreeToolsOneLayer() {
    Document doc;
    REQUIRE(doc.create("elements", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "belt", ls::Color{ 200, 40, 40, 255 }, &layer));
    doc.beginAction("pixels");
    REQUIRE(paintPixels(doc, layer, {{ 1, 1 }, { 2, 1 }}));
    doc.endAction();

    ShapeLayer rect, line;
    REQUIRE(addShapeElement(doc, layer.layer, ShapeKind::Rectangle, box(6, 6, 10, 10), &rect));
    REQUIRE(addShapeElement(doc, layer.layer, ShapeKind::Line, box(0, 14, 15, 14), &line));

    const std::vector<Element> elements = elementsOf(doc, layer.layer);
    REQUIRE(elements.size() == 3);
    CHECK(elements[0].kind == ElementKind::Paint);
    CHECK(elements[1].kind == ElementKind::Rectangle);
    CHECK(elements[2].kind == ElementKind::Line);
    CHECK(elements[1].geometry == rect.geometry);
    CHECK(elements[2].geometry == line.geometry && !elements[2].region.valid());

    // All three draw, in the layer's colour, on one layer.
    CHECK(at(doc, 1, 1).r == 200);
    CHECK(at(doc, 8, 8).r == 200);
    CHECK(at(doc, 7, 14).r == 200);
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    CHECK(info.ok() && info.value.layers.size() == 1);

    // The rectangle is still a rectangle: widen it, and it widens.
    ShapeParams params;
    REQUIRE(readShapeParams(doc, shapeOfElement(layer.layer, elements[1]), &params));
    CHECK(at(doc, 12, 8).a == 0);
    params.to = { 14.f, 10.f };
    REQUIRE(updateShape(doc, shapeOfElement(layer.layer, elements[1]), params));
    CHECK(at(doc, 12, 8).r == 200);

    // One colour for the layer reaches every element.
    doc.beginAction("recolour");
    REQUIRE(setPaintColor(doc, layer, ls::Color{ 40, 200, 40, 255 }));
    doc.endAction();
    CHECK(at(doc, 1, 1).g == 200 && at(doc, 8, 8).g == 200 && at(doc, 7, 14).g == 200);
    REQUIRE(ensurePalette(doc, doc.sprite()));
    REQUIRE(setPaletteEntry(doc, 3, ls::Color{ 40, 40, 200, 255 }));
    doc.beginAction("role");
    REQUIRE(setLayerRole(doc, layer, 3));
    doc.endAction();
    CHECK(at(doc, 1, 1).b == 200 && at(doc, 8, 8).b == 200 && at(doc, 7, 14).b == 200);

    // Remove the line; the rest stay. The last element cannot go.
    REQUIRE(removeElement(doc, layer.layer, elements[2]));
    CHECK(elementsOf(doc, layer.layer).size() == 2);
    CHECK(at(doc, 7, 14).a == 0);
    CHECK(at(doc, 8, 8).b == 200);
    CHECK(doc.undo());
    CHECK(elementsOf(doc, layer.layer).size() == 3);
    CHECK(at(doc, 7, 14).b == 200);
    REQUIRE(removeElement(doc, layer.layer, elementsOf(doc, layer.layer)[2]));
    REQUIRE(removeElement(doc, layer.layer, elementsOf(doc, layer.layer)[1]));
    CHECK(!removeElement(doc, layer.layer, elementsOf(doc, layer.layer)[0]));
}

// A layer that started as a shape gets a freehand element when the pencil
// first touches it, rather than the rectangle being turned into pixels.
void testThePencilOnAShapeLayerDoesNotEatTheShape() {
    Document doc;
    REQUIRE(doc.create("shape-first", kSize, kSize));
    ShapeLayer rect;
    REQUIRE(createShapeLayer(doc, doc.sprite(), ShapeKind::Rectangle, box(4, 4, 8, 8),
                             ls::Color{ 200, 40, 40, 255 }, &rect));

    // Adopted as the shape: the pencil must not draw into that region.
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(doc, doc.sprite(), &layers));
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].region == rect.paint.region);
    CHECK(layers[0].fill == rect.paint.fill);

    REQUIRE(ensurePaintElement(doc, layers[0]));
    CHECK(layers[0].drawable());
    CHECK(layers[0].region != rect.paint.region);
    doc.beginAction("pixels");
    REQUIRE(paintPixels(doc, layers[0], {{ 12, 12 }}));
    doc.endAction();
    CHECK(at(doc, 12, 12).r == 200);                    // in the layer's colour
    CHECK(at(doc, 6, 6).r == 200);

    const std::vector<Element> elements = elementsOf(doc, rect.paint.layer);
    REQUIRE(elements.size() == 2);
    CHECK(elements[0].kind == ElementKind::Rectangle);  // still a rectangle
    CHECK(elements[1].kind == ElementKind::Paint);

    // A second adoption finds the freehand element and prefers it.
    REQUIRE(adoptPaintLayers(doc, doc.sprite(), &layers));
    CHECK(layers[0].drawable() && layers[0].region == elements[1].region);
    // And ensurePaintElement makes no second one.
    const size_t before = elementsOf(doc, rect.paint.layer).size();
    REQUIRE(ensurePaintElement(doc, layers[0]));
    CHECK(elementsOf(doc, rect.paint.layer).size() == before);
}

void testElementsSurviveASave() {
    Document doc;
    REQUIRE(doc.create("saved", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "belt", ls::Color{ 200, 40, 40, 255 }, &layer));
    doc.beginAction("pixels");
    REQUIRE(paintPixels(doc, layer, {{ 1, 1 }}));
    doc.endAction();
    ShapeLayer rect, ell;
    REQUIRE(addShapeElement(doc, layer.layer, ShapeKind::Rectangle, box(6, 6, 10, 10), &rect));
    REQUIRE(addShapeElement(doc, layer.layer, ShapeKind::Ellipse, box(2, 8, 8, 14), &ell));

    std::string error;
    const std::string path = "fast_element_test.lsprite";
    REQUIRE(doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(again, again.sprite(), &layers));
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].drawable());
    const std::vector<Element> elements = elementsOf(again, layers[0].layer);
    REQUIRE(elements.size() == 3);
    CHECK(elements[0].kind == ElementKind::Paint);
    CHECK(elements[1].kind == ElementKind::Rectangle);
    CHECK(elements[2].kind == ElementKind::Ellipse);
}

} // namespace

int main() {
    testThreeToolsOneLayer();
    testThePencilOnAShapeLayerDoesNotEatTheShape();
    testElementsSurviveASave();

    if (failures == 0) {
        std::printf("fast_element: all checks passed\n");
        return 0;
    }
    std::printf("fast_element: %d check(s) failed\n", failures);
    return 1;
}
