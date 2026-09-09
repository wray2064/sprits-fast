// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// bucket_tests.cpp — filling an area when there is no bitmap to fill.

#include "app/bucket.h"
#include "app/transform.h"

#include <cstdio>

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

constexpr uint32_t kSize = 16;

struct Canvas {
    fast::Document   doc;
    SpriteId         sprite;
    fast::PaintLayer paint;

    bool build() {
        if (!doc.create("bucket", kSize, kSize)) { return false; }
        sprite = doc.sprite();
        return fast::createPaintLayer(doc, sprite, "layer 1",
                                      Color{200, 80, 50, 255}, &paint);
    }

    // A closed box, so a fill inside it must not escape.
    void drawBox() {
        fast::paintPixels(doc, paint, fast::linePixels({4, 4}, {11, 4}));
        fast::paintPixels(doc, paint, fast::linePixels({4, 11}, {11, 11}));
        fast::paintPixels(doc, paint, fast::linePixels({4, 4}, {4, 11}));
        fast::paintPixels(doc, paint, fast::linePixels({11, 4}, {11, 11}));
    }

    int opaque() {
        CompileProfile p;
        p.type = CompileProfileType::Export;
        p.outputWidth = kSize;
        p.outputHeight = kSize;
        p.palette = PalettePolicy::Unconstrained;
        auto compiled = doc.engine().compileSprite(sprite, p);
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
};

// ---------------------------------------------------------------------------

// The whole point of a bucket: a closed outline holds it in.
void testAFillStaysInsideItsOutline() {
    Canvas canvas;
    REQUIRE(canvas.build());
    canvas.drawBox();
    const int outline = canvas.opaque();
    CHECK(outline == 28);           // the box border

    const std::vector<Vec2i> area =
        fast::bucketArea(canvas.doc, canvas.sprite, {7, 7}, {});

    // The interior of an 8x8 box is 6x6.
    CHECK(area.size() == 36);
    for (Vec2i pixel : area) {
        CHECK(pixel.x > 4 && pixel.x < 11);
        CHECK(pixel.y > 4 && pixel.y < 11);
    }

    REQUIRE(fast::bucketFill(canvas.doc, canvas.sprite, canvas.paint, {7, 7}, {}));
    CHECK(canvas.opaque() == outline + 36);
}

// Outside the box, the fill covers everything except the box and its interior.
void testFillingOutsideCoversTheRest() {
    Canvas canvas;
    REQUIRE(canvas.build());
    canvas.drawBox();

    const std::vector<Vec2i> area =
        fast::bucketArea(canvas.doc, canvas.sprite, {0, 0}, {});
    CHECK(area.size() == kSize * kSize - 28 - 36);
}

// A diagonal line is a wall in pixel art. Following diagonals is the classic
// paint-bucket leak, so it is off unless asked for.
void testDiagonalsAreWallsByDefault() {
    Canvas canvas;
    REQUIRE(canvas.build());
    // A diagonal cutting the canvas corner to corner.
    fast::paintPixels(canvas.doc, canvas.paint, fast::linePixels({0, 0}, {15, 15}));

    const std::vector<Vec2i> orthogonal =
        fast::bucketArea(canvas.doc, canvas.sprite, {14, 0}, {});

    fast::BucketSettings leaky;
    leaky.diagonal = true;
    const std::vector<Vec2i> throughDiagonals =
        fast::bucketArea(canvas.doc, canvas.sprite, {14, 0}, leaky);

    // Orthogonal stays on one side; allowing diagonals leaks to the other.
    CHECK(orthogonal.size() < throughDiagonals.size());
    for (Vec2i pixel : orthogonal) {
        CHECK(pixel.x > pixel.y);
    }
}

void testGlobalFillIgnoresConnection() {
    Canvas canvas;
    REQUIRE(canvas.build());
    // Two separate marks of the same colour.
    fast::paintPixels(canvas.doc, canvas.paint, {{1, 1}});
    fast::paintPixels(canvas.doc, canvas.paint, {{14, 14}});

    fast::BucketSettings connected;
    const std::vector<Vec2i> one =
        fast::bucketArea(canvas.doc, canvas.sprite, {1, 1}, connected);
    CHECK(one.size() == 1);

    fast::BucketSettings global;
    global.global = true;
    const std::vector<Vec2i> both =
        fast::bucketArea(canvas.doc, canvas.sprite, {1, 1}, global);
    CHECK(both.size() == 2);
}

// A bucket on a rotated layer has to land where it was aimed, for the same
// reason the pencil does.
void testFillingThroughATransform() {
    Canvas canvas;
    REQUIRE(canvas.build());
    canvas.drawBox();

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 90.f,
                                             {kSize * 0.5f, kSize * 0.5f});
    REQUIRE(turn.valid());

    const int before = canvas.opaque();
    REQUIRE(fast::bucketFill(canvas.doc, canvas.sprite, canvas.paint, {8, 8}, {}));
    CHECK(canvas.opaque() > before);

    // Straightening it out shows the fill landed inside the box in the layer's
    // own space, not scattered somewhere else.
    REQUIRE(fast::setRotateAngle(canvas.doc, turn, 0.f));
    CompileProfile p;
    p.type = CompileProfileType::Export;
    p.outputWidth = kSize;
    p.outputHeight = kSize;
    p.palette = PalettePolicy::Unconstrained;
    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, p);
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, 7, 7).a != 0);
    CHECK(readPixel(compiled.value.raster, 0, 0).a == 0);
}

void testAwkwardSeeds() {
    Canvas canvas;
    REQUIRE(canvas.build());

    CHECK(fast::bucketArea(canvas.doc, canvas.sprite, {-1, 5}, {}).empty());
    CHECK(fast::bucketArea(canvas.doc, canvas.sprite, {5, -1}, {}).empty());
    CHECK(fast::bucketArea(canvas.doc, canvas.sprite, {kSize, 5}, {}).empty());
    CHECK(fast::bucketArea(canvas.doc, canvas.sprite, {5, kSize}, {}).empty());

    // An empty canvas fills entirely, which is correct rather than a mistake.
    CHECK(fast::bucketArea(canvas.doc, canvas.sprite, {8, 8}, {}).size()
          == kSize * kSize);
}

// A bucket is one action, however many pixels it covers.
void testABucketIsOneUndo() {
    Canvas canvas;
    REQUIRE(canvas.build());
    canvas.drawBox();
    const int before = canvas.opaque();

    canvas.doc.beginAction("Fill");
    REQUIRE(fast::bucketFill(canvas.doc, canvas.sprite, canvas.paint, {7, 7}, {}));
    canvas.doc.endAction();
    CHECK(canvas.opaque() == before + 36);

    CHECK(canvas.doc.undo());
    CHECK(canvas.opaque() == before);
    CHECK(!canvas.doc.canUndo() || canvas.doc.undoLabel() != "Fill");
}

} // namespace

int main() {
    testAFillStaysInsideItsOutline();
    testFillingOutsideCoversTheRest();
    testDiagonalsAreWallsByDefault();
    testGlobalFillIgnoresConnection();
    testFillingThroughATransform();
    testAwkwardSeeds();
    testABucketIsOneUndo();

    if (failures == 0) {
        std::printf("fast_bucket: all checks passed\n");
        return 0;
    }
    std::printf("fast_bucket: %d check(s) failed\n", failures);
    return 1;
}
