// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// system_tests.cpp — the features together, not one at a time.
//
// Every other test file proves one thing in isolation: a palette swap recolours
// a layer, a cycle survives a deleted frame, a sheet cell equals a lone export.
// None of them puts a document through all of it at once, and the seams are
// where an editor actually breaks: a frame duplicated *before* the palette was
// loaded, a dither whose slot is removed while a cycle plays it twice, an undo
// that has to walk back through a frame move and a palette load in one go.
//
// So this file builds one realistic document -- an animated, role-coloured,
// outlined, dithered figure with a cycle that repeats a frame -- and holds it to
// a small set of promises through everything the interface can do to it:
//
//   * a palette change recolours every frame, whenever the frame was made
//   * a cell of a sheet is always what the frame alone compiles to
//   * undo is exact: every step back reproduces the pixels recorded on the way
//   * a file round trip loses nothing that affects a pixel or a control
//   * removing a slot changes no pixel; putting it back re-attaches everything
//
// Pixels are the measure throughout, hashed per frame, because that is what a
// person sees and because every one of these features exists to change them.

#include "app/animation.h"
#include "app/document.h"
#include "app/dither.h"
#include "app/file_io.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/palette_io.h"
#include "app/sheet.h"
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

constexpr uint32_t kCanvas = 16;

// The roles the figure is drawn through. Named here so a test reads as "the
// body slot", not "slot 3".
constexpr ls::ColorRole kBody    = 0;
constexpr ls::ColorRole kShade   = 1;
constexpr ls::ColorRole kLight   = 2;
constexpr ls::ColorRole kOutline = 3;

const ls::Color kBodyColour    { 200, 90, 60, 255 };
const ls::Color kShadeColour   { 60, 30, 90, 255 };
const ls::Color kLightColour   { 240, 220, 160, 255 };
const ls::Color kOutlineColour { 20, 20, 30, 255 };

// --- looking at pixels --------------------------------------------------------

ls::RasterBuffer compileFrame(Document& doc, ls::SpriteId sprite) {
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = kCanvas;
    profile.outputHeight = kCanvas;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(sprite, profile);
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

uint64_t hashOf(const ls::RasterBuffer& raster) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t byte : raster.pixels) {
        h ^= byte;
        h *= 1099511628211ull;
    }
    return h ^ raster.width ^ (static_cast<uint64_t>(raster.height) << 32);
}

// One number per frame: the shape of the whole animation.
std::vector<uint64_t> hashEveryFrame(Document& doc) {
    std::vector<uint64_t> out;
    for (const Frame& frame : readFrames(doc)) {
        out.push_back(hashOf(compileFrame(doc, frame.sprite)));
    }
    return out;
}

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int countColour(const ls::RasterBuffer& raster, ls::Color colour) {
    int n = 0;
    for (uint32_t y = 0; y < raster.height; ++y) {
        for (uint32_t x = 0; x < raster.width; ++x) {
            if (same(ls::readPixel(raster, static_cast<int32_t>(x),
                                   static_cast<int32_t>(y)), colour)) {
                ++n;
            }
        }
    }
    return n;
}

bool cellMatches(const ls::RasterBuffer& sheet, ls::Vec2i at,
                 const ls::RasterBuffer& one, uint32_t scale) {
    for (uint32_t y = 0; y < kCanvas; ++y) {
        for (uint32_t x = 0; x < kCanvas; ++x) {
            const ls::Color expected =
                ls::readPixel(one, static_cast<int32_t>(x), static_cast<int32_t>(y));
            for (uint32_t sy = 0; sy < scale; ++sy) {
                for (uint32_t sx = 0; sx < scale; ++sx) {
                    const ls::Color got = ls::readPixel(
                        sheet, at.x + static_cast<int32_t>(x * scale + sx),
                        at.y + static_cast<int32_t>(y * scale + sy));
                    if (!same(got, expected)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// The promise every sheet makes, checked for a given list of steps.
bool sheetAgreesWithFrames(Document& doc, const std::vector<int>& steps,
                           const SheetSettings& settings) {
    const std::vector<Frame> frames = readFrames(doc);
    std::vector<ls::SpriteId> sprites;
    for (int step : steps) {
        if (step < 0 || step >= static_cast<int>(frames.size())) {
            return false;
        }
        sprites.push_back(frames[static_cast<size_t>(step)].sprite);
    }
    ls::RasterBuffer sheet;
    SheetPlan plan;
    std::string error;
    if (!composeSheet(doc, sprites, settings, &sheet, &plan, &error)) {
        return false;
    }
    if (plan.cells != static_cast<int>(steps.size())) {
        return false;
    }
    for (int i = 0; i < plan.cells; ++i) {
        const ls::RasterBuffer one = compileFrame(doc, sprites[static_cast<size_t>(i)]);
        if (!cellMatches(sheet, plan.positionOf(i), one, settings.scale)) {
            return false;
        }
    }
    return true;
}

// --- the figure ----------------------------------------------------------------

// Frame 0 is drawn by hand: a body through the body slot, a highlight dithered
// between the shade and light slots, and one outline around the whole figure
// through the outline slot. Frames 1 and 2 are duplicates with a pixel moved;
// frame 3 is added blank *after* everything else and given a body of its own,
// because a sprite created later is the one that can miss a palette binding.
struct Figure {
    Document doc;
    std::vector<Frame> frames;
    PaintLayer body;         // on frame 0
    PaintLayer highlight;    // on frame 0
    PaintLayer lateBody;     // on frame 3
};

bool buildFigure(Figure& f) {
    Document& doc = f.doc;
    if (!doc.create("hero", kCanvas, kCanvas)) {
        return false;
    }
    if (!ensurePalette(doc, doc.sprite())) {
        return false;
    }
    doc.beginAction("Palette");
    // The starter palette has some slots; make the four we draw through mean
    // exactly what this test expects, whatever the starter happens to be.
    if (!setPaletteEntry(doc, kBody, kBodyColour) ||
        !setPaletteEntry(doc, kShade, kShadeColour) ||
        !setPaletteEntry(doc, kLight, kLightColour) ||
        !setPaletteEntry(doc, kOutline, kOutlineColour)) {
        doc.abandonAction();
        return false;
    }
    setPaletteLabel(doc, kBody, "body");
    setPaletteLabel(doc, kOutline, "outline");
    doc.endAction();

    // The body: a block through the body slot.
    if (!createPaintLayer(doc, doc.sprite(), "Body", kBodyColour, &f.body)) {
        return false;
    }
    doc.beginAction("Body");
    std::vector<ls::Vec2i> block;
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) {
            block.push_back({ x, y });
        }
    }
    if (!paintPixels(doc, f.body, block) || !setLayerRole(doc, f.body, kBody)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();

    // The highlight: a strip dithered between two slots.
    if (!createPaintLayer(doc, doc.sprite(), "Highlight", kLightColour, &f.highlight)) {
        return false;
    }
    doc.beginAction("Highlight");
    std::vector<ls::Vec2i> strip;
    for (int y = 5; y < 11; ++y) {
        strip.push_back({ 5, y });
        strip.push_back({ 6, y });
    }
    DitherSettings dither;
    dither.pattern = ls::DitherPatternKind::Checker;
    dither.from = kShadeColour;
    dither.to = kLightColour;
    dither.fromRole = kShade;
    dither.toRole = kLight;
    dither.modulation = ls::DitherModulation::Constant;
    dither.density = 0.5f;
    if (!paintPixels(doc, f.highlight, strip) ||
        !setLayerDithered(doc, f.highlight, dither)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();

    // One outline around the figure, through a slot, on the body layer.
    doc.beginAction("Outline");
    OutlineSettings outline;
    outline.scope = OutlineScope::Sprite;
    outline.thickness = 1;
    outline.side = ls::OutlineSide::Outside;
    outline.colour = kOutlineColour;
    outline.role = kOutline;
    if (!setOutline(doc, f.body, outline)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();

    // Two more frames of it, each nudged so they are not the same picture.
    if (duplicateFrame(doc, 0) != 1 || duplicateFrame(doc, 1) != 2) {
        return false;
    }
    for (int i = 1; i <= 2; ++i) {
        std::vector<PaintLayer> layers;
        const std::vector<Frame> frames = readFrames(doc);
        if (!adoptPaintLayers(doc, frames[static_cast<size_t>(i)].sprite, &layers) ||
            layers.empty()) {
            return false;
        }
        doc.beginAction("Nudge");
        if (!paintPixels(doc, layers.front(), {{ 12, 4 + i }, { 13, 4 + i }})) {
            doc.abandonAction();
            return false;
        }
        doc.endAction();
    }

    // A frame that did not exist when the palette was made.
    if (addFrame(doc, 2) != 3) {
        return false;
    }
    {
        const std::vector<Frame> frames = readFrames(doc);
        if (!createPaintLayer(doc, frames[3].sprite, "Body", kBodyColour, &f.lateBody)) {
            return false;
        }
        doc.beginAction("Late body");
        if (!paintPixels(doc, f.lateBody, {{ 2, 2 }, { 3, 2 }, { 2, 3 }, { 3, 3 }}) ||
            !setLayerRole(doc, f.lateBody, kBody)) {
            doc.abandonAction();
            return false;
        }
        doc.endAction();
    }

    // Names and timing, and a walk that comes back through the middle.
    if (!setFrameName(doc, 0, "stand") || !setFrameDuration(doc, 0, 250) ||
        !setFrameName(doc, 2, "reach") || !setFrameDuration(doc, 3, 40)) {
        return false;
    }
    if (addCycle(doc, "walk", 4) != 0) {
        return false;
    }
    // A new cycle is every frame, [0, 1, 2, 3]; make it [0, 1, 2, 1] -- it
    // comes back through the middle and never shows the last frame.
    if (!removeCycleStep(doc, 0, 3, 4) || addCycleStep(doc, 0, 2, 1, 4) != 3) {
        return false;
    }
    if (!setCycleLoop(doc, 0, LoopMode::PingPong, 4)) {
        return false;
    }
    f.frames = readFrames(doc);
    return f.frames.size() == 4;
}

// --- 1. one palette, every frame ------------------------------------------------

void testAPaletteChangeReachesEveryFrame() {
    Figure f;
    REQUIRE(buildFigure(f));

    // Before: every frame shows the body colour, the outline colour, and both
    // ends of the highlight ramp -- including the frame added last.
    for (const Frame& frame : f.frames) {
        const ls::RasterBuffer px = compileFrame(f.doc, frame.sprite);
        CHECK(countColour(px, kBodyColour) > 0);
        CHECK(countColour(px, kOutlineColour) > 0 || &frame == &f.frames[3]);
    }
    {
        const ls::RasterBuffer first = compileFrame(f.doc, f.frames[0].sprite);
        CHECK(countColour(first, kShadeColour) > 0);
        CHECK(countColour(first, kLightColour) > 0);
    }

    // Change what "body" means. Not one layer is touched.
    const ls::Color newBody { 40, 160, 90, 255 };
    const ls::Color newLight { 255, 255, 255, 255 };
    const ls::Color newOutline { 90, 0, 0, 255 };
    f.doc.beginAction("Swap");
    REQUIRE(setPaletteEntry(f.doc, kBody, newBody));
    REQUIRE(setPaletteEntry(f.doc, kLight, newLight));
    REQUIRE(setPaletteEntry(f.doc, kOutline, newOutline));
    f.doc.endAction();

    for (size_t i = 0; i < f.frames.size(); ++i) {
        const ls::RasterBuffer px = compileFrame(f.doc, f.frames[i].sprite);
        CHECK(countColour(px, kBodyColour) == 0);
        CHECK(countColour(px, newBody) > 0);
        if (i < 3) {
            CHECK(countColour(px, kOutlineColour) == 0);
            CHECK(countColour(px, newOutline) > 0);
        }
    }
    {
        // The dither's light end followed; its shade end, untouched, did not.
        const ls::RasterBuffer first = compileFrame(f.doc, f.frames[0].sprite);
        CHECK(countColour(first, kLightColour) == 0);
        CHECK(countColour(first, newLight) > 0);
        CHECK(countColour(first, kShadeColour) > 0);
    }

    // And the sheet of the cycle, repeats and all, is those frames exactly.
    SheetSettings settings;
    settings.scale = 2;
    settings.layout = SheetLayout::Row;
    const std::vector<Cycle> cycles = readCycles(f.doc, 4);
    REQUIRE(cycles.size() == 1);
    CHECK(sheetAgreesWithFrames(f.doc, cycles[0].frames, settings));

    // The description beside the sheet says what the cycle does, not just
    // which cells there are: a consumer that reads "pingpong" plays it right.
    {
        SheetPlan plan;
        std::string error;
        REQUIRE(planSheet(static_cast<int>(cycles[0].frames.size()), kCanvas, kCanvas,
                          settings, &plan, &error));
        const std::string json = sheetManifest(plan, f.frames, cycles[0].frames,
                                               cycles, "hero.png", settings.scale);
        CHECK(json.find("\"loop\": \"pingpong\"") != std::string::npos);
        CHECK(json.find("\"name\": \"walk\"") != std::string::npos);
        CHECK(json.find("\"name\": \"stand\"") != std::string::npos);
        CHECK(json.find("\"durationMs\": 250") != std::string::npos);
        // Four cells for four steps, the repeated frame twice.
        size_t cells = 0;
        for (size_t at = json.find("\"frame\":"); at != std::string::npos;
             at = json.find("\"frame\":", at + 1)) {
            ++cells;
        }
        CHECK(cells == 4);
    }

    // The controls read the same story back: roles, not colours.
    DitherSettings shown;
    REQUIRE(readDitherSettings(f.doc, f.highlight, &shown));
    CHECK(shown.fromRole == kShade && shown.toRole == kLight);
    CHECK(same(shown.to, newLight));            // the resolved colour, for display
    CHECK(same(effectiveLayerColor(f.doc, f.frames[0].sprite, f.body), newBody));
    CHECK(same(effectiveLayerColor(f.doc, f.frames[3].sprite, f.lateBody), newBody));
    CHECK(outlineOf(f.doc, f.body).role == kOutline);
}

// --- 1b. the engine says so, too ---------------------------------------------------

// The window compiles a frame only when the engine says it is dirty. Every
// frame draws through the palette, so a palette write has to dirty every
// frame -- including one made after the palette, which resolves through the
// document's binding rather than its own, and one made by duplication.
void testAPaletteWriteDirtiesEveryFrame() {
    Figure f;
    REQUIRE(buildFigure(f));

    // Clean them all, the way the cache does.
    for (const Frame& frame : f.frames) {
        compileFrame(f.doc, frame.sprite);
        auto dirty = f.doc.engine().isDirty(frame.sprite.value);
        CHECK(dirty.ok() && !dirty.value);
    }

    f.doc.beginAction("Swap");
    REQUIRE(setPaletteEntry(f.doc, kBody, ls::Color{ 5, 6, 7, 255 }));
    f.doc.endAction();
    for (size_t i = 0; i < f.frames.size(); ++i) {
        auto dirty = f.doc.engine().isDirty(f.frames[i].sprite.value);
        CHECK(dirty.ok() && dirty.value);
    }

    // The same for the other palette writes the panel makes.
    for (const Frame& frame : f.frames) { compileFrame(f.doc, frame.sprite); }
    f.doc.beginAction("Remove");
    REQUIRE(removePaletteEntry(f.doc, kLight));
    f.doc.endAction();
    for (const Frame& frame : f.frames) {
        auto dirty = f.doc.engine().isDirty(frame.sprite.value);
        CHECK(dirty.ok() && dirty.value);
    }

    for (const Frame& frame : f.frames) { compileFrame(f.doc, frame.sprite); }
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("010101\n020202\n030303\n040404\n", &file, &error));
    int dropped = 0;
    REQUIRE(applyPaletteFile(f.doc, f.frames[0].sprite, file, &dropped));
    for (const Frame& frame : f.frames) {
        auto dirty = f.doc.engine().isDirty(frame.sprite.value);
        CHECK(dirty.ok() && dirty.value);
    }
}

// --- 2. a loaded palette, and the way back ---------------------------------------

void testALoadedPaletteAppliesEverywhereAndUndoesExactly() {
    Figure f;
    REQUIRE(buildFigure(f));
    const std::vector<uint64_t> before = hashEveryFrame(f.doc);

    // A three-colour file: it has body, shade and light but no outline slot.
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("112233\n445566\n778899\n", &file, &error));
    REQUIRE(file.entries.size() == 3);

    int dropped = -1;
    REQUIRE(applyPaletteFile(f.doc, f.frames[0].sprite, file, &dropped));
    CHECK(dropped == 1);                        // the outline slot, in use
    CHECK(paletteEntries(f.doc).size() == 3);

    const std::vector<uint64_t> loaded = hashEveryFrame(f.doc);
    CHECK(loaded.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(loaded[i] != before[i]);         // every frame recoloured
    }
    {
        // The outline still names its slot and still draws, in its literal.
        CHECK(outlineOf(f.doc, f.body).role == kOutline);
        const ls::RasterBuffer first = compileFrame(f.doc, f.frames[0].sprite);
        CHECK(countColour(first, kOutlineColour) > 0);
        CHECK(countColour(first, ls::Color{ 0x11, 0x22, 0x33, 255 }) > 0);
        const ls::RasterBuffer late = compileFrame(f.doc, f.frames[3].sprite);
        CHECK(countColour(late, ls::Color{ 0x11, 0x22, 0x33, 255 }) > 0);
    }

    // The cycle and sheet are unaffected by a recolour, as they should be.
    const std::vector<Cycle> cycles = readCycles(f.doc, 4);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles[0].frames == std::vector<int>({ 0, 1, 2, 1 }));
    CHECK(sheetAgreesWithFrames(f.doc, cycles[0].frames, SheetSettings{}));

    // One undo puts back the whole palette, labels and all, on every frame.
    REQUIRE(f.doc.undo());
    CHECK(hashEveryFrame(f.doc) == before);
    {
        bool labelled = false;
        for (const PaletteEntry& entry : paletteEntries(f.doc)) {
            labelled = labelled || (entry.role == kBody && entry.label == "body");
        }
        CHECK(labelled);
    }
    REQUIRE(f.doc.redo());
    CHECK(hashEveryFrame(f.doc) == loaded);
}

// --- 3. frame surgery under a cycle and a sheet -----------------------------------

void testFrameSurgeryKeepsTheCycleAndTheSheetHonest() {
    Figure f;
    REQUIRE(buildFigure(f));
    const std::vector<uint64_t> before = hashEveryFrame(f.doc);
    const std::vector<Cycle> walk = readCycles(f.doc, 4);
    REQUIRE(walk.size() == 1 && walk[0].frames == std::vector<int>({ 0, 1, 2, 1 }));

    // Delete the frame the cycle plays twice.
    REQUIRE(deleteFrame(f.doc, 1));
    {
        const std::vector<Frame> frames = readFrames(f.doc);
        CHECK(frames.size() == 3);
        CHECK(frames[1].name == "reach");       // the pictures kept their names
        CHECK(frames[2].durationMs == 40);
        const std::vector<Cycle> cycles = readCycles(f.doc, 3);
        REQUIRE(cycles.size() == 1);
        CHECK(cycles[0].frames == std::vector<int>({ 0, 1 }));   // both uses gone
        CHECK(cycles[0].loop == LoopMode::PingPong);
        CHECK(cycles[0].name == "walk");
        SheetSettings settings;
        settings.layout = SheetLayout::Column;
        CHECK(sheetAgreesWithFrames(f.doc, cycles[0].frames, settings));
    }

    // Move the late frame to the front: the cycle names pictures, so it now
    // says [1, 2] and plays the same pictures it did.
    REQUIRE(moveFrame(f.doc, 2, 0));
    {
        const std::vector<Frame> frames = readFrames(f.doc);
        CHECK(frames[0].durationMs == 40);
        CHECK(frames[1].name == "stand");
        const std::vector<Cycle> cycles = readCycles(f.doc, 3);
        REQUIRE(cycles.size() == 1);
        CHECK(cycles[0].frames == std::vector<int>({ 1, 2 }));
        CHECK(sheetAgreesWithFrames(f.doc, cycles[0].frames, SheetSettings{}));
        // And every-frame order follows the move too.
        CHECK(sheetAgreesWithFrames(f.doc, { 0, 1, 2 }, SheetSettings{}));
    }

    // A palette change in this state still reaches the moved frame.
    f.doc.beginAction("Swap");
    REQUIRE(setPaletteEntry(f.doc, kBody, ls::Color{ 1, 2, 3, 255 }));
    f.doc.endAction();
    {
        const std::vector<Frame> frames = readFrames(f.doc);
        for (const Frame& frame : frames) {
            CHECK(countColour(compileFrame(f.doc, frame.sprite), ls::Color{ 1, 2, 3, 255 }) > 0);
        }
    }

    // Three undos: the swap, the move, the delete. Back to the exact pixels
    // and the exact cycle.
    REQUIRE(f.doc.undo() && f.doc.undo() && f.doc.undo());
    CHECK(hashEveryFrame(f.doc) == before);
    {
        const std::vector<Cycle> cycles = readCycles(f.doc, 4);
        REQUIRE(cycles.size() == 1);
        CHECK(cycles[0].frames == std::vector<int>({ 0, 1, 2, 1 }));
        CHECK(cycles[0].loop == LoopMode::PingPong);
    }
}

// --- 4. the file round trip -------------------------------------------------------

void testARoundTripLosesNothing() {
    Figure f;
    REQUIRE(buildFigure(f));
    const std::vector<uint64_t> before = hashEveryFrame(f.doc);

    const std::string path = "fast_system_roundtrip.lsprite";
    std::string error;
    REQUIRE(f.doc.save(path, &error));

    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);

    // Frames: count, order, names, timing.
    const std::vector<Frame> frames = readFrames(again);
    REQUIRE(frames.size() == 4);
    CHECK(frames[0].name == "stand" && frames[0].durationMs == 250);
    CHECK(frames[1].name.empty() && frames[1].durationMs == kDefaultFrameMs);
    CHECK(frames[2].name == "reach");
    CHECK(frames[3].durationMs == 40);

    // Cycles.
    const std::vector<Cycle> cycles = readCycles(again, 4);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles[0].name == "walk");
    CHECK(cycles[0].frames == std::vector<int>({ 0, 1, 2, 1 }));
    CHECK(cycles[0].loop == LoopMode::PingPong);

    // Palette: colours and labels.
    {
        int seen = 0;
        for (const PaletteEntry& entry : paletteEntries(again)) {
            if (entry.role == kBody)    { CHECK(same(entry.color, kBodyColour)); CHECK(entry.label == "body"); ++seen; }
            if (entry.role == kOutline) { CHECK(same(entry.color, kOutlineColour)); CHECK(entry.label == "outline"); ++seen; }
        }
        CHECK(seen == 2);
    }

    // The controls: what the panels would show for the reopened layers.
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(again, frames[0].sprite, &layers));
    REQUIRE(layers.size() == 2);
    CHECK(layerRole(again, layers[0]) == kBody);
    CHECK(layerIsDithered(again, layers[1]));
    DitherSettings dither;
    REQUIRE(readDitherSettings(again, layers[1], &dither));
    CHECK(dither.fromRole == kShade && dither.toRole == kLight);
    CHECK(dither.pattern == ls::DitherPatternKind::Checker);
    CHECK(dither.modulation == ls::DitherModulation::Constant);
    CHECK(hasOutline(again, layers[0]));
    const OutlineSettings outline = outlineOf(again, layers[0]);
    CHECK(outline.scope == OutlineScope::Sprite);
    CHECK(outline.role == kOutline);
    CHECK(outline.thickness == 1);
    CHECK(outline.side == ls::OutlineSide::Outside);

    std::vector<PaintLayer> late;
    REQUIRE(adoptPaintLayers(again, frames[3].sprite, &late));
    REQUIRE(late.size() == 1);
    CHECK(layerRole(again, late[0]) == kBody);

    // And the pixels, frame for frame.
    CHECK(hashEveryFrame(again) == before);

    // A palette change in the reopened document still reaches every frame:
    // the bindings came back, not just the colours.
    again.beginAction("Swap");
    REQUIRE(setPaletteEntry(again, kBody, ls::Color{ 9, 9, 9, 255 }));
    again.endAction();
    for (const Frame& frame : readFrames(again)) {
        CHECK(countColour(compileFrame(again, frame.sprite), ls::Color{ 9, 9, 9, 255 }) > 0);
    }
    CHECK(sheetAgreesWithFrames(again, cycles[0].frames, SheetSettings{}));
}

// --- 5. the history, walked back through everything ----------------------------

void testUndoIsExactAcrossEveryFeature() {
    Figure f;
    REQUIRE(buildFigure(f));

    // A recording: the pixels of every frame after each action, so each undo
    // has a definite answer to be held to.
    // Pixels and the cycle's step list: a step move changes no pixel, so the
    // hashes alone would not notice an undo that skipped it.
    struct Moment {
        std::vector<uint64_t> pixels;
        std::vector<int>      walk;
    };
    const auto now = [&]() {
        Moment m;
        m.pixels = hashEveryFrame(f.doc);
        const std::vector<Cycle> cycles =
            readCycles(f.doc, static_cast<int>(readFrames(f.doc).size()));
        if (!cycles.empty()) {
            m.walk = cycles[0].frames;
        }
        return m;
    };
    const auto equal = [](const Moment& a, const Moment& b) {
        return a.pixels == b.pixels && a.walk == b.walk;
    };
    std::vector<Moment> record;
    record.push_back(now());
    const auto act = [&](bool ok) {
        CHECK(ok);
        record.push_back(now());
    };

    // A mixed sequence, each step one history entry.
    f.doc.beginAction("Swap");
    act(setPaletteEntry(f.doc, kBody, ls::Color{ 10, 20, 30, 255 }));
    f.doc.endAction();

    act(duplicateFrame(f.doc, 3) == 4);                       // 5 frames

    f.doc.beginAction("Thicken");
    OutlineSettings outline = outlineOf(f.doc, f.body);
    outline.thickness = 2;
    act(setOutline(f.doc, f.body, outline));
    f.doc.endAction();

    act(moveFrame(f.doc, 4, 0));

    // The step row: walk is [1, 2, 3, 2] after the move above. Reverse the
    // middle, drop a step, add one.
    act(moveCycleStep(f.doc, 0, 1, 2, 5));                   // [1, 3, 2, 2]
    CHECK(record.back().walk == std::vector<int>({ 1, 3, 2, 2 }));
    act(removeCycleStep(f.doc, 0, 3, 5));                    // [1, 3, 2]
    act(addCycleStep(f.doc, 0, 0, 0, 5) == 1);               // [1, 0, 3, 2]
    CHECK(record.back().walk == std::vector<int>({ 1, 0, 3, 2 }));

    {
        PaletteFile file;
        std::string error;
        CHECK(parsePalette("GIMP Palette\nName: two\n#\n255 0 0 red\n0 0 255 blue\n",
                           &file, &error));
        int dropped = 0;
        act(applyPaletteFile(f.doc, f.frames[0].sprite, file, &dropped));
    }

    f.doc.beginAction("Density");
    DitherSettings dither;
    CHECK(readDitherSettings(f.doc, f.highlight, &dither));
    dither.density = 0.9f;
    act(applyDitherSettings(f.doc, f.highlight, dither));
    f.doc.endAction();

    act(deleteFrame(f.doc, 2));                                // 4 frames
    CHECK(record.back().walk == std::vector<int>({ 1, 0, 2 }));  // 3 gone, 2 up

    f.doc.beginAction("Remove slot");
    act(removePaletteEntry(f.doc, 0));
    f.doc.endAction();

    f.doc.beginAction("Draw");
    act(paintPixels(f.doc, f.lateBody, {{ 14, 14 }}));
    f.doc.endAction();

    act(addCycle(f.doc, "idle", 4) == 1);

    // Every step back reproduces its recording; then every step forward.
    for (size_t i = record.size() - 1; i > 0; --i) {
        CHECK(equal(now(), record[i]));
        REQUIRE(f.doc.undo());
    }
    CHECK(equal(now(), record[0]));
    CHECK(readFrames(f.doc).size() == 4);
    CHECK(readCycles(f.doc, 4).size() == 1);
    CHECK(paletteEntries(f.doc).size() >= 4);

    for (size_t i = 1; i < record.size(); ++i) {
        REQUIRE(f.doc.redo());
        CHECK(equal(now(), record[i]));
    }
    CHECK(!f.doc.canRedo());
    CHECK(readCycles(f.doc, 4).size() == 2);
}

// --- 6. a slot removed while everything uses it ----------------------------------

void testRemovingASlotChangesNoPixelAndPuttingItBackReattaches() {
    Figure f;
    REQUIRE(buildFigure(f));
    const std::vector<uint64_t> before = hashEveryFrame(f.doc);

    CHECK(paletteRoleInUse(f.doc, kBody));
    CHECK(paletteRoleInUse(f.doc, kLight));         // through a ramp end
    CHECK(paletteRoleInUse(f.doc, kOutline));       // through an outline

    f.doc.beginAction("Remove");
    REQUIRE(removePaletteEntry(f.doc, kBody));
    REQUIRE(removePaletteEntry(f.doc, kLight));
    REQUIRE(removePaletteEntry(f.doc, kOutline));
    f.doc.endAction();

    // The promise: nothing on screen changed. Every layer falls back to the
    // literal it was showing, on every frame.
    CHECK(hashEveryFrame(f.doc) == before);

    // The layers still say which slot they meant. That is what lets a panel
    // offer to put it back, and what makes putting it back work.
    CHECK(layerRole(f.doc, f.body) == kBody);
    CHECK(layerRole(f.doc, f.lateBody) == kBody);
    CHECK(outlineOf(f.doc, f.body).role == kOutline);
    DitherSettings dither;
    REQUIRE(readDitherSettings(f.doc, f.highlight, &dither));
    CHECK(dither.toRole == kLight);
    CHECK(paletteRoleInUse(f.doc, kBody));          // in use, though absent

    // Adding a colour does not land in the hole, so nothing snaps to it by
    // accident.
    f.doc.beginAction("Add");
    const ls::ColorRole fresh = addPaletteEntry(f.doc, f.frames[0].sprite, ls::Color{ 7, 7, 7, 255 });
    f.doc.endAction();
    CHECK(fresh != kBody && fresh != kLight && fresh != kOutline);
    CHECK(hashEveryFrame(f.doc) == before);

    // Setting the slot again is the deliberate way back, and everything that
    // named it follows at once -- on every frame.
    f.doc.beginAction("Restore");
    REQUIRE(setPaletteEntry(f.doc, kBody, ls::Color{ 0, 200, 0, 255 }));
    f.doc.endAction();
    for (const Frame& frame : readFrames(f.doc)) {
        const ls::RasterBuffer px = compileFrame(f.doc, frame.sprite);
        CHECK(countColour(px, ls::Color{ 0, 200, 0, 255 }) > 0);
        CHECK(countColour(px, kBodyColour) == 0);
    }
    // The light end and the outline, still detached, still show their literals.
    {
        const ls::RasterBuffer first = compileFrame(f.doc, f.frames[0].sprite);
        CHECK(countColour(first, kLightColour) > 0);
        CHECK(countColour(first, kOutlineColour) > 0);
    }

    // Undo the lot: removal included, the palette is whole again.
    REQUIRE(f.doc.undo() && f.doc.undo() && f.doc.undo());
    CHECK(hashEveryFrame(f.doc) == before);
    CHECK(paletteEntries(f.doc).size() >= 4);
}

// --- 7. two palettes, a swap, and a frame of its own -------------------------------

// The feature the engine exists for, held together with everything else: a
// second palette, a document swap that recolours every frame in one step, a
// frame bound to a palette of its own that sits the swap out, and all of it
// through the sheet, a round trip and undo.
void testASwapRecoloursTheAnimationAndABoundFrameSitsItOut() {
    Figure f;
    REQUIRE(buildFigure(f));
    const std::vector<uint64_t> day = hashEveryFrame(f.doc);
    const ls::PaletteId dayId = documentPalette(f.doc);
    REQUIRE(dayId.valid());

    f.doc.beginAction("Night palette");
    const ls::PaletteId nightId = addPalette(f.doc, "night", dayId);
    REQUIRE(nightId.valid());
    const ls::Color nightBody { 30, 40, 120, 255 };
    REQUIRE(setPaletteEntry(f.doc, nightId, kBody, nightBody));
    f.doc.endAction();
    CHECK(hashEveryFrame(f.doc) == day);            // making one changes nothing

    // Frame 2 -- "reach" -- is the flash frame: white body, whatever the day.
    f.doc.beginAction("Flash palette");
    const ls::PaletteId flashId = addPalette(f.doc, "flash", dayId);
    REQUIRE(flashId.valid());
    REQUIRE(setPaletteEntry(f.doc, flashId, kBody, ls::Color{ 255, 255, 255, 255 }));
    REQUIRE(bindFrame(f.doc, f.frames[2].sprite, flashId));
    f.doc.endAction();
    {
        const ls::RasterBuffer reach = compileFrame(f.doc, f.frames[2].sprite);
        CHECK(countColour(reach, ls::Color{ 255, 255, 255, 255 }) > 0);
        CHECK(countColour(reach, kBodyColour) == 0);
        CHECK(countColour(compileFrame(f.doc, f.frames[1].sprite), kBodyColour) > 0);
    }

    // The swap.
    f.doc.beginAction("Swap");
    REQUIRE(usePalette(f.doc, nightId));
    f.doc.endAction();
    const std::vector<uint64_t> night = hashEveryFrame(f.doc);
    for (size_t i = 0; i < f.frames.size(); ++i) {
        const ls::RasterBuffer px = compileFrame(f.doc, f.frames[i].sprite);
        if (i == 2) {
            CHECK(countColour(px, ls::Color{ 255, 255, 255, 255 }) > 0);   // sat it out
        } else {
            CHECK(countColour(px, nightBody) > 0);
            CHECK(countColour(px, kBodyColour) == 0);
            CHECK(night[i] != day[i]);
        }
    }

    // The sheet is the frames, per-frame palettes included.
    const std::vector<Cycle> cycles = readCycles(f.doc, 4);
    REQUIRE(cycles.size() == 1);
    CHECK(sheetAgreesWithFrames(f.doc, cycles[0].frames, SheetSettings{}));

    // The panels: each frame's palette is what it draws with.
    CHECK(paletteFor(f.doc, f.frames[0].sprite) == nightId);
    CHECK(paletteFor(f.doc, f.frames[2].sprite) == flashId);
    CHECK(same(effectiveLayerColor(f.doc, f.frames[0].sprite, f.body), nightBody));
    DitherSettings shown;
    REQUIRE(readDitherSettings(f.doc, f.highlight, &shown));
    CHECK(same(shown.to, kLightColour));            // night copied day's light

    // A round trip keeps every palette, name and binding.
    {
        const std::string path = "fast_system_swap.lsprite";
        std::string error;
        REQUIRE(f.doc.save(path, &error));
        Document again;
        REQUIRE(again.open(path, &error));
        deleteFile(path);
        const std::vector<PaletteInfo> palettes = listPalettes(again);
        REQUIRE(palettes.size() == 3);
        CHECK(palettes[1].name == "night" && palettes[2].name == "flash");
        CHECK(documentPalette(again) == palettes[1].id);
        const std::vector<Frame> frames = readFrames(again);
        REQUIRE(frames.size() == 4);
        CHECK(frameBinding(again, frames[2].sprite) == palettes[2].id);
        CHECK(!frameBinding(again, frames[0].sprite).valid());
        CHECK(hashEveryFrame(again) == night);
        // And ensurePalette on open leaves those bindings exactly alone.
        REQUIRE(ensurePalette(again, frames[0].sprite));
        REQUIRE(ensurePalette(again, frames[2].sprite));
        CHECK(frameBinding(again, frames[2].sprite) == palettes[2].id);
        CHECK(hashEveryFrame(again) == night);
    }

    // Undo the swap: day again, flash frame still flashing.
    REQUIRE(f.doc.undo());
    CHECK(documentPalette(f.doc) == dayId);
    CHECK(countColour(compileFrame(f.doc, f.frames[0].sprite), kBodyColour) > 0);
    CHECK(countColour(compileFrame(f.doc, f.frames[2].sprite), ls::Color{ 255, 255, 255, 255 }) > 0);
    // Undo the flash binding and palette, and the night palette: back to day.
    REQUIRE(f.doc.undo() && f.doc.undo());
    CHECK(hashEveryFrame(f.doc) == day);
    CHECK(listPalettes(f.doc).size() == 1);
    REQUIRE(f.doc.redo() && f.doc.redo() && f.doc.redo());
    CHECK(hashEveryFrame(f.doc) == night);
}

} // namespace

int main() {
    testAPaletteChangeReachesEveryFrame();
    testAPaletteWriteDirtiesEveryFrame();
    testALoadedPaletteAppliesEverywhereAndUndoesExactly();
    testFrameSurgeryKeepsTheCycleAndTheSheetHonest();
    testARoundTripLosesNothing();
    testUndoIsExactAcrossEveryFeature();
    testRemovingASlotChangesNoPixelAndPuttingItBackReattaches();
    testASwapRecoloursTheAnimationAndABoundFrameSitsItOut();

    if (failures == 0) {
        std::printf("system_tests: all checks passed\n");
        return 0;
    }
    std::printf("system_tests: %d check(s) failed\n", failures);
    return 1;
}
