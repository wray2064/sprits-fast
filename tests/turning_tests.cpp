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
#include "app/canvas_ops.h"
#include "app/dither.h"
#include "app/element.h"
#include "app/import_image.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/shape.h"
#include "app/transform.h"

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

// A document from before: its drawing a region of pixels. Opened, it is the
// same picture, made of shapes -- and turns like one.
void testAnOldDrawingOpensAsShapes() {
    Document doc;
    REQUIRE(doc.create("old", kSize, kSize));
    ls::LSContext& engine = doc.engine();
    auto layer = engine.createLayer(doc.sprite(), { "old" });
    REQUIRE(layer.ok());
    ls::IntervalSet ring;
    for (ls::Vec2i p : linePixels({ 10, 10 }, { 40, 10 })) {
        ring.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    auto region = engine.createRegionFromIntervals(doc.id(), ls::geom::normalize(ring));
    REQUIRE(region.ok());
    ls::FillSolidOp fill;
    fill.targetRegion = region.value;
    fill.fallbackColor = kOutline;
    REQUIRE(engine.addOperation(layer.value, fill).ok());
    const ls::RasterBuffer before = picture(doc);
    std::string error;
    REQUIRE(doc.save("turning_old.lsprite", &error));

    Document opened;
    REQUIRE(opened.open("turning_old.lsprite", &error));
    std::remove("turning_old.lsprite");
    CHECK(picture(opened).pixels == before.pixels);
    auto info = opened.engine().getSpriteInfo(opened.sprite());
    REQUIRE(info.ok() && info.value.layers.size() == 1);
    const std::vector<Element> elements = elementsOf(opened, info.value.layers.front());
    REQUIRE(elements.size() == 1);
    CHECK(regionMadeOf(opened, elements.front().region) == RegionMade::Strokes);
    CHECK(!opened.modified());
}

// An image opened as a document: its one-pixel outline comes in as paths,
// not as an area whose one-pixel edge comes apart, so turned it stays closed
// round its fill, as a drawn one does.
void testAnImportedOutlineTurns() {
    const ls::Vec2i corners[13] = {
        { 20, 12 }, { 32, 10 }, { 40, 18 }, { 50, 16 }, { 54, 26 }, { 44, 32 }, { 52, 42 },
        { 42, 50 }, { 32, 44 }, { 22, 52 }, { 14, 42 }, { 18, 32 }, { 10, 22 },
    };
    ls::RasterBuffer image = ls::makeRaster(kSize, kSize);
    for (int i = 0; i < 13; ++i) {
        for (ls::Vec2i p : linePixels(corners[i], corners[(i + 1) % 13])) {
            ls::writePixel(image, p.x, p.y, kOutline);
        }
    }
    // The inside, filled from the middle up to the outline.
    std::vector<ls::Vec2i> stack { { 32, 30 } };
    while (!stack.empty()) {
        const ls::Vec2i p = stack.back();
        stack.pop_back();
        if (p.x < 0 || p.y < 0 || p.x >= kSize || p.y >= kSize ||
            ls::readPixel(image, p.x, p.y).a != 0) {
            continue;
        }
        ls::writePixel(image, p.x, p.y, kFill);
        stack.push_back({ p.x + 1, p.y });
        stack.push_back({ p.x - 1, p.y });
        stack.push_back({ p.x, p.y + 1 });
        stack.push_back({ p.x, p.y - 1 });
    }

    Document doc;
    ImportReport report;
    std::string error;
    REQUIRE(documentFromFrames(doc, "blob", { image }, {}, &report, &error));
    CHECK(picture(doc).pixels == image.pixels);
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    REQUIRE(info.ok() && !info.value.layers.empty());
    const ls::LayerId layer = info.value.layers.back();
    const ls::OperationId turn = addRotate(doc, layer, 3.f, { 32.f, 32.f },
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
        std::printf("    imported, turned: %d pixels leaked, %d bare inside, %d angles broken open\n",
                    leaked, bare, empty);
    }
}

// A bucket fill on a layer of its own, bounded by an outline on the layer
// under it: the fill names the outline as a wall, so with both layers turned
// alike it meets the outline at every angle, as a fill on the outline's own
// layer does.
void testAFillMeetsAnOutlineOnAnotherLayer() {
    Document doc;
    REQUIRE(doc.create("two layers", kSize, kSize));
    PaintLayer lines;
    PaintLayer colour;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "lines", kOutline, &lines));
    REQUIRE(drawBlob(doc, lines.layer));
    REQUIRE(createPaintLayer(doc, doc.sprite(), "colour", kFill, &colour));
    doc.beginAction("Fill");
    InkStroke stroke;
    Ink ink;
    ink.colour = kFill;
    REQUIRE(beginInkStroke(doc, colour.layer, ink, &stroke));
    REQUIRE(bucketFill(doc, doc.sprite(), stroke, { 32, 30 }, BucketSettings{}));
    doc.endAction();
    const Look drawn = look(picture(doc));
    CHECK(drawn.leaked == 0 && drawn.bare == 0);

    const ls::OperationId turnLines = addRotate(doc, lines.layer, 3.f, { 32.f, 32.f },
                                                ls::SamplingPolicy::RotSprite);
    const ls::OperationId turnColour = addRotate(doc, colour.layer, 3.f, { 32.f, 32.f },
                                                 ls::SamplingPolicy::RotSprite);
    REQUIRE(turnLines.valid() && turnColour.valid());
    int leaked = 0;
    int bare = 0;
    int empty = 0;
    for (int angle = 3; angle < 360; angle += 7) {
        REQUIRE(setRotateAngle(doc, turnLines, static_cast<float>(angle)));
        REQUIRE(setRotateAngle(doc, turnColour, static_cast<float>(angle)));
        const Look turned = look(picture(doc));
        leaked += turned.leaked;
        bare += turned.bare;
        empty += turned.inside < 400 ? 1 : 0;
    }
    CHECK(leaked == 0);
    CHECK(bare == 0);
    CHECK(empty == 0);
    if (leaked != 0 || bare != 0 || empty != 0) {
        std::printf("    two layers, turned: %d pixels leaked, %d bare inside, %d angles broken open\n",
                    leaked, bare, empty);
    }
}


// ------------------------------------------------------- quarter turns --
//
// A quarter turn or a flip about the canvas's centre sends every pixel to a
// pixel, so the math has no excuse: what it draws turned must be what it drew,
// turned, pixel for pixel. The report: a sword of one-pixel lines, turned 90,
// came out with every line a pixel off -- each end was kept on a pixel's
// corner, four pixels at once, and which one it drew changed with the turn.
// Here one of every kind of mark, drawn as the tools draw it, turned both
// ways Fast turns things: the canvas (its geometry rewritten) and a Rotate or
// Mirror on the layer (the geometry moved as it compiles).

enum class Turn { Quarter, Half, ThreeQuarters, Across, Down };

// Where the picture's pixel (x, y) lands.
ls::Vec2i landed(Turn turn, int x, int y) {
    switch (turn) {
        case Turn::Quarter:       return { kSize - 1 - y, x };
        case Turn::Half:          return { kSize - 1 - x, kSize - 1 - y };
        case Turn::ThreeQuarters: return { y, kSize - 1 - x };
        case Turn::Across:        return { kSize - 1 - x, y };
        case Turn::Down:          return { x, kSize - 1 - y };
    }
    return { x, y };
}

// How many pixels of `after` are not `before` turned.
int offBy(const ls::RasterBuffer& before, const ls::RasterBuffer& after, Turn turn) {
    if (after.width != kSize || after.height != kSize) {
        return kSize * kSize;
    }
    int off = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const ls::Vec2i to = landed(turn, x, y);
            const ls::Color was = ls::readPixel(before, x, y);
            const ls::Color is = ls::readPixel(after, to.x, to.y);
            if (!same(was, is)) {
                // The first few, to say where the math went wrong.
                if (++off <= 6) {
                    std::printf("    (%d,%d) %02x%02x%02x%02x lands on (%d,%d) %02x%02x%02x%02x\n",
                                x, y, was.r, was.g, was.b, was.a, to.x, to.y, is.r, is.g, is.b, is.a);
                }
            }
        }
    }
    return off;
}

// Lines of every slope pixel art uses, a polygon, a curve, a box and an oval,
// and the pencil: each as the tool makes it (a line's ends and a path's points
// in the middle of their pixels -- linePoint -- a box by its corners).
bool drawEveryKind(Document& doc, ls::LayerId layer) {
    const ls::Color colours[] = {
        { 170, 180, 196, 255 }, { 238, 244, 248, 255 }, { 111, 125, 140, 255 },
        { 233, 185, 73, 255 },  { 122, 74, 40, 255 },   { 43, 47, 58, 255 },
    };
    int next = 0;
    const auto colour = [&] { return colours[next++ % 6]; };
    const auto line = [&](ls::Vec2i a, ls::Vec2i b) {
        ShapeParams params;
        params.from = linePoint({ static_cast<float>(a.x), static_cast<float>(a.y) });
        params.to = linePoint({ static_cast<float>(b.x), static_cast<float>(b.y) });
        ShapeLayer made;
        return addShapeTo(doc, layer, ShapeKind::Line, params, colour(), ls::kColorRoleNone, &made);
    };
    const auto path = [&](ShapeKind kind, std::vector<ls::Vec2i> points) {
        ShapeParams params;
        for (ls::Vec2i p : points) {
            params.points.push_back(linePoint({ static_cast<float>(p.x), static_cast<float>(p.y) }));
        }
        ShapeLayer made;
        return addShapeTo(doc, layer, kind, params, colour(), ls::kColorRoleNone, &made);
    };
    const auto box = [&](ShapeKind kind, ls::Vec2i from, ls::Vec2i to) {
        ShapeParams params;
        params.from = { static_cast<float>(from.x), static_cast<float>(from.y) };
        params.to = { static_cast<float>(to.x), static_cast<float>(to.y) };
        ShapeLayer made;
        return addShapeTo(doc, layer, kind, params, colour(), ls::kColorRoleNone, &made);
    };
    // The sword's diagonals, then shallow, steep, level and upright lines.
    if (!line({ 12, 17 }, { 26, 3 }) || !line({ 12, 18 }, { 26, 4 }) || !line({ 13, 18 }, { 27, 4 }) ||
        !line({ 4, 40 }, { 24, 50 }) || !line({ 30, 4 }, { 36, 22 }) || !line({ 40, 30 }, { 60, 30 }) ||
        !line({ 58, 2 }, { 58, 20 }) || !line({ 2, 60 }, { 26, 52 }) || !line({ 44, 44 }, { 47, 62 })) {
        return false;
    }
    if (!path(ShapeKind::Polygon, { { 40, 36 }, { 56, 40 }, { 50, 54 }, { 38, 48 } }) ||
        !path(ShapeKind::Curve, { { 4, 30 }, { 10, 20 }, { 18, 36 }, { 26, 26 } }) ||
        !box(ShapeKind::Rectangle, { 44, 4 }, { 52, 9 }) ||
        !box(ShapeKind::Ellipse, { 20, 54 }, { 29, 61 }) ||
        !box(ShapeKind::Ellipse, { 3, 3 }, { 7, 7 })) {
        return false;
    }
    Ink ink;
    ink.colour = kOutline;
    doc.beginAction("Pencil");
    InkStroke stroke;
    if (!beginInkStroke(doc, layer, ink, &stroke) ||
        !strokeAlong(doc, stroke, 0, linePixels({ 32, 58 }, { 40, 62 }), PenBrush{})) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

void testQuarterTurnsAndFlipsAreExact() {
    Document doc;
    PaintLayer layer;
    REQUIRE(doc.create("turns", kSize, kSize));
    REQUIRE(createPaintLayer(doc, doc.sprite(), "Layer 1", kOutline, &layer));
    REQUIRE(drawEveryKind(doc, layer.layer));
    const ls::RasterBuffer before = picture(doc);
    REQUIRE(!before.empty());

    // The canvas turned and flipped: its geometry rewritten.
    std::string error;
    const struct { Turn turn; int quarters; bool flip; bool across; const char* name; } canvas[] = {
        { Turn::Quarter, 1, false, false, "canvas 90" },
        { Turn::Half, 2, false, false, "canvas 180" },
        { Turn::ThreeQuarters, 3, false, false, "canvas 270" },
        { Turn::Across, 0, true, true, "canvas flipped across" },
        { Turn::Down, 0, true, false, "canvas flipped down" },
    };
    for (const auto& c : canvas) {
        REQUIRE(c.flip ? flipCanvas(doc, c.across, &error) : rotateCanvas(doc, c.quarters, &error));
        const int off = offBy(before, picture(doc), c.turn);
        if (off != 0) {
            std::printf("  %s: %d pixel(s) off\n", c.name, off);
        }
        CHECK(off == 0);
        REQUIRE(doc.undo());
    }
    CHECK(picture(doc).pixels == before.pixels);   // and undone, as it was

    // The layer turned and mirrored as it compiles, about the canvas's centre,
    // with the sampling a Rotate starts with.
    const ls::Vec2f centre { kSize * 0.5f, kSize * 0.5f };
    const struct { Turn turn; float degrees; const char* name; } rotations[] = {
        { Turn::Quarter, 90.f, "Rotate 90" },
        { Turn::Half, 180.f, "Rotate 180" },
        { Turn::ThreeQuarters, 270.f, "Rotate 270" },
        { Turn::ThreeQuarters, -90.f, "Rotate -90" },
    };
    for (const auto& r : rotations) {
        doc.beginAction("Rotate");
        const ls::OperationId op = addRotate(doc, layer.layer, r.degrees, centre,
                                             ls::SamplingPolicy::RotSprite);
        doc.endAction();
        REQUIRE(op.valid());
        const int off = offBy(before, picture(doc), r.turn);
        if (off != 0) {
            std::printf("  %s: %d pixel(s) off\n", r.name, off);
        }
        CHECK(off == 0);
        REQUIRE(doc.undo());
    }
    const struct { Turn turn; ls::MirrorAxis axis; const char* name; } mirrors[] = {
        { Turn::Across, ls::MirrorAxis::X, "Mirror across" },
        { Turn::Down, ls::MirrorAxis::Y, "Mirror down" },
    };
    for (const auto& m : mirrors) {
        doc.beginAction("Mirror");
        const ls::OperationId op = addMirror(doc, layer.layer, m.axis, centre);
        doc.endAction();
        REQUIRE(op.valid());
        const int off = offBy(before, picture(doc), m.turn);
        if (off != 0) {
            std::printf("  %s: %d pixel(s) off\n", m.name, off);
        }
        CHECK(off == 0);
        REQUIRE(doc.undo());
    }
}

// A document from before a line's ends were kept in the middle of pixels
// opens looking as it did, and then turns exactly.
void testALineSavedOnCornersOpensCentred() {
    Document doc;
    PaintLayer layer;
    REQUIRE(doc.create("old line", kSize, kSize));
    REQUIRE(createPaintLayer(doc, doc.sprite(), "Layer 1", kOutline, &layer));
    ShapeParams params;
    params.from = { 12.f, 17.f };           // on corners, as lines once were
    params.to = { 26.f, 3.f };
    ShapeLayer made;
    REQUIRE(addShapeTo(doc, layer.layer, ShapeKind::Line, params, kOutline, ls::kColorRoleNone,
                       &made));
    const ls::RasterBuffer drawn = picture(doc);
    const std::string path = "fast_old_line.lsprite";
    std::string error;
    REQUIRE(doc.save(path, &error));

    Document opened;
    REQUIRE(opened.open(path, &error));
    CHECK(picture(opened).pixels == drawn.pixels);
    // Its ends, read from the reopened file (whose ids are its own).
    std::vector<ls::Vec2f> ends;
    auto info = opened.engine().getSpriteInfo(opened.sprite());
    REQUIRE(info.ok() && !info.value.layers.empty());
    auto operations = opened.engine().getLayerOperations(info.value.layers.front());
    REQUIRE(operations.ok());
    for (const ls::OperationInfo& op : operations.value) {
        auto found = opened.engine().getOperation(op.id);
        if (const auto* stroke = found.ok() ? std::get_if<ls::StrokePolylineOp>(&found.value) : nullptr) {
            auto line = opened.engine().getPolyline(stroke->polyline);
            REQUIRE(line.ok());
            ends = line.value.points;
        }
    }
    REQUIRE(ends.size() == 2);
    CHECK(ends[0].x == 12.5f && ends[0].y == 17.5f && ends[1].x == 26.5f && ends[1].y == 3.5f);
    REQUIRE(rotateCanvas(opened, 1, &error));
    CHECK(offBy(drawn, picture(opened), Turn::Quarter) == 0);
    std::remove(path.c_str());
}

} // namespace

int main() {
    testQuarterTurnsAndFlipsAreExact();
    testALineSavedOnCornersOpensCentred();
    testAFillMeetsAnOutlineOnAnotherLayer();
    testAnImportedOutlineTurns();
    testAnOldDrawingOpensAsShapes();
    testPencilBucketTurnGradient();
    testAnErasedLineStaysCut();
    if (failures == 0) {
        std::printf("fast_turning: all checks passed\n");
        return 0;
    }
    std::printf("fast_turning: %d check(s) failed\n", failures);
    return 1;
}
