// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// recovery_tests.cpp — autosave, and the rule it exists to keep.
//
// The rule: a recovery copy is never the file being edited. Everything here
// is a consequence of it -- the document does not notice a copy being taken,
// the copy carries the whole document, and it is gone the moment the work is
// safe on disk.
//
// These tests touch the user's real recovery folder, because that is where
// the code looks; each cleans up after itself, and none of them can collide
// with a running program because a session claims a name nothing else holds.

#include "app/document.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/library.h"
#include "app/paint.h"
#include "app/recovery.h"
#include "app/reference.h"

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

bool build(Document& doc) {
    if (!doc.create("work", kCanvas, kCanvas)) { return false; }
    PaintLayer layer;
    if (!createPaintLayer(doc, doc.sprite(), "Body", ls::Color{ 200, 90, 60, 255 }, &layer)) {
        return false;
    }
    doc.beginAction("draw");
    const bool ok = paintPixels(doc, layer, {{ 2, 2 }, { 3, 2 }});
    doc.endAction();
    return ok;
}

ls::Color at(Document& doc, int x, int y) {
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = kCanvas;
    profile.outputHeight = kCanvas;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(doc.sprite(), profile);
    if (compiled.fail()) { return ls::Color{ 0, 0, 0, 0 }; }
    return ls::readPixel(compiled.value.raster, x, y);
}

// --- the rule -------------------------------------------------------------

void testACopyIsNotTheFile() {
    Document doc;
    REQUIRE(build(doc));

    const std::string real = "fast_recovery_real.lsprite";
    std::string error;
    REQUIRE(doc.save(real, &error));
    CHECK(doc.path() == real);
    CHECK(!doc.modified());

    // Draw again, so there is something a crash would cost.
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(doc, doc.sprite(), &layers));
    doc.beginAction("more");
    REQUIRE(paintPixels(doc, layers.front(), {{ 9, 9 }}));
    doc.endAction();
    CHECK(doc.modified());

    // A copy leaves the document exactly as it was: same path, still unsaved.
    const std::string copy = "fast_recovery_copy.lsprite";
    REQUIRE(doc.saveCopy(copy, &error));
    CHECK(doc.path() == real);
    CHECK(doc.modified());

    // And the copy is the whole document, including what was never saved.
    Document reopened;
    REQUIRE(reopened.open(copy, &error));
    CHECK(at(reopened, 9, 9).r == 200);
    CHECK(at(reopened, 2, 2).r == 200);

    // The file the person owns still holds only what they saved.
    Document onDisk;
    REQUIRE(onDisk.open(real, &error));
    CHECK(at(onDisk, 2, 2).r == 200);
    CHECK(at(onDisk, 9, 9).a == 0);

    deleteFile(real);
    deleteFile(copy);
}

// --- the session ----------------------------------------------------------

void testASessionWritesOnItsIntervalAndOnlyWhenItShould() {
    Document doc;
    REQUIRE(build(doc));

    RecoverySession session;
    REQUIRE(session.begin());
    CHECK(session.active());
    CHECK(!fileExists(session.path()));       // nothing written yet
    session.setIntervalSeconds(60);

    uint64_t clock = 1000;
    // The first tick starts the clock rather than writing: a copy one second
    // into a session is a copy of nothing.
    CHECK(!session.tick(doc, clock, false));
    CHECK(!fileExists(session.path()));

    // Not yet due.
    CHECK(!session.tick(doc, clock + 59, false));
    // Due, but mid-drag: a copy taken during a stroke would catch it half
    // committed, and the write would land as a hitch.
    CHECK(!session.tick(doc, clock + 61, true));
    CHECK(!fileExists(session.path()));
    // Due, and idle.
    CHECK(session.tick(doc, clock + 61, false));
    CHECK(fileExists(session.path()));
    CHECK(session.haveCopy());
    CHECK(session.lastWriteSeconds() == clock + 61);
    // The copy carries a picture of the work, for the question after a crash.
    std::vector<uint8_t> picture;
    CHECK(readThumbnail(session.path(), &picture));
    CHECK(!picture.empty());

    // The document did not notice.
    CHECK(doc.path().empty());
    CHECK(doc.modified());

    // Not due again straight away.
    CHECK(!session.tick(doc, clock + 62, false));

    // Nothing to save means nothing written, however long it has been.
    session.clear();
    doc.markUnmodified();
    CHECK(!session.tick(doc, clock + 5000, false));
    CHECK(!fileExists(session.path()));

    session.clear();
    CHECK(!fileExists(session.path()));
}

// The clock handed in is milliseconds since the program started, divided
// down, so it genuinely is 0 for the first second. A session that treated
// "never written" as lastWriteSeconds_ == 0 re-armed itself on every frame
// from a clock that started at 0 and never wrote anything at all.
void testASessionThatStartsAtZeroStillWrites() {
    Document doc;
    REQUIRE(build(doc));

    RecoverySession session;
    REQUIRE(session.begin());
    session.setIntervalSeconds(15);

    CHECK(!session.tick(doc, 0, false));      // starts the clock
    CHECK(!session.tick(doc, 14, false));     // not due
    CHECK(session.tick(doc, 15, false));      // due, and it writes
    CHECK(fileExists(session.path()));

    session.clear();
    CHECK(!fileExists(session.path()));
}

void testTheIntervalIsBounded() {
    RecoverySession session;
    session.setIntervalSeconds(1);
    CHECK(session.intervalSeconds() == kMinAutosaveSeconds);
    session.setIntervalSeconds(999999);
    CHECK(session.intervalSeconds() == kMaxAutosaveSeconds);
    session.setIntervalSeconds(300);
    CHECK(session.intervalSeconds() == 300);

    // A session that never began writes nothing rather than guessing a path.
    Document doc;
    REQUIRE(build(doc));
    RecoverySession never;
    CHECK(!never.active());
    CHECK(!never.tick(doc, 10000, false));
    CHECK(!never.writeNow(doc, 10000));
}

// --- finding it afterwards -------------------------------------------------

void testAbandonedWorkIsFoundAndCanBeOpened() {
    // What is already there is not ours and must survive this test.
    const size_t before = findRecoveredWork().size();

    Document doc;
    REQUIRE(build(doc));
    const std::string real = "fast_recovery_named.lsprite";
    std::string error;
    REQUIRE(doc.save(real, &error));
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(doc, doc.sprite(), &layers));
    doc.beginAction("unsaved work");
    REQUIRE(paintPixels(doc, layers.front(), {{ 11, 11 }}));
    doc.endAction();

    RecoverySession session;
    REQUIRE(session.begin());
    REQUIRE(session.writeNow(doc, 5000));

    // The program dies here: nothing calls clear().
    std::vector<RecoveredWork> found = findRecoveredWork();
    CHECK(found.size() == before + 1);

    const RecoveredWork* ours = nullptr;
    for (const RecoveredWork& work : found) {
        if (work.path == session.path()) { ours = &work; }
    }
    REQUIRE(ours != nullptr);
    // It knows where it was going, so a recovery can offer to save it back.
    CHECK(ours->originalPath == real);
    CHECK(ours->name == "fast_recovery_named.lsprite");

    // And it holds the work that was never saved.
    Document recovered;
    REQUIRE(recovered.open(ours->path, &error));
    CHECK(at(recovered, 11, 11).r == 200);

    CHECK(discardRecoveredWork(*ours));
    CHECK(findRecoveredWork().size() == before);
    deleteFile(real);
}

void testAnUntitledDocumentIsRecoverableToo() {
    const size_t before = findRecoveredWork().size();

    Document doc;
    REQUIRE(build(doc));          // never saved: no path at all
    CHECK(doc.path().empty());

    RecoverySession session;
    REQUIRE(session.begin());
    REQUIRE(session.writeNow(doc, 6000));

    std::vector<RecoveredWork> found = findRecoveredWork();
    const RecoveredWork* ours = nullptr;
    for (const RecoveredWork& work : found) {
        if (work.path == session.path()) { ours = &work; }
    }
    REQUIRE(ours != nullptr);
    CHECK(ours->originalPath.empty());
    CHECK(ours->name == "untitled");      // something to show in a prompt

    session.clear();
    CHECK(findRecoveredWork().size() == before);
}

// A reference lives in the package, so it has to be in the copy too --
// recovering a drawing without the thing it was traced from is half a
// recovery.
void testTheCopyCarriesEverything() {
    Document doc;
    REQUIRE(build(doc));

    ls::RasterBuffer picture = ls::makeRaster(6, 4);
    for (uint32_t y = 0; y < picture.height; ++y) {
        for (uint32_t x = 0; x < picture.width; ++x) {
            uint8_t* pixel = picture.row(y) + static_cast<size_t>(x) * 4u;
            pixel[1] = 180;
            pixel[3] = 255;
        }
    }
    std::vector<uint8_t> png;
    std::string error;
    REQUIRE(encodeImageAsPng(picture, &png, &error));
    Reference made;
    REQUIRE(addReference(doc, "pose.png", png, &made, &error));

    RecoverySession session;
    REQUIRE(session.begin());
    REQUIRE(session.writeNow(doc, 7000));

    Document recovered;
    REQUIRE(recovered.open(session.path(), &error));
    const std::vector<Reference> references = readReferences(recovered);
    CHECK(references.size() == 1);
    if (!references.empty()) {
        CHECK(references[0].name == "pose");
        CHECK(referenceBytes(recovered, references[0]) != nullptr);
    }

    session.clear();
}

// ---- what is offered after a crash: the last work only, and the rest put away ----
//
// These use a folder of their own under the working directory, never the person's
// recovery folder.

const char* kScratch = "recovery_archive_test";

void clearScratch() {
    for (const char* sub : { "", "/older" }) {
        for (const DirectoryEntry& entry : listDirectory(std::string(kScratch) + sub)) {
            if (!entry.directory) {
                deleteFile(entry.path);
            }
        }
    }
}

// A file standing in for a recovery copy: only its name and its bytes matter to the
// code under test, except where a test says it is a real document.
void fakeCopy(int session, const std::string& originalPath = std::string()) {
    const std::string stem = std::string(kScratch) + "/session-" + std::to_string(session) + "-0";
    std::string error;
    const std::vector<uint8_t> bytes { 'x', 'y' };
    writeFileAtomic(stem + ".lsprite", bytes, &error);
    if (!originalPath.empty()) {
        writeFileAtomic(stem + ".from",
                        std::vector<uint8_t>(originalPath.begin(), originalPath.end()), &error);
    }
}

void testOnlyTheLatestWorkIsOfferedAndTheRestIsPutAway() {
    createDirectory(kScratch);
    clearScratch();
    fakeCopy(100);
    fakeCopy(300, "C:/art/hero walk.lsprite");
    fakeCopy(200);

    std::vector<RecoveredWork> found = findRecoveredWorkIn(kScratch);
    REQUIRE(found.size() == 3);
    // Written in the same second, so the names decide: the newest session first.
    CHECK(found[0].path.find("session-300-0") != std::string::npos);
    CHECK(found[1].path.find("session-200-0") != std::string::npos);
    CHECK(found[2].path.find("session-100-0") != std::string::npos);
    CHECK(found[0].name == "hero walk.lsprite");
    CHECK(found[0].originalPath == "C:/art/hero walk.lsprite");
    CHECK(found[1].name == "untitled");

    // The first is left; the others go to older/, neither offered nor lost.
    CHECK(archiveOlderRecoveredWork(found) == 2);
    CHECK(directoryExists(archivedRecoveryDirectory(found)));
    CHECK(fileName(archivedRecoveryDirectory(found)) == "older");
    const std::vector<RecoveredWork> after = findRecoveredWorkIn(kScratch);
    REQUIRE(after.size() == 1);
    CHECK(after[0].path == found[0].path);
    CHECK(fileExists(std::string(kScratch) + "/older/session-200-0.lsprite"));
    CHECK(fileExists(std::string(kScratch) + "/older/session-100-0.lsprite"));

    // One copy alone has nothing older to move.
    CHECK(archiveOlderRecoveredWork(after) == 0);
    clearScratch();
}

void testTheArchiveKeepsTheNewestTen() {
    createDirectory(kScratch);
    clearScratch();
    for (int session = 1; session <= 14; ++session) {
        fakeCopy(1000 + session);
    }
    const std::vector<RecoveredWork> found = findRecoveredWorkIn(kScratch);
    REQUIRE(found.size() == 14);
    CHECK(archiveOlderRecoveredWork(found) == 13);
    // The archive holds ten, the newest ten of the thirteen moved (1004..1013); the
    // three oldest were let go, and 1014, the one offered, was never moved.
    int archived = 0;
    for (const DirectoryEntry& entry : listDirectory(std::string(kScratch) + "/older")) {
        if (!entry.directory && hasExtension(entry.name, ".lsprite")) {
            ++archived;
        }
    }
    CHECK(archived == 10);
    CHECK(fileExists(std::string(kScratch) + "/older/session-1013-0.lsprite"));
    CHECK(fileExists(std::string(kScratch) + "/older/session-1004-0.lsprite"));
    CHECK(!fileExists(std::string(kScratch) + "/older/session-1003-0.lsprite"));
    CHECK(fileExists(std::string(kScratch) + "/session-1014-0.lsprite"));
    clearScratch();
}

void testACopyIsSummarisedFromItself() {
    createDirectory(kScratch);
    clearScratch();
    Document doc;
    REQUIRE(build(doc));
    std::string error;
    const std::string real = std::string(kScratch) + "/session-7-0.lsprite";
    REQUIRE(doc.saveCopy(real, &error));
    fakeCopy(8);

    RecoveredWork good;
    good.path = real;
    const RecoverySummary summary = summariseRecoveredWork(good);
    CHECK(summary.readable);
    CHECK(summary.width == kCanvas);
    CHECK(summary.height == kCanvas);
    CHECK(summary.frames >= 1);

    // A file that is not a document is reported as unreadable, not as nothing.
    RecoveredWork bad;
    bad.path = std::string(kScratch) + "/session-8-0.lsprite";
    CHECK(!summariseRecoveredWork(bad).readable);
    RecoveredWork none;
    CHECK(!summariseRecoveredWork(none).readable);
    clearScratch();
}

} // namespace

int main() {
    testOnlyTheLatestWorkIsOfferedAndTheRestIsPutAway();
    testTheArchiveKeepsTheNewestTen();
    testACopyIsSummarisedFromItself();
    testACopyIsNotTheFile();
    testASessionWritesOnItsIntervalAndOnlyWhenItShould();
    testASessionThatStartsAtZeroStillWrites();
    testTheIntervalIsBounded();
    testAbandonedWorkIsFoundAndCanBeOpened();
    testAnUntitledDocumentIsRecoverableToo();
    testTheCopyCarriesEverything();

    if (failures == 0) {
        std::printf("fast_recovery: all checks passed\n");
        return 0;
    }
    std::printf("fast_recovery: %d check(s) failed\n", failures);
    return 1;
}
