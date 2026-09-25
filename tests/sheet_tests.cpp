// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// sheet_tests.cpp — many frames in one image.
//
// The promise worth stating as something that can fail: a cell in a sheet is
// byte-for-byte what exporting that frame on its own would have produced. It is
// not automatic, because the engine deliberately lets a pattern be pinned to
// where the output frame sits inside something larger -- so compiling a frame
// for cell (2, 1) rather than at the origin genuinely changes its pixels unless
// the export is written to know better.

#include "app/animation.h"
#include "app/document.h"
#include "app/dither.h"
#include "app/paint.h"
#include "app/sheet.h"
#include "app/file_io.h"
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

bool build(Document& doc, int frameCount) {
    if (!doc.create("sheet", kCanvas, kCanvas)) {
        return false;
    }
    ShapeParams params;
    params.from = { 2.f, 2.f };
    params.to   = { 9.f, 9.f };
    ShapeLayer shape;
    if (!createShapeLayer(doc, doc.sprite(), ShapeKind::Rectangle, params,
                          { 255, 160, 40, 255 }, &shape)) {
        return false;
    }
    for (int i = 1; i < frameCount; ++i) {
        if (duplicateFrame(doc, i - 1) != i) {
            return false;
        }
    }
    doc.markUnmodified();
    return true;
}

// One frame on its own, compiled the way an export does it.
ls::RasterBuffer alone(Document& doc, ls::SpriteId sprite) {
    auto size = doc.engine().getCanvasSize(doc.id());
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = size.ok() ? static_cast<uint32_t>(size.value.x) : kCanvas;
    profile.outputHeight = size.ok() ? static_cast<uint32_t>(size.value.y) : kCanvas;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(sprite, profile);
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

// Whether a cell of the sheet matches a lone compile of that frame.
bool cellMatches(const ls::RasterBuffer& sheet, ls::Vec2i at,
                 const ls::RasterBuffer& one, uint32_t scale,
                 uint32_t cell = kCanvas) {
    for (uint32_t y = 0; y < cell; ++y) {
        for (uint32_t x = 0; x < cell; ++x) {
            const ls::Color expected =
                ls::readPixel(one, static_cast<int32_t>(x), static_cast<int32_t>(y));
            for (uint32_t sy = 0; sy < scale; ++sy) {
                for (uint32_t sx = 0; sx < scale; ++sx) {
                    const ls::Color got = ls::readPixel(
                        sheet,
                        at.x + static_cast<int32_t>(x * scale + sx),
                        at.y + static_cast<int32_t>(y * scale + sy));
                    if (got.r != expected.r || got.g != expected.g ||
                        got.b != expected.b || got.a != expected.a) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Gives the document's first frame a dither anchored in export space, which is
// the only thing that reacts to where a cell sits.
bool makeItPatternSensitive(Document& doc) {
    std::vector<PaintLayer> layers;
    if (!adoptPaintLayers(doc, doc.sprite(), &layers) || layers.empty()) {
        return false;
    }
    DitherSettings dither;
    dither.pattern = ls::DitherPatternKind::Bayer4;
    dither.from = ls::Color{ 30, 40, 90, 255 };
    dither.to   = ls::Color{ 250, 180, 90, 255 };
    dither.anchor = ls::PatternAnchor::Local;
    if (!setLayerDithered(doc, layers.front(), dither)) {
        return false;
    }
    // The dither helper writes a coordinate space of its own; the sheet only
    // matters for Export, so say so explicitly.
    auto operations = doc.engine().getLayerOperations(layers.front().layer);
    if (operations.fail()) {
        return false;
    }
    for (const ls::OperationInfo& op : operations.value) {
        if (op.type == "FillDitherOp") {
            return doc.engine().setOperationParameter(
                op.id, "coordinateSpace",
                static_cast<int64_t>(ls::CoordinateSpace::Export)).ok();
        }
    }
    return false;
}

// --- the plan, which needs no engine at all -------------------------------

void testTheGridIsNearlySquare() {
    SheetSettings settings;
    SheetPlan plan;
    std::string error;

    REQUIRE(planSheet(6, 16, 16, settings, &plan, &error));
    CHECK(plan.columns == 3 && plan.rows == 2);
    CHECK(plan.width == 48 && plan.height == 32);

    REQUIRE(planSheet(1, 16, 16, settings, &plan, &error));
    CHECK(plan.columns == 1 && plan.rows == 1);

    // Seven cells do not divide evenly, so the last row is short and the image
    // is still whole rows -- a ragged edge is transparent, not missing.
    REQUIRE(planSheet(7, 16, 16, settings, &plan, &error));
    CHECK(plan.columns == 3 && plan.rows == 3);
    CHECK(plan.cells == 7);
}

void testStripsAndColumnsAndFixedWidths() {
    SheetPlan plan;
    std::string error;

    SheetSettings row;
    row.layout = SheetLayout::Row;
    REQUIRE(planSheet(5, 16, 8, row, &plan, &error));
    CHECK(plan.columns == 5 && plan.rows == 1);
    CHECK(plan.width == 80 && plan.height == 8);

    SheetSettings column;
    column.layout = SheetLayout::Column;
    REQUIRE(planSheet(5, 16, 8, column, &plan, &error));
    CHECK(plan.columns == 1 && plan.rows == 5);
    CHECK(plan.width == 16 && plan.height == 40);

    SheetSettings four;
    four.columns = 4;
    REQUIRE(planSheet(10, 16, 16, four, &plan, &error));
    CHECK(plan.columns == 4 && plan.rows == 3);

    // More columns than cells is one row, not a row with holes on the right.
    SheetSettings wide;
    wide.columns = 99;
    REQUIRE(planSheet(3, 16, 16, wide, &plan, &error));
    CHECK(plan.columns == 3 && plan.rows == 1);
}

void testScaleMultipliesTheCell() {
    SheetSettings settings;
    settings.scale = 4;
    SheetPlan plan;
    std::string error;

    REQUIRE(planSheet(4, 16, 16, settings, &plan, &error));
    CHECK(plan.cellWidth == 64 && plan.cellHeight == 64);
    CHECK(plan.width == 128 && plan.height == 128);
    CHECK(plan.positionOf(3).x == 64 && plan.positionOf(3).y == 64);
}

void testAnAbsurdSheetIsRefusedRatherThanAttempted() {
    SheetPlan plan;
    std::string error;

    SheetSettings huge;
    huge.scale = 64;
    // 512 frames of 128 at 64x is tens of gigabytes; the answer is a sentence,
    // not an allocation failure.
    CHECK(!planSheet(512, 128, 128, huge, &plan, &error));
    CHECK(!error.empty());

    SheetSettings strip;
    strip.layout = SheetLayout::Row;
    strip.scale = 16;
    CHECK(!planSheet(400, 128, 16, strip, &plan, &error));   // past 16384 wide

    SheetSettings settings;
    settings.scale = 0;
    CHECK(!planSheet(4, 16, 16, settings, &plan, &error));
    settings.scale = 999;
    CHECK(!planSheet(4, 16, 16, settings, &plan, &error));

    SheetSettings fine;
    CHECK(!planSheet(0, 16, 16, fine, &plan, &error));
    CHECK(!planSheet(4, 0, 16, fine, &plan, &error));
}

// --- the promise ----------------------------------------------------------

void testACellIsExactlyASingleFrameExport() {
    Document doc;
    REQUIRE(build(doc, 4));
    REQUIRE(makeItPatternSensitive(doc));

    const std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 4);

    std::vector<ls::SpriteId> sprites;
    for (const Frame& frame : frames) {
        sprites.push_back(frame.sprite);
    }

    SheetSettings settings;
    ls::RasterBuffer sheet;
    SheetPlan plan;
    std::string error;
    REQUIRE(composeSheet(doc, sprites, settings, &sheet, &plan, &error));
    REQUIRE(plan.columns == 2 && plan.rows == 2);

    for (size_t i = 0; i < sprites.size(); ++i) {
        const ls::RasterBuffer one = alone(doc, sprites[i]);
        REQUIRE(!one.empty());
        CHECK(cellMatches(sheet, plan.positionOf(static_cast<int>(i)), one, 1));
    }
}

// And at 4x, where the only correct scaling is duplication.
void testAScaledCellIsTheSamePixelsFourTimes() {
    Document doc;
    REQUIRE(build(doc, 2));
    REQUIRE(makeItPatternSensitive(doc));

    const std::vector<Frame> frames = readFrames(doc);
    std::vector<ls::SpriteId> sprites;
    for (const Frame& frame : frames) {
        sprites.push_back(frame.sprite);
    }

    SheetSettings settings;
    settings.scale = 4;
    ls::RasterBuffer sheet;
    SheetPlan plan;
    std::string error;
    REQUIRE(composeSheet(doc, sprites, settings, &sheet, &plan, &error));

    for (size_t i = 0; i < sprites.size(); ++i) {
        const ls::RasterBuffer one = alone(doc, sprites[i]);
        CHECK(cellMatches(sheet, plan.positionOf(static_cast<int>(i)), one, 4));
    }
}

// The option that deliberately breaks the promise above, and must therefore be
// shown to do something -- a setting that changes nothing is worse than none.
//
// The canvas here is 14 wide against a 4-wide tile on purpose. At 16 the cells
// land on multiples of the tile and the lattice happens to line up anyway, so a
// test at 16 would pass whether or not the export origin was ever passed
// through. 14 is the size at which the difference is real.
void testPatternAcrossTheSheetActuallyChangesIt() {
    Document doc;
    REQUIRE(doc.create("odd", 14, 14));
    ShapeParams params;
    params.from = { 1.f, 1.f };
    params.to   = { 12.f, 12.f };
    ShapeLayer shape;
    REQUIRE(createShapeLayer(doc, doc.sprite(), ShapeKind::Rectangle, params,
                             { 255, 160, 40, 255 }, &shape));
    REQUIRE(makeItPatternSensitive(doc));
    REQUIRE(duplicateFrame(doc, 0) == 1);

    std::vector<ls::SpriteId> sprites;
    for (const Frame& frame : readFrames(doc)) {
        sprites.push_back(frame.sprite);
    }
    REQUIRE(sprites.size() == 2);

    SheetPlan plan;
    std::string error;

    SheetSettings cellwise;
    cellwise.layout = SheetLayout::Row;      // both cells on one row
    ls::RasterBuffer perCell;
    REQUIRE(composeSheet(doc, sprites, cellwise, &perCell, &plan, &error));

    SheetSettings continuous = cellwise;
    continuous.patternAcrossSheet = true;
    ls::RasterBuffer across;
    REQUIRE(composeSheet(doc, sprites, continuous, &across, &plan, &error));

    CHECK(perCell.width == across.width && perCell.height == across.height);
    // The second cell sits at x = 14, which is not a whole number of tiles, so
    // pinning the lattice to the sheet genuinely moves its pixels.
    CHECK(perCell.pixels != across.pixels);

    // The first cell is at the origin either way, so it is unmoved -- which is
    // what says the difference is the export origin and not something else.
    const ls::RasterBuffer one = alone(doc, sprites.front());
    CHECK(cellMatches(perCell, plan.positionOf(0), one, 1, 14));
    CHECK(cellMatches(across,  plan.positionOf(0), one, 1, 14));

    // And the second cell is the one that differs: with the default it still
    // matches a lone export, and with the option it does not.
    CHECK(cellMatches(perCell, plan.positionOf(1), one, 1, 14));
    CHECK(!cellMatches(across, plan.positionOf(1), one, 1, 14));
}

// --- the manifest ---------------------------------------------------------

void testTheManifestDescribesEveryCell() {
    Document doc;
    REQUIRE(build(doc, 3));
    REQUIRE(setFrameDuration(doc, 1, 250));
    REQUIRE(setFrameName(doc, 1, "contact"));

    const std::vector<Frame> frames = readFrames(doc);
    const std::vector<int> steps{ 0, 1, 2, 1 };     // a cycle that repeats one

    SheetSettings settings;
    settings.columns = 2;
    SheetPlan plan;
    std::string error;
    REQUIRE(planSheet(static_cast<int>(steps.size()), kCanvas, kCanvas,
                      settings, &plan, &error));

    const std::string json =
        sheetManifest(plan, frames, steps, readCycles(doc, 3), "hero.png", 1);

    CHECK(json.find("\"format\": \"sprits-sheet/1\"") != std::string::npos);
    CHECK(json.find("\"image\": \"hero.png\"") != std::string::npos);
    CHECK(json.find("\"columns\": 2") != std::string::npos);
    CHECK(json.find("\"durationMs\": 250") != std::string::npos);
    CHECK(json.find("\"name\": \"contact\"") != std::string::npos);

    // Four cells, because a repeated step is a repeated cell.
    size_t cells = 0;
    for (size_t at = json.find("\"frame\":"); at != std::string::npos;
         at = json.find("\"frame\":", at + 1)) {
        ++cells;
    }
    CHECK(cells == 4);

    // The fourth cell is the second row, second column.
    CHECK(json.find("\"x\": 16, \"y\": 16") != std::string::npos);
}

// A name is whatever somebody typed, and a manifest a quotation mark can break
// is one no consumer can trust.
void testAHostileNameCannotBreakTheManifest() {
    Document doc;
    REQUIRE(build(doc, 1));
    REQUIRE(setFrameName(doc, 0, "he said \"go\"\\then\nstopped"));

    const std::vector<Frame> frames = readFrames(doc);
    SheetPlan plan;
    std::string error;
    SheetSettings settings;
    REQUIRE(planSheet(1, kCanvas, kCanvas, settings, &plan, &error));

    const std::string json = sheetManifest(plan, frames, { 0 }, {}, "a\"b.png", 1);

    // Every quote in the payload is escaped, so the number of unescaped quotes
    // stays even and the structure holds.
    int unescaped = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) {
            ++unescaped;
        }
    }
    CHECK(unescaped % 2 == 0);
    CHECK(json.find('\n') != std::string::npos);          // it is still formatted
    CHECK(json.find("\\n") != std::string::npos);         // and the newline escaped
    CHECK(json.find("\\\"go\\\"") != std::string::npos);
}

// --- writing --------------------------------------------------------------

void testASheetIsWrittenWithItsDescription() {
    Document doc;
    REQUIRE(build(doc, 3));

    const std::vector<Frame> frames = readFrames(doc);
    const std::string path = "sheet_test_out.png";

    SheetSettings settings;
    settings.scale = 2;
    std::string error;
    REQUIRE(exportSheetToPng(doc, frames, { 0, 1, 2 }, {}, path, settings, &error));

    CHECK(fileExists(path));
    // Beside the image under the same name, which is where a consumer looks --
    // not "sheet_test_out.png.json".
    CHECK(fileExists("sheet_test_out.json"));
    CHECK(!fileExists("sheet_test_out.png.json"));

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(path, bytes, &error));
    CHECK(bytes.size() > 8);
    CHECK(bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');

    deleteFile(path);
    deleteFile("sheet_test_out.json");
}

void testAStepNamingNothingIsRefused() {
    Document doc;
    REQUIRE(build(doc, 2));

    const std::vector<Frame> frames = readFrames(doc);
    std::string error;
    SheetSettings settings;
    settings.writeManifest = false;

    CHECK(!exportSheetToPng(doc, frames, { 0, 7 }, {}, "sheet_bad.png",
                            settings, &error));
    CHECK(!error.empty());
    // Nothing was written, rather than a sheet with a hole in it.
    CHECK(!fileExists("sheet_bad.png"));

    CHECK(!exportSheetToPng(doc, frames, {}, {}, "sheet_bad.png", settings, &error));
}

} // namespace

// Padding: a border round the sheet and spacing between cells, in the
// finished image's pixels, counted into its size and every cell's position.
void testBorderAndSpacingMoveTheCells() {
    SheetSettings settings;
    settings.columns = 2;
    settings.border = 3;
    settings.spacing = 2;
    SheetPlan plan;
    std::string error;
    REQUIRE(planSheet(4, kCanvas, kCanvas, settings, &plan, &error));
    CHECK(plan.width == 3 + 16 + 2 + 16 + 3);
    CHECK(plan.height == plan.width);
    CHECK(plan.positionOf(0).x == 3 && plan.positionOf(0).y == 3);
    CHECK(plan.positionOf(3).x == 3 + 16 + 2 && plan.positionOf(3).y == 3 + 16 + 2);
}

// The Aseprite layouts: every cell with its rectangle and hold, a cycle that
// is a run of cells as a frame tag in its direction, one that is not left
// out, and Fast named as the app.
void testTheAsepriteLayouts() {
    Document doc;
    REQUIRE(build(doc, 3));
    REQUIRE(setFrameDuration(doc, 2, 250));
    const std::vector<Frame> frames = readFrames(doc);
    Cycle run;
    run.name = "walk";
    run.frames = { 1, 2 };
    run.loop = LoopMode::PingPong;
    Cycle scattered;
    scattered.name = "odd";
    scattered.frames = { 2, 0 };
    const std::vector<int> steps{ 0, 1, 2 };
    SheetSettings settings;
    settings.layout = SheetLayout::Row;
    SheetPlan plan;
    std::string error;
    REQUIRE(planSheet(3, kCanvas, kCanvas, settings, &plan, &error));

    const std::string hash =
        asepriteManifest(plan, frames, steps, { run, scattered }, "hero.png", 1, true);
    CHECK(hash.find("\"hero 2\": {") != std::string::npos);
    CHECK(hash.find("\"frame\": { \"x\": 32, \"y\": 0, \"w\": 16, \"h\": 16 }") !=
          std::string::npos);
    CHECK(hash.find("\"duration\": 250") != std::string::npos);
    CHECK(hash.find("\"name\": \"walk\", \"from\": 1, \"to\": 2, \"direction\": \"pingpong\"") !=
          std::string::npos);
    CHECK(hash.find("\"odd\"") == std::string::npos);
    CHECK(hash.find("\"app\": \"Sprit's'fast\"") != std::string::npos);
    CHECK(hash.find("\"size\": { \"w\": 48, \"h\": 16 }") != std::string::npos);

    const std::string array =
        asepriteManifest(plan, frames, steps, {}, "hero.png", 1, false);
    CHECK(array.find("{ \"frames\": [") != std::string::npos);
    CHECK(array.find("\"filename\": \"hero 0\"") != std::string::npos);
    CHECK(array.find("\"frameTags\": []") != std::string::npos);
}

int main() {
    testTheGridIsNearlySquare();
    testStripsAndColumnsAndFixedWidths();
    testScaleMultipliesTheCell();
    testAnAbsurdSheetIsRefusedRatherThanAttempted();
    testACellIsExactlyASingleFrameExport();
    testAScaledCellIsTheSamePixelsFourTimes();
    testPatternAcrossTheSheetActuallyChangesIt();
    testTheManifestDescribesEveryCell();
    testAHostileNameCannotBreakTheManifest();
    testASheetIsWrittenWithItsDescription();
    testAStepNamingNothingIsRefused();
    testBorderAndSpacingMoveTheCells();
    testTheAsepriteLayouts();

    if (failures == 0) {
        std::printf("sheet: all checks passed\n");
        return 0;
    }
    std::printf("sheet: %d check(s) failed\n", failures);
    return 1;
}
