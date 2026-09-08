// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// robustness_tests.cpp — the awkward paths.
//
// The happy path is covered elsewhere. These are the things a real session does
// that a demo never does: opening a second file into the same window, drawing
// outside the canvas, erasing what was never there, undoing across a save.

#include "app/paint.h"

#include <cstdio>
#include <fstream>
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

int opaqueOf(LSContext& engine, SpriteId sprite, uint32_t size) {
    auto compiled = engine.compileSprite(sprite, profile(size));
    if (compiled.fail()) { return -1; }
    int count = 0;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            count += readPixel(compiled.value.raster, static_cast<int32_t>(x),
                               static_cast<int32_t>(y)).a != 0;
        }
    }
    return count;
}

struct Canvas {
    fast::Document   doc;
    SpriteId         sprite;
    fast::PaintLayer paint;

    bool build(uint32_t size = 16) {
        if (!doc.create("robust", size, size)) { return false; }
        sprite = doc.engine().createSprite(doc.id()).value;
        return fast::createPaintLayer(doc, sprite, "layer 1",
                                      Color{200, 60, 60, 255}, &paint);
    }
    int opaque(uint32_t size = 16) { return opaqueOf(doc.engine(), sprite, size); }
};

// ---------------------------------------------------------------------------

// Opening a file into a window that already has a document must not leave the
// previous one behind. An editor that opens ten files should be holding one
// document, not ten.
void testOpeningDoesNotLeakThePreviousDocument() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, {{1, 1}});

    const std::string path = "robust_leak.lsprite";
    std::string error;
    REQUIRE(canvas.doc.save(path, &error));

    const size_t before = canvas.doc.engine().documents().size();
    CHECK(before == 1);

    REQUIRE(canvas.doc.open(path, &error));
    const size_t afterOne = canvas.doc.engine().documents().size();

    REQUIRE(canvas.doc.open(path, &error));
    REQUIRE(canvas.doc.open(path, &error));
    const size_t afterThree = canvas.doc.engine().documents().size();

    std::printf("  documents held: start %zu, after 1 open %zu, after 3 %zu\n",
                before, afterOne, afterThree);
    CHECK(afterThree == 1);
}

// Drawing outside the canvas must not corrupt anything. A drag that leaves the
// window reports coordinates outside it, and negative ones at that.
void testPaintingOutsideTheCanvasIsHarmless() {
    Canvas canvas;
    REQUIRE(canvas.build());

    CHECK(fast::paintPixels(canvas.doc, canvas.paint, {{-5, -5}, {100, 100}, {8, 8}}));
    // Only the pixel that is actually on the canvas shows up.
    CHECK(canvas.opaque() == 1);

    // And the document still saves and reloads with out-of-bounds pixels in it.
    std::string error;
    CHECK(canvas.doc.save("robust_outside.lsprite", &error));

    fast::Document reopened;
    REQUIRE(reopened.open("robust_outside.lsprite", &error));
    auto info = reopened.engine().getDocumentInfo(reopened.id());
    REQUIRE(info.ok() && !info.value.sprites.empty());
    CHECK(opaqueOf(reopened.engine(), info.value.sprites.front(), 16) == 1);
}

void testErasingWhatWasNeverThere() {
    Canvas canvas;
    REQUIRE(canvas.build());
    CHECK(fast::erasePixels(canvas.doc, canvas.paint, {{3, 3}}));
    CHECK(canvas.opaque() == 0);

    fast::paintPixels(canvas.doc, canvas.paint, {{3, 3}});
    CHECK(canvas.opaque() == 1);
    CHECK(fast::erasePixels(canvas.doc, canvas.paint, {{3, 3}}));
    CHECK(canvas.opaque() == 0);
    // Erasing twice is not an error either.
    CHECK(fast::erasePixels(canvas.doc, canvas.paint, {{3, 3}}));
    CHECK(canvas.opaque() == 0);
}

// Saving does not end the session: undo has to keep working across it.
void testUndoStillWorksAfterSaving() {
    Canvas canvas;
    REQUIRE(canvas.build());

    canvas.doc.beginAction("Stroke");
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({0, 0}, {5, 5}));
    canvas.doc.endAction();
    CHECK(canvas.opaque() == 6);

    std::string error;
    REQUIRE(canvas.doc.save("robust_undo_after_save.lsprite", &error));
    CHECK(!canvas.doc.modified());

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == 0);
    CHECK(canvas.doc.modified());     // undoing after a save is an unsaved change
}

// Several layers, each with its own colour, composited in order.
void testLayersStackAndRecolourIndependently() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::PaintLayer second;
    REQUIRE(fast::createPaintLayer(canvas.doc, canvas.sprite, "layer 2",
                                   Color{40, 80, 220, 255}, &second));

    fast::paintPixels(canvas.doc, canvas.paint, {{4, 4}, {5, 4}});
    fast::paintPixels(canvas.doc, second, {{5, 4}, {6, 4}});
    CHECK(canvas.opaque() == 3);

    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile(16));
    REQUIRE(compiled.ok());
    // The later layer wins where they overlap.
    const Color overlap = readPixel(compiled.value.raster, 5, 4);
    CHECK(overlap.b > overlap.r);
    const Color lower = readPixel(compiled.value.raster, 4, 4);
    CHECK(lower.r > lower.b);

    // Recolouring one leaves the other alone.
    fast::setPaintColor(canvas.doc, second, Color{20, 200, 60, 255});
    compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile(16));
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, 6, 4).g > 150);
    CHECK(readPixel(compiled.value.raster, 4, 4).r > 150);
}

// Hiding a layer removes it from the picture and undoing brings it back. This
// caught a stale-cache bug in the engine once already.
void testHidingALayerUpdatesTheCompile() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, {{2, 2}, {3, 3}});
    CHECK(canvas.opaque() == 2);

    canvas.doc.beginAction("Hide");
    canvas.doc.engine().setLayerVisibility(canvas.paint.layer, false);
    canvas.doc.endAction();
    CHECK(canvas.opaque() == 0);

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == 2);
}

// The whole point of the substrate: a drawing that has been through many
// changes is exactly as clean as one that has not.
void testManyEditsDoNotDegrade() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({2, 8}, {13, 8}));
    const int drawn = canvas.opaque();
    CHECK(drawn == 12);

    for (int i = 0; i < 25; ++i) {
        fast::setPaintColor(canvas.doc, canvas.paint,
                            Color{static_cast<uint8_t>(i * 9), 100, 200, 255});
        CHECK(canvas.opaque() == drawn);
    }

    canvas.doc.beginAction("Batch");
    for (int i = 0; i < 25; ++i) {
        fast::paintPixels(canvas.doc, canvas.paint, {{i % 16, 1}});
        fast::erasePixels(canvas.doc, canvas.paint, {{i % 16, 1}});
    }
    canvas.doc.endAction();
    CHECK(canvas.opaque() == drawn);

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == drawn);
}

// A file the editor wrote, opened by a *different* Document, must be drawable on
// again. This is the "draw, save, reopen, keep drawing" loop, and it is the one
// that makes the program feel like an editor rather than a demo.
void testReopenedDocumentCanBeDrawnOnAgain() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({2, 2}, {2, 9}));
    const int drawn = canvas.opaque();

    std::string error;
    const std::string path = "robust_reopen.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    std::vector<fast::PaintLayer> adopted;
    SpriteId sprite;
    const bool found = fast::adoptPaintLayers(reopened, &sprite, &adopted);
    std::printf("  adopted %zu paint layer(s) from the reopened file\n", adopted.size());
    REQUIRE(found);
    REQUIRE(adopted.size() == 1);

    CHECK(opaqueOf(reopened.engine(), sprite, 16) == drawn);

    // And it can be drawn on, recoloured and undone like any other.
    reopened.beginAction("More");
    CHECK(fast::paintPixels(reopened, adopted.front(), {{9, 9}}));
    reopened.endAction();
    CHECK(opaqueOf(reopened.engine(), sprite, 16) == drawn + 1);

    CHECK(fast::setPaintColor(reopened, adopted.front(), Color{10, 220, 90, 255}));
    auto compiled = reopened.engine().compileSprite(sprite, profile(16));
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, 2, 5).g > 150);

    CHECK(reopened.undo());
    CHECK(opaqueOf(reopened.engine(), sprite, 16) == drawn);
}

} // namespace

int main() {
    testOpeningDoesNotLeakThePreviousDocument();
    testPaintingOutsideTheCanvasIsHarmless();
    testErasingWhatWasNeverThere();
    testUndoStillWorksAfterSaving();
    testLayersStackAndRecolourIndependently();
    testHidingALayerUpdatesTheCompile();
    testManyEditsDoNotDegrade();
    testReopenedDocumentCanBeDrawnOnAgain();

    if (failures == 0) {
        std::printf("fast_robustness: all checks passed\n");
        return 0;
    }
    std::printf("fast_robustness: %d check(s) failed\n", failures);
    return 1;
}
