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

#include "app/file_io.h"
#include "app/palette.h"
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
        sprite = doc.sprite();
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

    fast::OutlineSettings outline;
    outline.colour = Color{20, 20, 30, 255};
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, outline));
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

    CHECK(fast::outlineOf(canvas.doc, shape.paint).thickness == 1);
    outline.thickness = 2;
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, outline));
    CHECK(fast::outlineOf(canvas.doc, shape.paint).thickness == 2);
    CHECK(canvas.opaque() > outlined);

    // Setting it again drives the operation that is already there rather than
    // adding a second one, which is what keeps a slider drag to one entry.
    auto operations = canvas.doc.engine().getLayerOperations(shape.paint.layer);
    REQUIRE(operations.ok());
    int outlineOps = 0;
    for (const OperationInfo& op : operations.value) {
        if (op.type == "GenerateSilhouetteOutlineOp") { ++outlineOps; }
    }
    CHECK(outlineOps == 1);

    outline.colour = Color{200, 40, 40, 255};
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, outline));
    CHECK(fast::outlineOf(canvas.doc, shape.paint).colour.r == 200);

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
    fast::OutlineSettings outline;
    outline.colour = Color{20, 20, 30, 255};
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, outline));
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
    fast::OutlineSettings freehandLine;
    freehandLine.colour = Color{20, 20, 30, 255};
    REQUIRE(fast::setOutline(canvas.doc, drawn, freehandLine));
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


// An outline round the whole figure, not round one layer of it.
//
// Two layers that touch: a per-layer outline draws a seam where they meet, and
// a sprite-wide one does not. That difference is the whole feature, so it is
// stated as a difference rather than as two pictures that happen to look right.
void testAnOutlineCanTraceTheWholeSprite() {
    Canvas canvas;
    REQUIRE(canvas.build());

    // The arm first, so the body's layer sits above it. That ordering is what
    // makes the difference visible: a line belonging to the body draws *over*
    // the arm, where a line belonging to the figure has no reason to be at all.
    fast::ShapeLayer arm;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(14, 10, 22, 16),
                                   Color{90, 140, 220, 255}, &arm));
    fast::ShapeLayer body;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(6, 6, 14, 20),
                                   Color{220, 120, 60, 255}, &body));

    // A line round the body alone. The body's silhouette ends at x = 14, so
    // that column is inked even though the figure carries on into the arm --
    // a seam straight through the middle of the character.
    fast::OutlineSettings perLayer;
    perLayer.scope = fast::OutlineScope::Layer;
    perLayer.colour = Color{20, 20, 30, 255};
    REQUIRE(fast::setOutline(canvas.doc, body.paint, perLayer));

    CHECK(canvas.at(5, 12).r == 20);        // outside the body on the left
    CHECK(canvas.at(14, 12).r == 20);       // the seam, over the arm
    CHECK(canvas.at(22, 12).a == 0);        // and nothing round the arm

    // The same outline, told to trace the figure.
    fast::OutlineSettings figure = perLayer;
    figure.scope = fast::OutlineScope::Sprite;
    REQUIRE(fast::setOutline(canvas.doc, body.paint, figure));

    CHECK(fast::outlineOf(canvas.doc, body.paint).scope == fast::OutlineScope::Sprite);
    CHECK(canvas.at(5, 12).r == 20);        // still round the outside
    CHECK(canvas.at(14, 12).b == 220);      // the seam is gone: that is arm now
    CHECK(canvas.at(22, 12).r == 20);       // and the line went round the arm

    // And it follows every part rather than the one it sits on: move the arm
    // and the figure's line moves with it.
    REQUIRE(fast::updateShape(canvas.doc, arm, box(14, 10, 26, 16)));
    CHECK(canvas.at(22, 12).b == 220);      // artwork where the line used to be
    CHECK(canvas.at(26, 12).r == 20);       // and the line further out
}

// The colour can come from a palette slot, so an outline joins a palette swap
// instead of being the one thing left behind by it.
void testAnOutlineCanFollowAPaletteSlot() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(8, 8, 18, 18),
                                   Color{220, 120, 60, 255}, &shape));
    REQUIRE(fast::ensurePalette(canvas.doc, canvas.sprite));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 4, Color{200, 30, 40, 255}));

    fast::OutlineSettings settings;
    settings.role = 4;
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, settings));
    CHECK(canvas.at(7, 12).r == 200);

    REQUIRE(fast::setPaletteEntry(canvas.doc, 4, Color{30, 200, 120, 255}));
    CHECK(canvas.at(7, 12).g == 200);

    // What the panel is told is what the canvas shows: the slot's colour, not
    // the literal the operation fell back on before the slot was set. Detach
    // from here and nothing visibly changes.
    const fast::OutlineSettings shown = fast::outlineOf(canvas.doc, shape.paint);
    CHECK(shown.role == 4);
    CHECK(shown.colour.g == 200 && shown.colour.r == 30);
    fast::OutlineSettings detached = shown;
    detached.role = kColorRoleNone;
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, detached));
    CHECK(canvas.at(7, 12).g == 200);
    CHECK(fast::outlineOf(canvas.doc, shape.paint).role == kColorRoleNone);
}

// A thickness a person cannot type is one the interface cannot produce, but a
// file can. It is clamped rather than trusted.
void testOutlineThicknessIsBounded() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::ShapeLayer shape;
    REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                   fast::ShapeKind::Rectangle, box(10, 10, 16, 16),
                                   Color{220, 120, 60, 255}, &shape));

    fast::OutlineSettings settings;
    settings.thickness = 9999;
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, settings));
    CHECK(fast::outlineOf(canvas.doc, shape.paint).thickness == fast::kMaxOutlineThickness);

    settings.thickness = 0;
    REQUIRE(fast::setOutline(canvas.doc, shape.paint, settings));
    CHECK(fast::outlineOf(canvas.doc, shape.paint).thickness == 1);
}


// The one that would silently rot. targetSprite is an id, and every id is
// minted fresh by the reader -- so unless serialization remaps it, a reloaded
// file has an outline pointing at a sprite that no longer exists, and it
// quietly goes back to tracing one layer.
void testTheOutlineScopeSurvivesAReload() {
    const std::string path = "outline_scope_test.lsprite";
    std::vector<uint8_t> before;
    {
        Canvas canvas;
        REQUIRE(canvas.build());
        fast::ShapeLayer arm;
        REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                       fast::ShapeKind::Rectangle, box(14, 10, 22, 16),
                                       Color{90, 140, 220, 255}, &arm));
        fast::ShapeLayer body;
        REQUIRE(fast::createShapeLayer(canvas.doc, canvas.sprite,
                                       fast::ShapeKind::Rectangle, box(6, 6, 14, 20),
                                       Color{220, 120, 60, 255}, &body));

        fast::OutlineSettings figure;
        figure.scope = fast::OutlineScope::Sprite;
        figure.thickness = 2;
        figure.colour = Color{20, 20, 30, 255};
        REQUIRE(fast::setOutline(canvas.doc, body.paint, figure));
        before = canvas.pixels();

        std::string error;
        REQUIRE(canvas.doc.save(path, &error));
    }

    fast::Document reopened;
    std::string error;
    REQUIRE(reopened.open(path, &error));

    SpriteId sprite;
    std::vector<fast::PaintLayer> layers;
    REQUIRE(fast::adoptPaintLayers(reopened, &sprite, &layers));
    REQUIRE(layers.size() == 2);

    // The layer holding it is the body, which was created second.
    const fast::PaintLayer& body = layers.back();
    const fast::OutlineSettings restored = fast::outlineOf(reopened, body);
    CHECK(restored.scope == fast::OutlineScope::Sprite);
    CHECK(restored.thickness == 2);

    // And it still draws the same picture, which is the check that would catch
    // a remap that pointed at some other sprite rather than at none.
    auto compiled = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(compiled.value.raster.pixels == before);

    fast::deleteFile(path);
}

} // namespace

int main() {
    testARectangleStaysARectangle();
    testEllipsesAndLines();
    testDrivingAShapeAddsNothing();
    testAnOutlineFollowsTheShape();
    testAnOutlineCanTraceTheWholeSprite();
    testAnOutlineCanFollowAPaletteSlot();
    testOutlineThicknessIsBounded();
    testTheOutlineScopeSurvivesAReload();
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
