// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// animation_tests.cpp — what the frame model promises.
//
// Two things are worth stating as things that can fail. The first is that a
// duplicated frame is a frame rather than a second view of the one before it,
// which is the whole reason the engine's clone had to be fixed. The second is
// that a cycle names pictures rather than positions: reorder the frames and the
// cycle has to still play the same pictures in the same order, which is the bug
// every timeline written in a hurry has.

#include "app/animation.h"
#include "app/document.h"
#include "app/paint.h"
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

// A document with one frame that draws a square, so frames can be told apart by
// what they look like rather than by an id.
bool build(Document& doc) {
    if (!doc.create("walk", 16, 16)) {
        return false;
    }
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return false;
    }
    ShapeParams params;
    params.from = { 2.f, 2.f };
    params.to   = { 8.f, 8.f };
    ShapeLayer shape;
    if (!createShapeLayer(doc, info.value.sprites.front(), ShapeKind::Rectangle,
                          params, { 255, 160, 40, 255 }, &shape)) {
        return false;
    }
    doc.markUnmodified();
    return true;
}

std::vector<uint8_t> pixelsOf(Document& doc, ls::SpriteId sprite) {
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = 16;
    profile.outputHeight = 16;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(sprite, profile);
    return compiled.ok() ? compiled.value.raster.pixels : std::vector<uint8_t>{};
}

// Moves whatever the frame's first shape draws, so the frame becomes visibly
// different from its neighbours.
bool nudge(Document& doc, const Frame& frame, float x) {
    auto info = doc.engine().getSpriteInfo(frame.sprite);
    if (info.fail() || info.value.layers.empty()) {
        return false;
    }
    std::vector<PaintLayer> layers;
    if (!adoptPaintLayers(doc, frame.sprite, &layers) || layers.empty()) {
        return false;
    }
    ShapeLayer shape;
    if (!shapeOfLayer(doc, layers.front(), &shape)) {
        return false;
    }
    ShapeParams params;
    params.from = { x, 2.f };
    params.to   = { x + 6.f, 8.f };
    return updateShape(doc, shape, params);
}

// --------------------------------------------------------------------------

void testANewDocumentIsOneFrame() {
    Document doc;
    REQUIRE(build(doc));

    const std::vector<Frame> frames = readFrames(doc);
    CHECK(frames.size() == 1);
    CHECK(frames.front().durationMs == kDefaultFrameMs);
    CHECK(readCycles(doc, 1).empty());
}

// The one the engine fix exists for: a duplicate must be its own drawing.
void testADuplicatedFrameIsItsOwn() {
    Document doc;
    REQUIRE(build(doc));

    REQUIRE(duplicateFrame(doc, 0) == 1);
    std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 2);

    const std::vector<uint8_t> before = pixelsOf(doc, frames[0].sprite);
    REQUIRE(!before.empty());
    CHECK(pixelsOf(doc, frames[1].sprite) == before);   // a copy starts identical

    REQUIRE(nudge(doc, frames[1], 8.f));
    CHECK(pixelsOf(doc, frames[0].sprite) == before);   // the original held still
    CHECK(pixelsOf(doc, frames[1].sprite) != before);
}

void testAFrameLandsAfterTheOneItCameFrom() {
    Document doc;
    REQUIRE(build(doc));

    REQUIRE(duplicateFrame(doc, 0) == 1);
    REQUIRE(duplicateFrame(doc, 0) == 1);   // inserted between, not appended

    const std::vector<Frame> frames = readFrames(doc);
    CHECK(frames.size() == 3);
}

void testTheLastFrameCannotBeDeleted() {
    Document doc;
    REQUIRE(build(doc));

    CHECK(!deleteFrame(doc, 0));
    CHECK(readFrames(doc).size() == 1);

    REQUIRE(duplicateFrame(doc, 0) == 1);
    CHECK(deleteFrame(doc, 0));
    CHECK(readFrames(doc).size() == 1);
}

void testAddingAFrameIsUndoable() {
    Document doc;
    REQUIRE(build(doc));

    REQUIRE(duplicateFrame(doc, 0) == 1);
    REQUIRE(readFrames(doc).size() == 2);
    CHECK(doc.modified());

    REQUIRE(doc.undo());
    CHECK(readFrames(doc).size() == 1);

    REQUIRE(doc.redo());
    CHECK(readFrames(doc).size() == 2);
}

void testADurationRidesWithTheDocument() {
    Document doc;
    REQUIRE(build(doc));

    REQUIRE(setFrameDuration(doc, 0, 250));
    CHECK(readFrames(doc).front().durationMs == 250);

    // Undo restores engine state wholesale, and the duration is engine
    // metadata, so it comes back with everything else.
    REQUIRE(doc.undo());
    CHECK(readFrames(doc).front().durationMs == kDefaultFrameMs);
}

void testADurationIsClamped() {
    Document doc;
    REQUIRE(build(doc));

    REQUIRE(setFrameDuration(doc, 0, 0));
    CHECK(readFrames(doc).front().durationMs == kMinFrameMs);

    REQUIRE(setFrameDuration(doc, 0, -5000));
    CHECK(readFrames(doc).front().durationMs == kMinFrameMs);

    REQUIRE(setFrameDuration(doc, 0, 999999));
    CHECK(readFrames(doc).front().durationMs == kMaxFrameMs);
}

// The bug every timeline written in a hurry has.
void testACycleNamesPicturesNotPositions() {
    Document doc;
    REQUIRE(build(doc));
    REQUIRE(duplicateFrame(doc, 0) == 1);
    REQUIRE(duplicateFrame(doc, 1) == 2);

    Cycle walk;
    walk.name = "walk";
    walk.frames = { 0, 2 };
    REQUIRE(setCycles(doc, { walk }, 3));

    // Drag the last frame to the front. The cycle still means the same two
    // pictures, so its indices have to move with them.
    REQUIRE(moveFrame(doc, 2, 0));

    const std::vector<Cycle> after = readCycles(doc, 3);
    REQUIRE(after.size() == 1);
    CHECK(after.front().frames == std::vector<int>({ 1, 0 }));
    CHECK(after.front().name == "walk");
}

void testDeletingAFrameLeavesTheCyclesCoherent() {
    Document doc;
    REQUIRE(build(doc));
    REQUIRE(duplicateFrame(doc, 0) == 1);
    REQUIRE(duplicateFrame(doc, 1) == 2);

    Cycle all;
    all.name = "all";
    all.frames = { 0, 1, 2 };
    Cycle justTheMiddle;
    justTheMiddle.name = "middle";
    justTheMiddle.frames = { 1 };
    REQUIRE(setCycles(doc, { all, justTheMiddle }, 3));

    REQUIRE(deleteFrame(doc, 1));

    const std::vector<Cycle> after = readCycles(doc, 2);
    // The frame is gone from the first cycle, and the frame after it renumbered.
    REQUIRE(after.size() == 1);
    CHECK(after.front().name == "all");
    CHECK(after.front().frames == std::vector<int>({ 0, 1 }));
    // The cycle that named only the deleted frame is dropped rather than left
    // empty: an empty cycle is a trap for every loop that reads it.
}

void testCyclesSurviveASave() {
    const std::string path = "animation_cycles_test.lsprite";

    Cycle walk;
    walk.name = "walk | fast\nrun";      // separators in a name must not escape
    walk.frames = { 1, 0, 1 };
    walk.loop = LoopMode::PingPong;
    {
        Document doc;
        REQUIRE(build(doc));
        REQUIRE(duplicateFrame(doc, 0) == 1);
        REQUIRE(setFrameDuration(doc, 1, 320));
        REQUIRE(setFrameName(doc, 1, "contact"));
        REQUIRE(setCycles(doc, { walk }, 2));
        std::string error;
        REQUIRE(doc.save(path, &error));
    }

    Document reopened;
    std::string error;
    REQUIRE(reopened.open(path, &error));

    const std::vector<Frame> frames = readFrames(reopened);
    REQUIRE(frames.size() == 2);
    CHECK(frames[1].durationMs == 320);
    CHECK(frames[1].name == "contact");

    const std::vector<Cycle> cycles = readCycles(reopened, 2);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles.front().name == walk.name);
    CHECK(cycles.front().frames == walk.frames);
    CHECK(cycles.front().loop == LoopMode::PingPong);
}

// --- playback -------------------------------------------------------------

std::vector<Frame> framesOfLength(std::initializer_list<int> durations) {
    std::vector<Frame> frames;
    for (int ms : durations) {
        Frame frame;
        frame.durationMs = ms;
        frames.push_back(frame);
    }
    return frames;
}

void testPlaybackLandsOnTheRightFrame() {
    const std::vector<Frame> frames = framesOfLength({ 100, 200, 100 });
    Cycle cycle = everyFrame(3);

    CHECK(cycleDurationMs(frames, cycle) == 400);
    CHECK(frameAt(frames, cycle, 0)   == 0);
    CHECK(frameAt(frames, cycle, 99)  == 0);
    CHECK(frameAt(frames, cycle, 100) == 1);   // a boundary belongs to the next
    CHECK(frameAt(frames, cycle, 299) == 1);
    CHECK(frameAt(frames, cycle, 300) == 2);
    CHECK(frameAt(frames, cycle, 400) == 0);   // and it loops
    CHECK(frameAt(frames, cycle, 1250) == 0);   // 1250 % 400 == 50
    CHECK(frameAt(frames, cycle, 1150) == 2);   // 1150 % 400 == 350

    // A clock that has not started is the start rather than an error.
    CHECK(frameAt(frames, cycle, -50) == 0);
}

void testPlaybackIsTheSameEveryTimeItPasses() {
    const std::vector<Frame> frames = framesOfLength({ 100, 200, 100 });
    const Cycle cycle = everyFrame(3);

    // Scrubbing backwards must land on what playing forwards showed, which is
    // only true because nothing accumulates.
    for (int64_t t = 0; t < 400; ++t) {
        CHECK(frameAt(frames, cycle, t) == frameAt(frames, cycle, t + 400));
        CHECK(frameAt(frames, cycle, t) == frameAt(frames, cycle, t + 4000));
    }
}

void testOnceHoldsTheLastFrame() {
    const std::vector<Frame> frames = framesOfLength({ 100, 100 });
    Cycle cycle = everyFrame(2);
    cycle.loop = LoopMode::Once;

    CHECK(frameAt(frames, cycle, 0)     == 0);
    CHECK(frameAt(frames, cycle, 150)   == 1);
    CHECK(frameAt(frames, cycle, 200)   == 1);
    CHECK(frameAt(frames, cycle, 99999) == 1);
}

void testPingPongDoesNotPlayTheEndsTwice() {
    const std::vector<Frame> frames = framesOfLength({ 100, 100, 100, 100 });
    Cycle cycle = everyFrame(4);
    cycle.loop = LoopMode::PingPong;

    // 0 1 2 3 2 1, then round again -- six steps, not eight.
    const int expected[] = { 0, 1, 2, 3, 2, 1 };
    for (int step = 0; step < 6; ++step) {
        CHECK(frameAt(frames, cycle, step * 100 + 50) == expected[step]);
    }
    CHECK(frameAt(frames, cycle, 650) == 0);   // and back to the start
}

void testACycleOfOneFrameDoesNotDivideByNothing() {
    const std::vector<Frame> frames = framesOfLength({ 100 });
    for (LoopMode mode : { LoopMode::Loop, LoopMode::Once, LoopMode::PingPong }) {
        Cycle cycle = everyFrame(1);
        cycle.loop = mode;
        CHECK(frameAt(frames, cycle, 0) == 0);
        CHECK(frameAt(frames, cycle, 100000) == 0);
    }

    // And an empty cycle says so rather than indexing into nothing.
    Cycle empty;
    CHECK(frameAt(frames, empty, 0) == -1);
    CHECK(cycleDurationMs(frames, empty) == 0);
}

// --- the format, as untrusted input ---------------------------------------

void testTheCycleFormatRoundTrips() {
    std::vector<Cycle> cycles;
    Cycle a;
    a.name = "walk";
    a.frames = { 0, 1, 2, 1 };
    a.loop = LoopMode::Loop;
    Cycle b;
    b.name = "hurt";
    b.frames = { 3 };
    b.loop = LoopMode::Once;
    cycles = { a, b };

    std::vector<Cycle> read;
    REQUIRE(decodeCycles(encodeCycles(cycles), &read));
    REQUIRE(read.size() == 2);
    CHECK(read[0].name == "walk" && read[0].frames == a.frames);
    CHECK(read[1].name == "hurt" && read[1].loop == LoopMode::Once);
}

void testANameCannotEscapeItsField() {
    Cycle cycle;
    cycle.name = "a|b\nc%d\re";
    cycle.frames = { 0 };

    const std::string encoded = encodeCycles({ cycle });
    // Exactly two lines: a header and one cycle. If the name had escaped, there
    // would be more.
    size_t lines = 0;
    for (char c : encoded) {
        if (c == '\n') { ++lines; }
    }
    CHECK(lines == 2);

    std::vector<Cycle> read;
    REQUIRE(decodeCycles(encoded, &read));
    REQUIRE(read.size() == 1);
    CHECK(read.front().name == cycle.name);
}

void testARubbishCycleFileIsRefusedOrIgnored() {
    std::vector<Cycle> read;

    // Not this format at all.
    CHECK(!decodeCycles("", &read));
    CHECK(!decodeCycles("{\"cycles\":[]}", &read));
    CHECK(!decodeCycles("lsfast-cycles 9\nwalk|0|0\n", &read));

    // This format, but every line malformed. Read as no cycles rather than
    // refused: one bad line should not cost a person the good ones.
    REQUIRE(decodeCycles("lsfast-cycles 1\ngarbage\n||\nx|y|z\n", &read));
    CHECK(read.empty());

    // A good line among bad ones survives.
    REQUIRE(decodeCycles("lsfast-cycles 1\ngarbage\nwalk|0|0,1\nx|y|z\n", &read));
    REQUIRE(read.size() == 1);
    CHECK(read.front().frames == std::vector<int>({ 0, 1 }));

    // An out-of-range loop mode plays as a plain loop rather than as whatever
    // that number happens to cast to.
    REQUIRE(decodeCycles("lsfast-cycles 1\nwalk|77|0\n", &read));
    REQUIRE(read.size() == 1);
    CHECK(read.front().loop == LoopMode::Loop);

    // Numbers that are not numbers, and one that would overflow.
    REQUIRE(decodeCycles("lsfast-cycles 1\nwalk|0|0,notanumber,99999999999,-4,2\n", &read));
    REQUIRE(read.size() == 1);
    CHECK(read.front().frames == std::vector<int>({ 0, 2 }));
}

void testAFileCannotNameFramesThatDoNotExist() {
    Document doc;
    REQUIRE(build(doc));

    // Written straight into metadata, the way a file that came from elsewhere
    // would arrive -- past setCycles, which would have cleaned it.
    REQUIRE(doc.engine().setMetadata(doc.id().value, kCyclesKey,
                                          "lsfast-cycles 1\nwalk|0|0,9,4000\nghost|0|7\n").ok());

    const std::vector<Cycle> cycles = readCycles(doc, 1);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles.front().frames == std::vector<int>({ 0 }));
}

void testTheNumberOfCyclesIsBounded() {
    std::string text = "lsfast-cycles 1\n";
    for (int i = 0; i < 500; ++i) {
        text += "c" + std::to_string(i) + "|0|0\n";
    }
    std::vector<Cycle> read;
    REQUIRE(decodeCycles(text, &read));
    CHECK(read.size() <= kMaxCycles);
}


// --- editing cycles -------------------------------------------------------

// Three frames, so a cycle has something to be a sequence over.
bool buildThree(Document& doc) {
    if (!build(doc)) {
        return false;
    }
    return duplicateFrame(doc, 0) == 1 && duplicateFrame(doc, 1) == 2;
}

void testANewCycleCoversEveryFrame() {
    Document doc;
    REQUIRE(buildThree(doc));

    REQUIRE(addCycle(doc, "walk", 3) == 0);
    const std::vector<Cycle> cycles = readCycles(doc, 3);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles.front().name == "walk");
    CHECK(cycles.front().frames == std::vector<int>({ 0, 1, 2 }));
    CHECK(cycles.front().loop == LoopMode::Loop);

    // A second one is a second one, not a replacement.
    REQUIRE(addCycle(doc, "hurt", 3) == 1);
    CHECK(readCycles(doc, 3).size() == 2);
}

// The reason a cycle is a sequence over frames rather than a set of them: a
// step may name a frame another step already named.
void testACycleCanPlayAFrameTwice() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "walk", 3) == 0);

    // 0 1 2 -> 0 1 2 1, the shape every four-frame walk actually has.
    REQUIRE(addCycleStep(doc, 0, 2, 1, 3) == 3);
    std::vector<Cycle> cycles = readCycles(doc, 3);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles.front().frames == std::vector<int>({ 0, 1, 2, 1 }));

    // And the two steps naming frame 1 are independent: removing one leaves
    // the other.
    REQUIRE(removeCycleStep(doc, 0, 1, 3));
    cycles = readCycles(doc, 3);
    CHECK(cycles.front().frames == std::vector<int>({ 0, 2, 1 }));
}

void testStepsInsertWhereAsked() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "walk", 3) == 0);

    REQUIRE(addCycleStep(doc, 0, 0, 2, 3) == 1);          // after the first step
    CHECK(readCycles(doc, 3).front().frames == std::vector<int>({ 0, 2, 1, 2 }));

    // A step index outside the cycle appends rather than failing.
    REQUIRE(addCycleStep(doc, 0, 999, 0, 3) == 4);
    CHECK(readCycles(doc, 3).front().frames == std::vector<int>({ 0, 2, 1, 2, 0 }));

    // A frame index outside the document is refused rather than stored and
    // dropped on the next read.
    CHECK(addCycleStep(doc, 0, 0, 9, 3) < 0);
    CHECK(addCycleStep(doc, 0, 0, -1, 3) < 0);
    CHECK(addCycleStep(doc, 7, 0, 0, 3) < 0);             // no such cycle
}

void testTheLastStepStays() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "one", 3) == 0);

    REQUIRE(removeCycleStep(doc, 0, 0, 3));
    REQUIRE(removeCycleStep(doc, 0, 0, 3));
    REQUIRE(readCycles(doc, 3).front().frames.size() == 1);

    // Removing the last one would delete the cycle rather than empty it, which
    // is a different thing a person asks for differently.
    CHECK(!removeCycleStep(doc, 0, 0, 3));
    CHECK(readCycles(doc, 3).size() == 1);

    CHECK(deleteCycle(doc, 0, 3));
    CHECK(readCycles(doc, 3).empty());
}

void testStepsReorder() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "walk", 3) == 0);

    REQUIRE(moveCycleStep(doc, 0, 2, 0, 3));
    CHECK(readCycles(doc, 3).front().frames == std::vector<int>({ 2, 0, 1 }));

    REQUIRE(moveCycleStep(doc, 0, 0, 2, 3));
    CHECK(readCycles(doc, 3).front().frames == std::vector<int>({ 0, 1, 2 }));

    CHECK(!moveCycleStep(doc, 0, 0, 0, 3));      // nowhere is not a move
    CHECK(!moveCycleStep(doc, 0, 0, 9, 3));
}

void testRenamingAndLooping() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "walk", 3) == 0);

    REQUIRE(renameCycle(doc, 0, "run", 3));
    CHECK(readCycles(doc, 3).front().name == "run");

    REQUIRE(setCycleLoop(doc, 0, LoopMode::PingPong, 3));
    CHECK(readCycles(doc, 3).front().loop == LoopMode::PingPong);

    CHECK(!renameCycle(doc, 4, "nope", 3));
    CHECK(!setCycleLoop(doc, 4, LoopMode::Once, 3));
}

void testEveryCycleEditIsOneUndoStep() {
    Document doc;
    REQUIRE(buildThree(doc));
    doc.markUnmodified();

    REQUIRE(addCycle(doc, "walk", 3) == 0);
    CHECK(doc.modified());
    REQUIRE(addCycleStep(doc, 0, 0, 2, 3) >= 0);
    CHECK(readCycles(doc, 3).front().frames.size() == 4);

    REQUIRE(doc.undo());
    CHECK(readCycles(doc, 3).front().frames.size() == 3);   // the step came back off
    REQUIRE(doc.undo());
    CHECK(readCycles(doc, 3).empty());                      // and the cycle itself
    REQUIRE(doc.redo());
    CHECK(readCycles(doc, 3).size() == 1);
}

// Deleting a frame has to reach the cycles that name it, including the steps
// that named it twice.
void testDeletingAFrameCleansEveryStep() {
    Document doc;
    REQUIRE(buildThree(doc));
    REQUIRE(addCycle(doc, "walk", 3) == 0);
    REQUIRE(addCycleStep(doc, 0, 2, 1, 3) == 3);        // 0 1 2 1

    REQUIRE(deleteFrame(doc, 1));                        // the frame both steps named

    const std::vector<Cycle> after = readCycles(doc, 2);
    REQUIRE(after.size() == 1);
    // Both steps naming frame 1 are gone, and frame 2 renumbered down to 1.
    CHECK(after.front().frames == std::vector<int>({ 0, 1 }));
}

void testCyclesAreBounded() {
    Document doc;
    REQUIRE(build(doc));

    for (size_t i = 0; i < kMaxCycles + 8; ++i) {
        addCycle(doc, "c" + std::to_string(i), 1);
    }
    CHECK(readCycles(doc, 1).size() == kMaxCycles);
}

} // namespace

// A run of frames: reversing it keeps every cycle naming the same pictures,
// duplicating it puts the copies straight after it in order, deleting it
// leaves the rest and refuses to leave nothing, and a hold set across it is
// one step.
void testRangesOfFrames() {
    fast::Document doc;
    if (!doc.create("ranges", 4, 4)) { CHECK(false); return; }
    for (int i = 0; i < 3; ++i) {
        if (fast::duplicateFrame(doc, i) < 0) { CHECK(false); return; }
    }
    std::vector<fast::Frame> before = fast::readFrames(doc);
    if (before.size() != 4) { CHECK(false); return; }
    fast::Cycle cycle;
    cycle.name = "walk";
    cycle.frames = { 0, 1, 2, 3 };
    fast::setCycles(doc, { cycle }, 4);
    doc.clearHistory();

    CHECK(fast::reverseFrames(doc, 1, 3));
    std::vector<fast::Frame> after = fast::readFrames(doc);
    CHECK(after[1].sprite == before[3].sprite && after[3].sprite == before[1].sprite);
    // The cycle still plays the same pictures in the same order.
    const std::vector<fast::Cycle> cycles = fast::readCycles(doc, 4);
    CHECK(cycles.size() == 1 && cycles[0].frames == std::vector<int>({ 0, 3, 2, 1 }));
    CHECK(doc.undo() && !doc.canUndo());

    CHECK(fast::duplicateFrames(doc, 1, 2) == 3);
    after = fast::readFrames(doc);
    CHECK(after.size() == 6);
    CHECK(after[0].sprite == before[0].sprite && after[1].sprite == before[1].sprite &&
          after[2].sprite == before[2].sprite && after[5].sprite == before[3].sprite);
    CHECK(doc.undo() && !doc.canUndo());

    CHECK(fast::setFramesDuration(doc, 0, 2, 250));
    after = fast::readFrames(doc);
    CHECK(after[0].durationMs == 250 && after[2].durationMs == 250 &&
          after[3].durationMs != 250);
    CHECK(doc.undo() && !doc.canUndo());

    CHECK(!fast::deleteFrames(doc, 0, 3));                    // not every frame
    CHECK(fast::deleteFrames(doc, 1, 2));
    after = fast::readFrames(doc);
    CHECK(after.size() == 2 && after[1].sprite == before[3].sprite);
    CHECK(doc.undo() && !doc.canUndo());
}

int main() {
    testRangesOfFrames();
    testANewDocumentIsOneFrame();
    testADuplicatedFrameIsItsOwn();
    testAFrameLandsAfterTheOneItCameFrom();
    testTheLastFrameCannotBeDeleted();
    testAddingAFrameIsUndoable();
    testADurationRidesWithTheDocument();
    testADurationIsClamped();
    testACycleNamesPicturesNotPositions();
    testDeletingAFrameLeavesTheCyclesCoherent();
    testCyclesSurviveASave();
    testPlaybackLandsOnTheRightFrame();
    testPlaybackIsTheSameEveryTimeItPasses();
    testOnceHoldsTheLastFrame();
    testPingPongDoesNotPlayTheEndsTwice();
    testACycleOfOneFrameDoesNotDivideByNothing();
    testTheCycleFormatRoundTrips();
    testANameCannotEscapeItsField();
    testARubbishCycleFileIsRefusedOrIgnored();
    testAFileCannotNameFramesThatDoNotExist();
    testTheNumberOfCyclesIsBounded();
    testANewCycleCoversEveryFrame();
    testACycleCanPlayAFrameTwice();
    testStepsInsertWhereAsked();
    testTheLastStepStays();
    testStepsReorder();
    testRenamingAndLooping();
    testEveryCycleEditIsOneUndoStep();
    testDeletingAFrameCleansEveryStep();
    testCyclesAreBounded();
    if (failures == 0) {
        std::printf("animation: all checks passed\n");
        return 0;
    }
    std::printf("animation: %d check(s) failed\n", failures);
    return 1;
}
