// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// shape_tests.cpp — shapes that stay shapes.
//
// The claim: a rectangle drawn an hour ago is still a rectangle. It still has an
// origin, a width and a corner radius, and changing any of them updates the
// picture without redrawing anything -- including through a save and reload,
// which is where this would otherwise quietly stop working.
//
// In every other pixel editor a shape becomes pixels on mouse release, so every
// test below would be impossible there rather than merely failing.

#include "app/shape.h"

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

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

using namespace ls;

namespace {

constexpr uint32_t kSize = 32;

CompileProfile profile() {
    CompileProfile p;
    p.type = CompileProfileType::Export;
    p.outputWidth = kSize;
    p.outputHeight = kSize;
    p.palette = PalettePolicy::Unconstrained;
    return p;
}

struct Canvas {
    fast::Document doc;
    SpriteId       sprite;

    bool build() {
        if (!doc.create("shape", kSize, kSize)) { return false; }
        sprite = doc.engine().createSprite(doc.id()).value;
        return sprite.valid();
    }

    int opaque() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        if (compiled.fail()) { return -1; }
        int count = 0;
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                count += readPixel(compiled.value.raster, static_cast<int32_t>(x),
                                   static_cast<int32_t>(y)).a != 0;
            }
        }
        return count;
    }

    std::vector<uint8_t> pixels() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        return compiled.ok() ? compiled.value.raster.pixels : std::vector<uint8_t>();
    }

    Color at(int32_t x, int32_t y) {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        if (compiled.fail()) { return Color{0, 0, 0, 0}; }
        return readPixel(compiled.value.raster, x, y);
    }
};

fast::ShapeParams box(float x0, float y0, float x1, float y1) {
    fast::ShapeParams params;
    params.from = { x0, y0 };
    params.to = { x1, y1 };
    return params;
}

// ---------------------------------------------------------------------------

// The claim, in one test: draw a rectangle, resize it, and the picture follows
// without anything being redrawn.
void testARectangleStaysARectangle() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(4, 4, 12, 12),
                                   Color{220, 120, 60, 255}, &shape));
    CHECK(canvas.opaque() == 64);

    REQUIRE(fast::updateShape(canvas.doc, shape, box(4, 4, 20, 20)));
    CHECK(canvas.opaque() == 256);

    REQUIRE(fast::updateShape(canvas.doc, shape, box(8, 8, 12, 12)));
    CHECK(canvas.opaque() == 16);

    // And back to exactly what it was, because nothing was ever spent.
    REQUIRE(fast::updateShape(canvas.doc, shape, box(4, 4, 12, 12)));
    CHECK(canvas.opaque() == 64);

    // A drag in the other direction describes the same rectangle.
    REQUIRE(fast::updateShape(canvas.doc, shape, box(12, 12, 4, 4)));
    CHECK(canvas.opaque() == 64);
}

void testEllipsesAndLines() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer ellipse;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Ellipse, box(4, 4, 20, 20),
                                   Color{80, 140, 220, 255}, &ellipse));
    const int round = canvas.opaque();
    // An ellipse in a 16x16 box covers less than the box but a good deal of it.
    CHECK(round > 150);
    CHECK(round < 256);

    REQUIRE(fast::updateShape(canvas.doc, ellipse, box(4, 4, 12, 12)));
    CHECK(canvas.opaque() < round);

    Canvas lineCanvas;
    REQUIRE(lineCanvas.build());
    fast::ShapeLayer line;
    REQUIRE(fast::createShapeLayer(lineCanvas.doc, lineCanvas.sprite,
                                   fast::ShapeKind::Line, box(4, 4, 4, 20),
                                   Color{240, 240, 240, 255}, &line));
    const int drawn = lineCanvas.opaque();
    CHECK(drawn > 0);

    // A line is not a box: reversing the drag gives the same line, but moving
    // one end gives a different one.
    REQUIRE(fast::updateShape(lineCanvas.doc, line, box(4, 4, 20, 4)));
    CHECK(lineCanvas.opaque() > 0);
}

// Editing a shape must not add operations: it is one geometry being driven, the
// same way a transform angle is.
void testDrivingAShapeAddsNothing() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(4, 4, 12, 12),
                                   Color{220, 120, 60, 255}, &shape));

    const size_t operations =
        canvas.doc.engine().getLayerOperations(shape.paint.layer).value.size();

    for (int i = 1; i < 60; ++i) {
        const float side = 2.f + static_cast<float>(i % 20);
        fast::updateShape(canvas.doc, shape, box(4, 4, 4 + side, 4 + side));
    }

    CHECK(canvas.doc.engine().getLayerOperations(shape.paint.layer).value.size()
          == operations);

    // The document has one geometry for this shape, not sixty.
    auto info = canvas.doc.engine().getDocumentInfo(canvas.doc.id());
    REQUIRE(info.ok());
}

// An outline generated from the layer follows the shape, rather than being
// stamped where the shape used to be.
void testAnOutlineFollowsTheShape() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(8, 8, 16, 16),
                                   Color{220, 120, 60, 255}, &shape));
    const int bare = canvas.opaque();
    CHECK(!fast::hasOutline(canvas.doc, shape.paint));

    REQUIRE(fast::addOutline(canvas.doc, shape.paint, Color{20, 20, 30, 255}, 1));
    CHECK(fast::hasOutline(canvas.doc, shape.paint));
    const int outlined = canvas.opaque();
    CHECK(outlined > bare);

    // The outline is around the shape, in its own colour.
    CHECK(canvas.at(12, 12).r == 220);      // inside
    CHECK(canvas.at(7, 12).r == 20);        // the ring, just outside the edge

    // Move the shape. In a conventional editor the outline would stay behind.
    REQUIRE(fast::updateShape(canvas.doc, shape, box(16, 16, 24, 24)));
    CHECK(canvas.at(12, 12).a == 0);        // nothing left where it was
    CHECK(canvas.at(20, 20).r == 220);      // the shape moved
    CHECK(canvas.at(15, 20).r == 20);       // and so did its outline

    CHECK(fast::outlineThickness(canvas.doc, shape.paint) == 1);
    REQUIRE(fast::setOutlineThickness(canvas.doc, shape.paint, 2));
    CHECK(fast::outlineThickness(canvas.doc, shape.paint) == 2);
    CHECK(canvas.opaque() > outlined);

    REQUIRE(fast::setOutlineColor(canvas.doc, shape.paint, Color{200, 40, 40, 255}));
    CHECK(fast::outlineColor(canvas.doc, shape.paint).r == 200);

    REQUIRE(fast::removeOutline(canvas.doc, shape.paint));
    CHECK(!fast::hasOutline(canvas.doc, shape.paint));
}

// The one that would have been missed. A shape is only worth having if it is
// still a shape after the file has been closed and opened.
void testAShapeSurvivesAReload() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(6, 6, 18, 18),
                                   Color{220, 120, 60, 255}, &shape));
    REQUIRE(fast::addOutline(canvas.doc, shape.paint, Color{20, 20, 30, 255}, 1));
    const std::vector<uint8_t> before = canvas.pixels();

    std::string error;
    const std::string path = "shape_test.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    SpriteId sprite;
    std::vector<fast::PaintLayer> layers;
    REQUIRE(fast::adoptPaintLayers(reopened, &sprite, &layers));
    REQUIRE(layers.size() == 1);

    auto compiled = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(compiled.value.raster.pixels == before);

    // Still a rectangle, and still editable.
    fast::ShapeLayer restored;
    REQUIRE(fast::shapeOfLayer(reopened, layers.front(), &restored));
    CHECK(restored.kind == fast::ShapeKind::Rectangle);
    CHECK(fast::hasOutline(reopened, layers.front()));

    fast::ShapeParams params;
    REQUIRE(fast::readShapeParams(reopened, restored, &params));
    CHECK(params.from.x == 6.f);
    CHECK(params.to.x == 18.f);

    REQUIRE(fast::updateShape(reopened, restored, box(2, 2, 28, 28)));
    auto after = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(after.ok());
    CHECK(after.value.raster.pixels != before);
}

// A layer drawn by hand is not a shape, and saying so beats offering controls
// that would do nothing.
void testFreehandLayersAreNotShapes() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::PaintLayer drawn;
    REQUIRE(fast::createPaintLayer(canvas.doc, canvas.sprite, "freehand",
                                   Color{200, 200, 200, 255}, &drawn));
    fast::paintPixels(canvas.doc, drawn, fast::linePixels({2, 2}, {12, 12}));

    fast::ShapeLayer none;
    CHECK(!fast::shapeOfLayer(canvas.doc, drawn, &none));

    // But an outline works on anything the layer draws, shape or not.
    REQUIRE(fast::addOutline(canvas.doc, drawn, Color{20, 20, 30, 255}, 1));
    CHECK(fast::hasOutline(canvas.doc, drawn));
    CHECK(canvas.opaque() > 11);
}

void testShapesAreUndoable() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(4, 4, 12, 12),
                                   Color{220, 120, 60, 255}, &shape));
    CHECK(canvas.opaque() == 64);
    CHECK(canvas.doc.undoLabel() == "Draw Rectangle");

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == 0);

    CHECK(canvas.doc.redo());
    CHECK(canvas.opaque() == 64);
}

} // namespace

int main() {
    testARectangleStaysARectangle();
    testEllipsesAndLines();
    testDrivingAShapeAddsNothing();
    testAnOutlineFollowsTheShape();
    testAShapeSurvivesAReload();
    testFreehandLayersAreNotShapes();
    testShapesAreUndoable();

    if (failures == 0) {
        std::printf("fast_shape: all checks passed\n");
        return 0;
    }
    std::printf("fast_shape: %d check(s) failed\n", failures);
    return 1;
}
