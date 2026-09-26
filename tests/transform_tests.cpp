// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// transform_tests.cpp — the claim the whole paradigm rests on.
//
// "Rotate it forty times and the fortieth is as clean as the first" is the
// sentence LiveSprite is sold on. It is also the sentence most easily believed
// without checking, so it is written here as something that can fail: not
// "roughly as good", but *the same bytes*.
//
// In a conventional editor every one of these tests would fail, because each
// rotation there resamples the result of the last one.

#include "app/ink.h"
#include "app/paint.h"
#include "app/transform.h"

#include <cmath>
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
    fast::Document   doc;
    SpriteId         sprite;
    fast::PaintLayer paint;

    bool build() {
        if (!doc.create("transform", kSize, kSize)) { return false; }
        sprite = doc.sprite();
        if (!fast::createPaintLayer(doc, sprite, "layer 1",
                                    Color{220, 90, 40, 255}, &paint)) {
            return false;
        }
        // An asymmetric shape, so a wrong rotation cannot pass by symmetry.
        for (int32_t y = 10; y < 22; ++y) {
            fast::paintPixels(doc, paint, fast::linePixels({10, y}, {17, y}));
        }
        fast::paintPixels(doc, paint, fast::linePixels({10, 10}, {21, 10}));
        return true;
    }

    std::vector<uint8_t> pixels() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        return compiled.ok() ? compiled.value.raster.pixels : std::vector<uint8_t>();
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

    Vec2f centre() const { return { kSize * 0.5f, kSize * 0.5f }; }
};

// ---------------------------------------------------------------------------

// The headline claim. Spin the angle through forty values and come back: the
// pixels are not similar to the original, they are the original.
void testFortyRotationsCostNothing() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const std::vector<uint8_t> original = canvas.pixels();
    const int originalOpaque = canvas.opaque();
    REQUIRE(!original.empty());
    REQUIRE(originalOpaque > 0);

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 0.f,
                                             canvas.centre());
    REQUIRE(turn.valid());

    // At zero degrees, adding the operation has changed nothing at all.
    CHECK(canvas.pixels() == original);

    // The rotations have to actually rotate, or everything below passes for the
    // wrong reason.
    bool anyDiffered = false;
    for (int i = 1; i <= 40; ++i) {
        REQUIRE(fast::setRotateAngle(canvas.doc, turn, static_cast<float>(i * 9)));
        CHECK(canvas.opaque() > 0);
        anyDiffered = anyDiffered || canvas.pixels() != original;
    }
    CHECK(anyDiffered);

    // Back to where it started.
    REQUIRE(fast::setRotateAngle(canvas.doc, turn, 0.f));
    CHECK(canvas.pixels() == original);
    CHECK(canvas.opaque() == originalOpaque);

    // And removing the operation leaves the same thing again.
    REQUIRE(fast::removeTransform(canvas.doc, canvas.paint.layer, turn));
    CHECK(canvas.pixels() == original);
}

// Forty angle changes must leave one operation, not forty. This is what makes
// dragging a slider affordable.
void testDrivingAnAngleAddsNothing() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const size_t before =
        canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size();

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 15.f,
                                             canvas.centre());
    REQUIRE(turn.valid());

    for (int i = 0; i < 200; ++i) {
        fast::setRotateAngle(canvas.doc, turn, static_cast<float>(i) * 1.7f);
    }

    const size_t after =
        canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size();
    CHECK(after == before + 1);
    CHECK(fast::listTransforms(canvas.doc, canvas.paint.layer).size() == 1);
}

// The same angle always produces the same picture, whatever route was taken to
// it. In a resampling editor the second one would be visibly worse.
void testTheSameAngleAlwaysGivesTheSamePixels() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 37.f,
                                             canvas.centre());
    REQUIRE(turn.valid());
    const std::vector<uint8_t> first = canvas.pixels();
    REQUIRE(!first.empty());

    // Wander a long way off and come back.
    const float wander[] = { 90.f, 180.f, -45.f, 12.5f, 300.f, 0.f, 271.f };
    for (float angle : wander) {
        fast::setRotateAngle(canvas.doc, turn, angle);
        canvas.pixels();
    }
    fast::setRotateAngle(canvas.doc, turn, 37.f);

    CHECK(canvas.pixels() == first);
}

// A quarter turn is exact: no pixel can be lost or invented.
void testQuarterTurnsPreserveEveryPixel() {
    Canvas canvas;
    REQUIRE(canvas.build());
    const int originalOpaque = canvas.opaque();

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 0.f,
                                             canvas.centre());
    REQUIRE(turn.valid());

    for (float angle : { 90.f, 180.f, 270.f, 360.f }) {
        fast::setRotateAngle(canvas.doc, turn, angle);
        CHECK(canvas.opaque() == originalOpaque);
    }
}

void testScaleAndMirrorAreEditableToo() {
    Canvas canvas;
    REQUIRE(canvas.build());
    const std::vector<uint8_t> original = canvas.pixels();

    const OperationId scale = fast::addScale(canvas.doc, canvas.paint.layer,
                                             {2.f, 2.f}, canvas.centre());
    REQUIRE(scale.valid());
    CHECK(canvas.pixels() != original);

    REQUIRE(fast::setScaleFactor(canvas.doc, scale, {1.f, 1.f}));
    CHECK(canvas.pixels() == original);

    REQUIRE(fast::removeTransform(canvas.doc, canvas.paint.layer, scale));

    // Mirroring twice about the same axis is the identity.
    const OperationId first = fast::addMirror(canvas.doc, canvas.paint.layer,
                                              MirrorAxis::X, canvas.centre());
    const OperationId second = fast::addMirror(canvas.doc, canvas.paint.layer,
                                               MirrorAxis::X, canvas.centre());
    REQUIRE(first.valid() && second.valid());
    CHECK(fast::listTransforms(canvas.doc, canvas.paint.layer).size() == 2);
    // Two mirrors about the same axis compose to the identity, so this is the
    // original picture again -- reached the long way round.
    CHECK(canvas.pixels() == original);

    fast::clearTransforms(canvas.doc, canvas.paint.layer);
    CHECK(fast::listTransforms(canvas.doc, canvas.paint.layer).empty());
    CHECK(canvas.pixels() == original);
}

// A layer shown rotated is still drawn on straight, so the pencil has to be
// mapped back into the layer's own space. Without this the cursor and the mark
// part company the moment an angle is set.
void testDrawingThroughATransformLandsWhereClicked() {
    Canvas canvas;
    REQUIRE(canvas.build());

    // With nothing applied, the mapping is the identity.
    Vec2f mapped;
    REQUIRE(fast::mapCanvasPointToLayer(canvas.doc, canvas.paint.layer, {5.f, 7.f}, &mapped));
    CHECK(mapped.x == 5.f && mapped.y == 7.f);

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 90.f,
                                             canvas.centre());
    REQUIRE(turn.valid());

    // Click an empty spot on the rotated canvas.
    const Vec2i clicked { 24, 6 };
    REQUIRE(fast::mapCanvasPointToLayer(canvas.doc, canvas.paint.layer,
                                        {static_cast<float>(clicked.x),
                                         static_cast<float>(clicked.y)}, &mapped));

    fast::paintPixels(canvas.doc, canvas.paint,
                      {{static_cast<int32_t>(mapped.x + 0.5f),
                        static_cast<int32_t>(mapped.y + 0.5f)}});

    // The new pixel appears where the click was, not where the source is.
    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, clicked.x, clicked.y).a != 0);
}

// An offset too: a click lands where it was made, not where the drawing is.
void testDrawingThroughAnOffsetLandsWhereClicked() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(fast::addOffset(canvas.doc, canvas.paint.layer, {6.f, -2.f}).valid());
    Vec2f mapped;
    REQUIRE(fast::mapCanvasPointToLayer(canvas.doc, canvas.paint.layer, {20.f, 9.f}, &mapped));
    CHECK(std::fabs(mapped.x - 14.f) < 0.001f && std::fabs(mapped.y - 11.f) < 0.001f);
    fast::paintPixels(canvas.doc, canvas.paint,
                      {{static_cast<int32_t>(mapped.x + 0.5f), static_cast<int32_t>(mapped.y + 0.5f)}});
    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, 20, 9).a != 0);
}

// A colour the layer has never had, painted on a turned layer: its new
// element goes under the turn with the rest of the drawing, so it too lands
// where it was clicked.
void testANewColourOnATurnedLayerTurnsToo() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(fast::addRotate(canvas.doc, canvas.paint.layer, 90.f, canvas.centre()).valid());
    const Vec2i clicked { 24, 6 };
    Vec2f mapped;
    REQUIRE(fast::mapCanvasPointToLayer(canvas.doc, canvas.paint.layer,
                                        {static_cast<float>(clicked.x),
                                         static_cast<float>(clicked.y)}, &mapped));
    fast::Ink blue;
    blue.colour = Color{30, 60, 220, 255};
    fast::InkStroke stroke;
    REQUIRE(fast::beginInkStroke(canvas.doc, canvas.paint.layer, blue, &stroke));
    REQUIRE(fast::strokeInk(canvas.doc, stroke,
                            {{static_cast<int32_t>(mapped.x + 0.5f),
                              static_cast<int32_t>(mapped.y + 0.5f)}}));
    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(readPixel(compiled.value.raster, clicked.x, clicked.y).b == 220);
}

// A scale of zero has no inverse, and saying so beats drawing in the wrong place.
void testAnUninvertibleTransformIsReported() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const OperationId scale = fast::addScale(canvas.doc, canvas.paint.layer,
                                             {0.f, 0.f}, canvas.centre());
    REQUIRE(scale.valid());

    Vec2f mapped;
    CHECK(!fast::mapCanvasPointToLayer(canvas.doc, canvas.paint.layer,
                                       {4.f, 4.f}, &mapped));
}

// The transform is part of the document, so it is still a parameter after a
// round trip -- not baked into the pixels on the way out.
void testTransformsSurviveAFileRoundTrip() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 33.f,
                                             canvas.centre());
    REQUIRE(turn.valid());
    const std::vector<uint8_t> rotated = canvas.pixels();

    std::string error;
    const std::string path = "transform_test.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    SpriteId sprite;
    std::vector<fast::PaintLayer> layers;
    REQUIRE(fast::adoptPaintLayers(reopened, &sprite, &layers));
    REQUIRE(!layers.empty());

    auto compiled = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(compiled.value.raster.pixels == rotated);

    // Still editable, and still able to give back the original.
    const std::vector<fast::TransformEntry> restored =
        fast::listTransforms(reopened, layers.front().layer);
    REQUIRE(restored.size() == 1);
    CHECK(restored.front().kind == fast::TransformKind::Rotate);
    CHECK(restored.front().angleDegrees == 33.f);

    REQUIRE(fast::setRotateAngle(reopened, restored.front().id, 0.f));
    auto straightened = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(straightened.ok());

    fast::clearTransforms(canvas.doc, canvas.paint.layer);
    CHECK(straightened.value.raster.pixels == canvas.pixels());
}

// Undo has to cover a transform like anything else.
void testTransformsAreUndoable() {
    Canvas canvas;
    REQUIRE(canvas.build());
    const std::vector<uint8_t> original = canvas.pixels();

    canvas.doc.beginAction("Rotate");
    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 45.f,
                                             canvas.centre());
    canvas.doc.endAction();
    REQUIRE(turn.valid());
    CHECK(canvas.pixels() != original);

    CHECK(canvas.doc.undo());
    CHECK(canvas.pixels() == original);
    CHECK(fast::listTransforms(canvas.doc, canvas.paint.layer).empty());

    CHECK(canvas.doc.redo());
    CHECK(fast::listTransforms(canvas.doc, canvas.paint.layer).size() == 1);
}

} // namespace

int main() {
    testFortyRotationsCostNothing();
    testDrivingAnAngleAddsNothing();
    testTheSameAngleAlwaysGivesTheSamePixels();
    testQuarterTurnsPreserveEveryPixel();
    testScaleAndMirrorAreEditableToo();
    testDrawingThroughATransformLandsWhereClicked();
    testDrawingThroughAnOffsetLandsWhereClicked();
    testANewColourOnATurnedLayerTurnsToo();
    testAnUninvertibleTransformIsReported();
    testTransformsSurviveAFileRoundTrip();
    testTransformsAreUndoable();

    if (failures == 0) {
        std::printf("fast_transform: all checks passed\n");
        return 0;
    }
    std::printf("fast_transform: %d check(s) failed\n", failures);
    return 1;
}
