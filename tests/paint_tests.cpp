// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// paint_tests.cpp — the pencil, without a window.
//
// The point of these is not that drawing puts pixels on screen. It is that
// drawing does so *without* pixels being the truth: a stroke accumulates into
// one region, a recolour touches no drawing, and an undo of a whole drag is one
// step rather than one per sample.

#include "app/paint.h"

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

CompileProfile profile(uint32_t size) {
    CompileProfile p;
    p.type = CompileProfileType::Export;
    p.outputWidth = size;
    p.outputHeight = size;
    p.palette = PalettePolicy::Unconstrained;
    return p;
}

struct Canvas {
    fast::Document  doc;
    SpriteId        sprite;
    fast::PaintLayer paint;

    bool build(uint32_t size = 16, Color color = Color{200, 60, 60, 255}) {
        if (!doc.create("paint", size, size)) { return false; }
        sprite = doc.engine().createSprite(doc.id()).value;
        return fast::createPaintLayer(doc, sprite, "layer 1", color, &paint);
    }

    RasterBuffer compile(uint32_t size = 16) {
        auto result = doc.engine().compileSprite(sprite, profile(size));
        return result.ok() ? result.value.raster : RasterBuffer{};
    }

    int opaque(uint32_t size = 16) {
        const RasterBuffer raster = compile(size);
        int count = 0;
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                count += readPixel(raster, static_cast<int32_t>(x),
                                   static_cast<int32_t>(y)).a != 0;
            }
        }
        return count;
    }

    Color at(int32_t x, int32_t y) { return readPixel(compile(), x, y); }
};

// ---------------------------------------------------------------------------

void testNewPaintLayerIsEmpty() {
    Canvas canvas;
    REQUIRE(canvas.build());
    CHECK(canvas.paint.valid());
    CHECK(canvas.opaque() == 0);

    // Creating the layer is itself undoable.
    CHECK(canvas.doc.canUndo());
    CHECK(canvas.doc.undoLabel() == "Add layer");
}

void testPaintingMarksPixels() {
    Canvas canvas;
    REQUIRE(canvas.build());

    CHECK(fast::paintPixels(canvas.doc, canvas.paint, {{4, 4}, {5, 4}, {6, 4}}));
    CHECK(canvas.opaque() == 3);

    const Color painted = canvas.at(5, 4);
    CHECK(painted.r == 200 && painted.g == 60 && painted.b == 60 && painted.a == 255);
    CHECK(canvas.at(0, 0).a == 0);
}

// The stroke accumulates into one region rather than leaving an operation per
// sample. This is what keeps a long drawing session cheap to compile.
void testAStrokeStaysOneOperation() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const size_t before = canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size();

    for (int32_t i = 0; i < 40; ++i) {
        fast::paintPixels(canvas.doc, canvas.paint, {{i % 16, i / 16}});
    }

    const size_t after = canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size();
    CHECK(before == after);
    CHECK(after == 1);
    CHECK(canvas.opaque() == 40);
}

void testErasingRemovesPixels() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::paintPixels(canvas.doc, canvas.paint, {{2, 2}, {3, 2}, {4, 2}});
    CHECK(canvas.opaque() == 3);

    CHECK(fast::erasePixels(canvas.doc, canvas.paint, {{3, 2}}));
    CHECK(canvas.opaque() == 2);
    CHECK(canvas.at(3, 2).a == 0);
    CHECK(canvas.at(2, 2).a != 0);
}

// Recolouring is one parameter change. The drawing is not repainted, and this is
// the difference the whole design exists for.
void testRecolouringDoesNotTouchTheDrawing() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, {{1, 1}, {2, 2}, {3, 3}});
    const int drawn = canvas.opaque();

    CHECK(fast::setPaintColor(canvas.doc, canvas.paint, Color{20, 90, 200, 255}));

    CHECK(canvas.opaque() == drawn);
    const Color after = canvas.at(2, 2);
    CHECK(after.r == 20 && after.g == 90 && after.b == 200);

    const Color reported = fast::paintColor(canvas.doc, canvas.paint);
    CHECK(reported.r == 20 && reported.g == 90 && reported.b == 200);
}

// A drag is many calls and one history entry.
void testAWholeDragUndoesInOneStep() {
    Canvas canvas;
    REQUIRE(canvas.build());

    canvas.doc.beginAction("Pencil");
    for (int32_t i = 0; i < 10; ++i) {
        fast::paintPixels(canvas.doc, canvas.paint, {{i, 5}});
    }
    canvas.doc.endAction();

    CHECK(canvas.opaque() == 10);
    CHECK(canvas.doc.undoLabel() == "Pencil");

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == 0);

    CHECK(canvas.doc.redo());
    CHECK(canvas.opaque() == 10);
}

void testLineFillsTheGapsInADrag() {
    // A drag reports positions per frame; without interpolation a quick stroke
    // is a row of dots.
    const std::vector<Vec2i> horizontal = fast::linePixels({0, 0}, {5, 0});
    CHECK(horizontal.size() == 6);

    const std::vector<Vec2i> diagonal = fast::linePixels({0, 0}, {3, 3});
    CHECK(diagonal.size() == 4);
    CHECK(diagonal.back().x == 3 && diagonal.back().y == 3);

    const std::vector<Vec2i> single = fast::linePixels({7, 7}, {7, 7});
    CHECK(single.size() == 1);

    // Backwards, and steeper than 45 degrees, both still connect end to end.
    const std::vector<Vec2i> steep = fast::linePixels({4, 9}, {2, 0});
    CHECK(steep.size() == 10);
    CHECK(steep.front().x == 4 && steep.front().y == 9);
    CHECK(steep.back().x == 2 && steep.back().y == 0);

    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({0, 0}, {9, 9}));
    CHECK(canvas.opaque() == 10);
}

// Painting the same pixel twice must not double anything: a region is a set of
// spans, not a list of marks.
void testPaintingIsIdempotent() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::paintPixels(canvas.doc, canvas.paint, {{5, 5}});
    fast::paintPixels(canvas.doc, canvas.paint, {{5, 5}});
    fast::paintPixels(canvas.doc, canvas.paint, {{5, 5}});
    CHECK(canvas.opaque() == 1);
}

// What is saved is the shape and the rule, not the picture. Reopening recompiles
// it.
void testDrawingSurvivesAFileRoundTrip() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({2, 2}, {2, 12}));
    const int drawn = canvas.opaque();
    CHECK(drawn == 11);

    std::string error;
    const std::string path = "fast_paint_test.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    auto info = reopened.engine().getDocumentInfo(reopened.id());
    REQUIRE(info.ok());
    REQUIRE(info.value.sprites.size() == 1);

    auto compiled = reopened.engine().compileSprite(info.value.sprites.front(), profile(16));
    REQUIRE(compiled.ok());
    int count = 0;
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            count += readPixel(compiled.value.raster, static_cast<int32_t>(x),
                               static_cast<int32_t>(y)).a != 0;
        }
    }
    CHECK(count == drawn);
}

} // namespace

int main() {
    testNewPaintLayerIsEmpty();
    testPaintingMarksPixels();
    testAStrokeStaysOneOperation();
    testErasingRemovesPixels();
    testRecolouringDoesNotTouchTheDrawing();
    testAWholeDragUndoesInOneStep();
    testLineFillsTheGapsInADrag();
    testPaintingIsIdempotent();
    testDrawingSurvivesAFileRoundTrip();

    if (failures == 0) {
        std::printf("fast_paint: all checks passed\n");
        return 0;
    }
    std::printf("fast_paint: %d check(s) failed\n", failures);
    return 1;
}
