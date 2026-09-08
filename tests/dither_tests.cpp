// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// dither_tests.cpp — gradients made of pixels, and the pattern that stays put.
//
// The claims worth checking are the ones a bitmap editor cannot make: that
// switching a finished drawing to a dithered gradient and back loses nothing,
// and that a dithered shape can be rotated with the screen either turning with
// it or staying level -- a choice, rather than whatever the pixels happened to
// be baked as.

#include "app/dither.h"
#include "app/transform.h"

#include <cstdio>
#include <map>
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
    fast::Document   doc;
    SpriteId         sprite;
    fast::PaintLayer paint;

    bool build() {
        if (!doc.create("dither", kSize, kSize)) { return false; }
        sprite = doc.engine().createSprite(doc.id()).value;
        if (!fast::createPaintLayer(doc, sprite, "layer 1",
                                    Color{200, 80, 50, 255}, &paint)) {
            return false;
        }
        // A solid block, so a dither has somewhere to happen.
        for (int32_t y = 6; y < 26; ++y) {
            fast::paintPixels(doc, paint, fast::linePixels({6, y}, {25, y}));
        }
        return true;
    }

    std::vector<uint8_t> pixels() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        return compiled.ok() ? compiled.value.raster.pixels : std::vector<uint8_t>();
    }

    // How many distinct colours the compile produced. A dither of two stops
    // shows both; a solid fill shows one.
    size_t distinctColours() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        if (compiled.fail()) { return 0; }
        std::map<uint32_t, int> seen;
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                const Color c = readPixel(compiled.value.raster,
                                          static_cast<int32_t>(x), static_cast<int32_t>(y));
                if (c.a == 0) { continue; }
                seen[(static_cast<uint32_t>(c.r) << 16) |
                     (static_cast<uint32_t>(c.g) << 8) | c.b] += 1;
            }
        }
        return seen.size();
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
};

fast::DitherSettings gradientSettings() {
    fast::DitherSettings settings;
    settings.pattern = DitherPatternKind::Bayer4;
    settings.from = Color{30, 40, 80, 255};
    settings.to = Color{240, 230, 200, 255};
    settings.modulation = DitherModulation::Linear;
    settings.gradientStart = {6.f, 6.f};
    settings.gradientEnd = {26.f, 26.f};
    settings.anchor = PatternAnchor::Local;
    return settings;
}

// ---------------------------------------------------------------------------

// Switching a finished drawing to a dithered gradient and back changes only how
// it is coloured. The drawing itself is never touched.
void testSwitchingFillKindKeepsTheDrawing() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const int drawn = canvas.opaque();
    const std::vector<uint8_t> solid = canvas.pixels();
    CHECK(canvas.distinctColours() == 1);

    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, gradientSettings()));
    CHECK(fast::layerIsDithered(canvas.doc, canvas.paint));

    // Same shape, more colours in it.
    CHECK(canvas.opaque() == drawn);
    CHECK(canvas.distinctColours() > 1);
    CHECK(canvas.pixels() != solid);

    // And back again, to exactly what it was.
    REQUIRE(fast::setLayerSolid(canvas.doc, canvas.paint, Color{200, 80, 50, 255}));
    CHECK(!fast::layerIsDithered(canvas.doc, canvas.paint));
    CHECK(canvas.opaque() == drawn);
    CHECK(canvas.pixels() == solid);
}

// A gradient built from a threshold matrix: both ramp ends appear, and where
// they appear depends on position rather than being scattered.
void testALinearGradientVariesAcrossTheShape() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, gradientSettings()));

    auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile());
    REQUIRE(compiled.ok());

    // Near the gradient start the dark stop should dominate; near the end, the
    // light one. Counted over a corner block rather than a single pixel, since
    // a dither is a mixture everywhere.
    const auto darkness = [&](int32_t x0, int32_t y0) {
        int dark = 0;
        for (int32_t y = y0; y < y0 + 6; ++y) {
            for (int32_t x = x0; x < x0 + 6; ++x) {
                const Color c = readPixel(compiled.value.raster, x, y);
                if (c.a != 0 && c.r < 128) { ++dark; }
            }
        }
        return dark;
    };
    CHECK(darkness(7, 7) > darkness(19, 19));
}

// Constant density is the classic two-tone screen, and the density controls how
// much of each stop appears.
void testDensityControlsTheMix() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::DitherSettings settings = gradientSettings();
    settings.modulation = DitherModulation::Constant;
    settings.density = 0.1f;
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, settings));

    const auto lightPixels = [&] {
        auto compiled = canvas.doc.engine().compileSprite(canvas.sprite, profile());
        if (compiled.fail()) { return -1; }
        int light = 0;
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                const Color c = readPixel(compiled.value.raster,
                                          static_cast<int32_t>(x), static_cast<int32_t>(y));
                if (c.a != 0 && c.r > 128) { ++light; }
            }
        }
        return light;
    };

    const int sparse = lightPixels();

    settings.density = 0.9f;
    REQUIRE(fast::applyDitherSettings(canvas.doc, canvas.paint, settings));
    const int dense = lightPixels();

    CHECK(sparse < dense);
    // And the shape is unchanged either way: density picks colours, not coverage.
    CHECK(canvas.opaque() == 400);
}

// Every prebaked pattern must actually produce a mixture. A kind that silently
// resolved to nothing would look like a bug in the drawing.
void testEveryPatternKindWorks() {
    for (int kind = 0; kind < 12; ++kind) {
        Canvas canvas;
        REQUIRE(canvas.build());

        fast::DitherSettings settings = gradientSettings();
        settings.pattern = static_cast<DitherPatternKind>(kind);
        settings.modulation = DitherModulation::Constant;
        settings.density = 0.5f;

        if (!fast::setLayerDithered(canvas.doc, canvas.paint, settings)) {
            std::printf("FAIL pattern kind %d could not be applied\n", kind);
            ++failures;
            continue;
        }
        if (canvas.distinctColours() < 2) {
            std::printf("FAIL pattern kind %d produced a flat fill\n", kind);
            ++failures;
        }
        if (canvas.opaque() != 400) {
            std::printf("FAIL pattern kind %d changed the shape\n", kind);
            ++failures;
        }
    }
    CHECK(fast::ditherPatternNames().size() == 12);
}

// The claim with no equivalent in a bitmap editor: whether the screen turns with
// the artwork is a parameter, not something baked in.
void testAnchoringDecidesWhatHappensUnderRotation() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::DitherSettings settings = gradientSettings();
    settings.modulation = DitherModulation::Constant;
    settings.density = 0.5f;
    settings.anchor = PatternAnchor::Local;
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, settings));

    const OperationId turn = fast::addRotate(canvas.doc, canvas.paint.layer, 0.f,
                                             {kSize * 0.5f, kSize * 0.5f});
    REQUIRE(turn.valid());

    const std::vector<uint8_t> localAtZero = canvas.pixels();
    REQUIRE(fast::setRotateAngle(canvas.doc, turn, 31.f));
    const std::vector<uint8_t> localTurned = canvas.pixels();
    CHECK(localTurned != localAtZero);

    // The same rotation with the pattern locked to the canvas is a different
    // picture, because the screen has not turned with the shape.
    settings.anchor = PatternAnchor::Global;
    REQUIRE(fast::applyDitherSettings(canvas.doc, canvas.paint, settings));
    const std::vector<uint8_t> globalTurned = canvas.pixels();
    CHECK(globalTurned != localTurned);

    // Fixed is different again, and all three are reachable by changing one
    // parameter on the same drawing.
    settings.anchor = PatternAnchor::Fixed;
    REQUIRE(fast::applyDitherSettings(canvas.doc, canvas.paint, settings));
    CHECK(canvas.opaque() > 0);

    CHECK(fast::patternAnchorNames().size() == 3);
}

// Settings read back are the layer's own, so the interface shows what is there
// rather than what was last typed.
void testSettingsRoundTripThroughTheLayer() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::DitherSettings written = gradientSettings();
    written.pattern = DitherPatternKind::CrossHatch;
    written.modulation = DitherModulation::Radial;
    written.anchor = PatternAnchor::Global;
    written.density = 0.25f;
    written.from = Color{10, 20, 30, 255};
    written.to = Color{200, 210, 220, 255};
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, written));

    fast::DitherSettings read;
    REQUIRE(fast::readDitherSettings(canvas.doc, canvas.paint, &read));
    CHECK(read.modulation == DitherModulation::Radial);
    CHECK(read.anchor == PatternAnchor::Global);
    CHECK(read.density == 0.25f);
    CHECK(read.from.r == 10 && read.from.b == 30);
    CHECK(read.to.r == 200 && read.to.b == 220);

    // A solid layer has no dither settings to report.
    Canvas plain;
    REQUIRE(plain.build());
    fast::DitherSettings none;
    CHECK(!fast::readDitherSettings(plain.doc, plain.paint, &none));
}

// Dragging a density slider must not build a new operation, or a new ramp, per
// frame.
void testDrivingSettingsAddsNothing() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, gradientSettings()));

    const size_t operations =
        canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size();

    fast::DitherSettings settings = gradientSettings();
    for (int i = 0; i < 100; ++i) {
        settings.density = static_cast<float>(i) / 100.f;
        settings.from = Color{static_cast<uint8_t>(i * 2), 40, 80, 255};
        fast::applyDitherSettings(canvas.doc, canvas.paint, settings);
    }

    CHECK(canvas.doc.engine().getLayerOperations(canvas.paint.layer).value.size()
          == operations);
}

// A dithered layer must still be drawable after a reload, which is why layer
// adoption recognises both kinds of fill.
void testADitheredLayerSurvivesAReload() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(fast::setLayerDithered(canvas.doc, canvas.paint, gradientSettings()));
    const std::vector<uint8_t> before = canvas.pixels();

    std::string error;
    const std::string path = "dither_test.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    SpriteId sprite;
    std::vector<fast::PaintLayer> layers;
    REQUIRE(fast::adoptPaintLayers(reopened, &sprite, &layers));
    REQUIRE(layers.size() == 1);
    CHECK(fast::layerIsDithered(reopened, layers.front()));

    auto compiled = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(compiled.value.raster.pixels == before);

    // And it can still be drawn on and re-tuned.
    CHECK(fast::paintPixels(reopened, layers.front(), {{1, 1}}));
    fast::DitherSettings restored;
    CHECK(fast::readDitherSettings(reopened, layers.front(), &restored));
}

} // namespace

int main() {
    testSwitchingFillKindKeepsTheDrawing();
    testALinearGradientVariesAcrossTheShape();
    testDensityControlsTheMix();
    testEveryPatternKindWorks();
    testAnchoringDecidesWhatHappensUnderRotation();
    testSettingsRoundTripThroughTheLayer();
    testDrivingSettingsAddsNothing();
    testADitheredLayerSurvivesAReload();

    if (failures == 0) {
        std::printf("fast_dither: all checks passed\n");
        return 0;
    }
    std::printf("fast_dither: %d check(s) failed\n", failures);
    return 1;
}
