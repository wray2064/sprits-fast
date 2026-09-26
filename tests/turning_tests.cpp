// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// turning_tests.cpp — what a person draws, turned.
//
// The flow that broke: a blob drawn with the pencil, filled with the bucket,
// the layer turned, a dithered gradient laid over the turned fill, and the
// gradient taken away again. Every mark is a shape -- the outline its strokes,
// the fill a face, the gradient a face with a rule -- so at every angle the
// outline is drawn as a line at that angle, the fill is found again inside it,
// the dither stays a dither, and taking the gradient away leaves what was
// under it as it was.

#include "app/bucket.h"
#include "app/dither.h"
#include "app/element.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/transform.h"

#include <cstdio>
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

using namespace fast;

namespace {

constexpr int kSize = 64;
const ls::Color kOutline { 224, 143, 64, 255 };
const ls::Color kFill { 30, 63, 96, 255 };
const ls::Color kDark { 20, 20, 40, 255 };
const ls::Color kLight { 220, 220, 240, 255 };

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

ls::RasterBuffer picture(Document& doc) {
    ls::CompileProfile p;
    p.type = ls::CompileProfileType::Export;
    p.outputWidth = kSize;
    p.outputHeight = kSize;
    p.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(doc.sprite(), p);
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

// Whatever a 4-connected walk from the border reaches without crossing the
// outline's colour.
std::vector<uint8_t> outsideOf(const ls::RasterBuffer& raster) {
    std::vector<uint8_t> outside(kSize * kSize, 0);
    std::vector<ls::Vec2i> stack;
    for (int i = 0; i < kSize; ++i) {
        stack.push_back({ i, 0 });
        stack.push_back({ i, kSize - 1 });
        stack.push_back({ 0, i });
        stack.push_back({ kSize - 1, i });
    }
    while (!stack.empty()) {
        const ls::Vec2i p = stack.back();
        stack.pop_back();
        if (p.x < 0 || p.y < 0 || p.x >= kSize || p.y >= kSize) {
            continue;
        }
        const int at = p.y * kSize + p.x;
        if (outside[at] || same(ls::readPixel(raster, p.x, p.y), kOutline)) {
            continue;
        }
        outside[at] = 1;
        stack.push_back({ p.x + 1, p.y });
        stack.push_back({ p.x - 1, p.y });
        stack.push_back({ p.x, p.y + 1 });
        stack.push_back({ p.x, p.y - 1 });
    }
    return outside;
}

struct Look {
    int leaked = 0;       // fill or dither outside the outline
    int bare = 0;         // nothing drawn inside the outline
    int inside = 0;       // pixels inside the outline
    int clashes = 0;      // two dither pixels side by side the same
};

Look look(const ls::RasterBuffer& raster) {
    Look out;
    const std::vector<uint8_t> outside = outsideOf(raster);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const ls::Color c = ls::readPixel(raster, x, y);
            const bool painted = c.a != 0 && !same(c, kOutline);
            if (outside[y * kSize + x]) {
                out.leaked += painted ? 1 : 0;
            } else if (!same(c, kOutline)) {
                ++out.inside;
                out.bare += c.a == 0 ? 1 : 0;
                if (x + 1 < kSize) {
                    const ls::Color right = ls::readPixel(raster, x + 1, y);
                    const bool dither = same(c, kDark) || same(c, kLight);
                    out.clashes += dither && same(c, right) ? 1 : 0;
                }
            }
        }
    }
    return out;
}

// The blob of the report, drawn as its thirteen drags of the pencil.
bool drawBlob(Document& doc, ls::LayerId layer) {
    const ls::Vec2i corners[13] = {
        { 20, 12 }, { 32, 10 }, { 40, 18 }, { 50, 16 }, { 54, 26 }, { 44, 32 }, { 52, 42 },
        { 42, 50 }, { 32, 44 }, { 22, 52 }, { 14, 42 }, { 18, 32 }, { 10, 22 },
    };
    Ink ink;
    ink.colour = kOutline;
    for (int i = 0; i < 13; ++i) {
        doc.beginAction("Pencil");
        InkStroke stroke;
        if (!beginInkStroke(doc, layer, ink, &stroke) ||
            !strokeAlong(doc, stroke, 0, linePixels(corners[i], corners[(i + 1) % 13]), PenBrush{})) {
            doc.abandonAction();
            return false;
        }
        doc.endAction();
    }
    return true;
}

void testPencilBucketTurnGradient() {
    Document doc;
    REQUIRE(doc.create("turning", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "blob", kOutline, &layer));
    REQUIRE(drawBlob(doc, layer.layer));

    // Filled as drawn.
    Ink fill;
    fill.colour = kFill;
    doc.beginAction("Fill");
    InkStroke bucket;
    REQUIRE(beginInkStroke(doc, layer.layer, fill, &bucket));
    REQUIRE(bucketFill(doc, doc.sprite(), bucket, { 32, 30 }, BucketSettings{}));
    pruneEmptyInks(doc, layer.layer);
    doc.endAction();
    const Look drawn = look(picture(doc));
    CHECK(drawn.leaked == 0 && drawn.bare == 0 && drawn.inside > 400);

    // Turned through every angle: the outline closed, the fill inside it and
    // up to it.
    const ls::OperationId turn = addRotate(doc, layer.layer, 3.f, { 32.f, 32.f },
                                           ls::SamplingPolicy::RotSprite);
    REQUIRE(turn.valid());
    int leaked = 0;
    int bare = 0;
    int empty = 0;
    for (int angle = 3; angle < 360; angle += 7) {
        REQUIRE(setRotateAngle(doc, turn, static_cast<float>(angle)));
        const Look turned = look(picture(doc));
        leaked += turned.leaked;
        bare += turned.bare;
        empty += turned.inside < 400 ? 1 : 0;
    }
    CHECK(leaked == 0);
    CHECK(bare == 0);
    CHECK(empty == 0);
    if (leaked != 0 || bare != 0 || empty != 0) {
        std::printf("    turned: %d pixels leaked, %d bare inside, %d angles broken open\n",
                    leaked, bare, empty);
    }

    // A level dither over the turned fill, where the fill is: crisp, and
    // inside the outline too.
    REQUIRE(setRotateAngle(doc, turn, 23.f));
    DitherSettings dither;
    dither.from = kDark;
    dither.to = kLight;
    dither.modulation = ls::DitherModulation::Constant;
    dither.density = 0.5f;
    const ls::IntervalSet area = bucketAreaOnLayer(doc, doc.sprite(), layer.layer, { 32, 30 },
                                                   BucketSettings{});
    REQUIRE(!area.empty());
    PaintLayer gradient;
    doc.beginAction("Gradient");
    REQUIRE(addGradientElement(doc, layer.layer, area, true, dither, &gradient));
    doc.endAction();
    int shaded = 0;
    for (int angle = 23; angle < 360; angle += 37) {
        REQUIRE(setRotateAngle(doc, turn, static_cast<float>(angle)));
        const ls::RasterBuffer raster = picture(doc);
        const Look dithered = look(raster);
        CHECK(dithered.leaked == 0 && dithered.bare == 0 && dithered.clashes == 0);
        if (dithered.leaked != 0 || dithered.bare != 0 || dithered.clashes != 0) {
            std::printf("    dithered at %d: %d leaked, %d bare, %d clashes\n", angle,
                        dithered.leaked, dithered.bare, dithered.clashes);
        }
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                shaded += same(ls::readPixel(raster, x, y), kDark) ? 1 : 0;
            }
        }
    }
    CHECK(shaded > 0);

    // Taking the gradient away leaves the fill and the outline as they were.
    REQUIRE(setRotateAngle(doc, turn, 23.f));
    Element made;
    for (const Element& element : elementsOf(doc, layer.layer)) {
        if (element.fill == gradient.fill) {
            made = element;
        }
    }
    REQUIRE(made.valid());
    REQUIRE(removeElement(doc, layer.layer, made));
    const ls::RasterBuffer after = picture(doc);
    const Look back = look(after);
    CHECK(back.leaked == 0 && back.bare == 0 && back.inside > 400);
    int dark = 0;
    int blue = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const ls::Color c = ls::readPixel(after, x, y);
            dark += same(c, kDark) || same(c, kLight) ? 1 : 0;
            blue += same(c, kFill) ? 1 : 0;
        }
    }
    CHECK(dark == 0 && blue == back.inside);
}

// Erasing across a turned outline cuts the line where it was erased, and
// nothing else: turned any way, the rest of the line is whole and the cut is
// clean -- no specks left where it was rubbed out.
void testAnErasedLineStaysCut() {
    Document doc;
    REQUIRE(doc.create("cut", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "line", kOutline, &layer));
    Ink ink;
    ink.colour = kOutline;
    doc.beginAction("Pencil");
    InkStroke stroke;
    REQUIRE(beginInkStroke(doc, layer.layer, ink, &stroke));
    REQUIRE(strokeAlong(doc, stroke, 0, linePixels({ 10, 32 }, { 54, 32 }), PenBrush{}));
    doc.endAction();
    doc.beginAction("Eraser");
    InkStroke rub;
    REQUIRE(beginEraseStroke(doc, layer.layer, &rub));
    PenBrush wide;
    wide.size = 3;
    REQUIRE(strokeAlong(doc, rub, 0, linePixels({ 30, 32 }, { 34, 32 }), wide));
    doc.endAction();
    const ls::OperationId turn = addRotate(doc, layer.layer, 0.f, { 32.f, 32.f },
                                           ls::SamplingPolicy::RotSprite);
    REQUIRE(turn.valid());
    for (int angle = 0; angle < 360; angle += 11) {
        REQUIRE(setRotateAngle(doc, turn, static_cast<float>(angle)));
        const ls::RasterBuffer raster = picture(doc);
        ls::IntervalSet drawn;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                if (ls::readPixel(raster, x, y).a != 0) {
                    drawn.intervals.push_back({ y, x, x + 1 });
                }
            }
        }
        const auto pieces = ls::geom::connectedComponents(ls::geom::normalize(drawn), true);
        CHECK(pieces.size() == 2);
        if (pieces.size() != 2) {
            std::printf("    at %d degrees: %zu pieces\n", angle, pieces.size());
        }
    }
}

} // namespace

int main() {
    testPencilBucketTurnGradient();
    testAnErasedLineStaysCut();
    if (failures == 0) {
        std::printf("fast_turning: all checks passed\n");
        return 0;
    }
    std::printf("fast_turning: %d check(s) failed\n", failures);
    return 1;
}
