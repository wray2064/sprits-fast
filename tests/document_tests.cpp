// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// document_tests.cpp — the editor spine, without a window.
//
// Undo, redo, cancelled actions, and the promise that saving a file does not
// destroy data belonging to another application. All of this is testable before
// a single pixel is drawn on screen, which is why it lives in fast_core rather
// than in the interface.

#include "app/document.h"

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

int opaqueCount(LSContext& engine, SpriteId sprite, uint32_t size) {
    auto compiled = engine.compileSprite(sprite, profile(size));
    if (compiled.fail()) {
        return -1;
    }
    int count = 0;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            count += readPixel(compiled.value.raster,
                               static_cast<int32_t>(x), static_cast<int32_t>(y)).a != 0;
        }
    }
    return count;
}

// A document with one sprite and one layer, ready to be drawn on.
struct Scene {
    fast::Document doc;
    SpriteId sprite;
    LayerId  layer;

    bool build(uint32_t canvas = 16) {
        if (!doc.create("test", canvas, canvas)) { return false; }
        sprite = doc.sprite();
        layer = doc.engine().createLayer(sprite, {"main"}).value;
        return sprite.valid() && layer.valid();
    }

    // A filled square, as one undoable action.
    void drawSquare(Vec2f origin, float side, const std::string& label) {
        doc.beginAction(label);
        LSContext& engine = doc.engine();
        const GeometryId rect =
            engine.createRect(doc.id(), {origin, side, side, 0.f}).value;
        FillSolidOp fill;
        fill.targetRegion = engine.createRegionFromGeometry(rect).value;
        fill.fallbackColor = {200, 100, 60, 255};
        engine.addOperation(layer, fill);
        doc.endAction();
    }
};

// ---------------------------------------------------------------------------

void testNewDocumentIsClean() {
    fast::Document doc;
    REQUIRE(doc.create("untitled", 32, 32));
    CHECK(!doc.modified());
    CHECK(!doc.canUndo());
    CHECK(!doc.canRedo());
    CHECK(doc.path().empty());
    CHECK(doc.undoLabel().empty());
}

void testUndoAndRedoRestoreThePicture() {
    Scene scene;
    REQUIRE(scene.build());

    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 0);

    scene.drawSquare({4.f, 4.f}, 8.f, "Draw square");
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 64);
    CHECK(scene.doc.canUndo());
    CHECK(scene.doc.undoLabel() == "Draw square");
    CHECK(scene.doc.modified());

    CHECK(scene.doc.undo());
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 0);
    CHECK(!scene.doc.canUndo());
    CHECK(scene.doc.canRedo());
    CHECK(scene.doc.redoLabel() == "Draw square");

    CHECK(scene.doc.redo());
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 64);
    CHECK(!scene.doc.canRedo());

    // Nothing to step past at either end, and asking says so rather than
    // corrupting the history.
    CHECK(scene.doc.undo());
    CHECK(!scene.doc.undo());
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 0);
}

// The handles the interface is holding have to survive an undo. This is the
// reason the engine restores ids exactly instead of renumbering.
void testHandlesSurviveUndo() {
    Scene scene;
    REQUIRE(scene.build());
    scene.drawSquare({4.f, 4.f}, 8.f, "Draw");

    CHECK(scene.doc.undo());
    CHECK(scene.doc.engine().getSpriteInfo(scene.sprite).ok());
    CHECK(scene.doc.engine().getLayerInfo(scene.layer).ok());

    CHECK(scene.doc.redo());
    CHECK(scene.doc.engine().getLayerInfo(scene.layer).ok());
}

void testNewActionDiscardsTheRedoBranch() {
    Scene scene;
    REQUIRE(scene.build());

    scene.drawSquare({2.f, 2.f}, 4.f, "First");
    scene.drawSquare({8.f, 8.f}, 4.f, "Second");
    CHECK(scene.doc.undo());
    CHECK(scene.doc.canRedo());

    scene.drawSquare({2.f, 8.f}, 4.f, "Third");
    CHECK(!scene.doc.canRedo());
    CHECK(scene.doc.undoLabel() == "Third");
}

// A tool cancelled mid-drag must leave neither a change nor a history entry.
void testAbandonedActionLeavesNoTrace() {
    Scene scene;
    REQUIRE(scene.build());
    scene.drawSquare({4.f, 4.f}, 8.f, "Draw");
    const int before = opaqueCount(scene.doc.engine(), scene.sprite, 16);

    scene.doc.beginAction("Cancelled");
    {
        LSContext& engine = scene.doc.engine();
        const GeometryId rect = engine.createRect(scene.doc.id(),
                                                  {{0.f, 0.f}, 16.f, 16.f, 0.f}).value;
        FillSolidOp fill;
        fill.targetRegion = engine.createRegionFromGeometry(rect).value;
        fill.fallbackColor = {0, 0, 0, 255};
        engine.addOperation(scene.layer, fill);
    }
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 256);

    scene.doc.abandonAction();
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == before);
    CHECK(scene.doc.undoLabel() == "Draw");
}

// A compound tool that brackets its own helpers should produce one entry.
void testNestedActionsCollapseToOne() {
    Scene scene;
    REQUIRE(scene.build());

    scene.doc.beginAction("Outer");
    scene.drawSquare({2.f, 2.f}, 4.f, "Inner one");
    scene.drawSquare({8.f, 8.f}, 4.f, "Inner two");
    scene.doc.endAction();

    CHECK(scene.doc.undoLabel() == "Outer");
    CHECK(scene.doc.undo());
    CHECK(opaqueCount(scene.doc.engine(), scene.sprite, 16) == 0);
    CHECK(!scene.doc.canUndo());
}

void testHistoryLimitIsHonoured() {
    Scene scene;
    REQUIRE(scene.build());
    scene.doc.setHistoryLimit(3);

    for (int i = 0; i < 6; ++i) {
        scene.drawSquare({static_cast<float>(i), 0.f}, 1.f, "Step");
    }

    int steps = 0;
    while (scene.doc.undo()) { ++steps; }
    CHECK(steps == 3);
}

// Save, reopen, and check the picture rather than the structure.
void testSaveAndReopen() {
    Scene scene;
    REQUIRE(scene.build());
    scene.drawSquare({4.f, 4.f}, 8.f, "Draw");
    scene.doc.setUiState("{\"zoom\":8}");

    const std::string path = "fast_document_test.lsprite";
    std::string error;
    REQUIRE(scene.doc.save(path, &error));
    CHECK(!scene.doc.modified());
    CHECK(scene.doc.path() == path);

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));
    CHECK(reopened.uiState() == "{\"zoom\":8}");
    CHECK(!reopened.modified());
    CHECK(!reopened.canUndo());

    auto info = reopened.engine().getDocumentInfo(reopened.id());
    REQUIRE(info.ok());
    REQUIRE(info.value.sprites.size() == 1);
    CHECK(opaqueCount(reopened.engine(), info.value.sprites.front(), 16) == 64);
}

// Fast must not be the reason another application's data disappears from a file
// they share. Entries it does not understand are carried through untouched.
void testForeignEntriesSurviveARoundTrip() {
    Scene scene;
    REQUIRE(scene.build());
    scene.drawSquare({4.f, 4.f}, 8.f, "Draw");

    // Write a file as though another application had made it.
    std::vector<PackageEntry> theirs;
    PackageEntry note;
    note.name = "pract/arranger.json";
    note.contentType = "application/json";
    const std::string payload = "{\"cell\":[4,2]}";
    note.data.assign(payload.begin(), payload.end());
    theirs.push_back(note);

    auto package = scene.doc.engine().writePackage(scene.doc.id(), theirs);
    REQUIRE(package.ok());

    const std::string path = "fast_foreign_test.lsprite";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(package.value.bytes.data()),
                  static_cast<std::streamsize>(package.value.bytes.size()));
    }

    // Fast opens it, knows nothing about that entry, and saves.
    fast::Document doc;
    std::string error;
    REQUIRE(doc.open(path, &error));
    CHECK(doc.foreignEntryCount() == 1);

    doc.setUiState("{\"zoom\":4}");
    REQUIRE(doc.save(path, &error));

    // The other application's data is still there.
    fast::Document third;
    REQUIRE(third.open(path, &error));
    CHECK(third.foreignEntryCount() == 1);
    CHECK(third.uiState() == "{\"zoom\":4}");
}

void testOpeningRubbishFailsCleanly() {
    fast::Document doc;
    REQUIRE(doc.create("untitled", 8, 8));

    std::string error;
    CHECK(!doc.open("no_such_file_here.lsprite", &error));
    CHECK(!error.empty());

    // A file that exists but is not a package.
    const std::string path = "fast_not_a_package.lsprite";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "this is not a zip archive";
    }
    error.clear();
    CHECK(!doc.open(path, &error));
    CHECK(!error.empty());

    // The document it was already holding is untouched.
    CHECK(doc.id().valid());
}

void testSaveInPlaceNeedsAPath() {
    fast::Document doc;
    REQUIRE(doc.create("untitled", 8, 8));

    std::string error;
    CHECK(!doc.saveInPlace(&error));
    CHECK(!error.empty());

    REQUIRE(doc.save("fast_in_place_test.lsprite", &error));
    CHECK(doc.saveInPlace(&error));
}

} // namespace

// The history as a list: what undo would take back, oldest first, and what
// redo would bring back, nearest first.
void testHistoryReadsAsAList() {
    fast::Document doc;
    if (!doc.create("history", 8, 8)) { CHECK(false); return; }
    doc.clearHistory();
    for (const char* label : { "one", "two", "three" }) {
        doc.beginAction(label);
        doc.engine().createLayer(doc.sprite(), { label });
        doc.endAction();
    }
    CHECK((doc.undoLabels() == std::vector<std::string>{ "one", "two", "three" }));
    CHECK(doc.undo() && doc.undo());
    CHECK((doc.undoLabels() == std::vector<std::string>{ "one" }));
    CHECK((doc.redoLabels() == std::vector<std::string>{ "two", "three" }));
}

int main() {
    testHistoryReadsAsAList();
    testNewDocumentIsClean();
    testUndoAndRedoRestoreThePicture();
    testHandlesSurviveUndo();
    testNewActionDiscardsTheRedoBranch();
    testAbandonedActionLeavesNoTrace();
    testNestedActionsCollapseToOne();
    testHistoryLimitIsHonoured();
    testSaveAndReopen();
    testForeignEntriesSurviveARoundTrip();
    testOpeningRubbishFailsCleanly();
    testSaveInPlaceNeedsAPath();

    if (failures == 0) {
        std::printf("fast_document: all checks passed\n");
        return 0;
    }
    std::printf("fast_document: %d check(s) failed\n", failures);
    return 1;
}
