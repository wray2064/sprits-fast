// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// animation.h — frames, cycles, and where each of them actually lives.
//
// The decision this file exists to record: **a frame is a sprite.** A LiveSprite
// document already holds an ordered list of sprites, each with its own layers,
// operations and transforms, and that list is what a saved file carries. So Fast
// keeps no frame list of its own. The document's sprite order *is* the timeline,
// which means there is no second list to fall out of step with the first, and
// undo -- which restores engine state wholesale -- puts the timeline back
// without being told the timeline exists.
//
// Everything the engine has no opinion about is stored beside it:
//
//   - How long a frame is held, and what it is called, live in engine metadata
//     on the sprite ("fast.frame.ms", "fast.frame.name"). Metadata is part of
//     the document, so it is snapshotted by undo and written into the file --
//     the two properties a duration has to have.
//   - Cycles -- named runs of frames, like "walk" or "hurt" -- live in metadata
//     on the document ("fast.cycles"), for the same reasons.
//   - Which frame is being looked at, and whether playback is running, are not
//     properties of the artwork. They ride in the `fast/` view entry with zoom
//     and pan.
//
// **All of it is untrusted input.** A frame duration, a cycle's frame list and a
// cycle's name all arrive from whoever sent the file. Everything here clamps or
// refuses rather than trusting, and an index that names no frame is dropped
// instead of being carried into the interface.

#include "app/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// Metadata keys. Namespaced because the engine insists on it: two applications
// annotating one document must not be able to overwrite each other.
constexpr const char* kFrameDurationKey = "fast.frame.ms";
constexpr const char* kFrameNameKey     = "fast.frame.name";
constexpr const char* kCyclesKey        = "fast.cycles";

// How long a frame may be held.
//
// The floor is one millisecond because zero is not a frame, it is a division by
// nothing in every playback loop that ever reads it. The ceiling is a minute,
// which is far longer than any sprite frame and still short enough that a
// mistyped number cannot make a timeline that appears frozen.
constexpr int kMinFrameMs     = 1;
constexpr int kMaxFrameMs     = 60000;
constexpr int kDefaultFrameMs = 100;      // 10 fps, the usual pixel-art starting point

// Fast's policy, not the engine's. A compile is area times layers times frames,
// and the interface has to draw a strip of every one of them.
constexpr size_t kMaxFrames          = 512;
constexpr size_t kMaxCycles          = 64;
constexpr size_t kMaxFramesPerCycle  = 1024;   // a cycle may repeat a frame
constexpr size_t kMaxNameLength      = 64;

struct Frame {
    ls::SpriteId sprite;
    std::string  name;                    // empty means unnamed; the panel numbers it
    int          durationMs = kDefaultFrameMs;
};

// What happens when a cycle reaches its end.
enum class LoopMode : uint8_t {
    Loop = 0,      // back to the start
    Once = 1,      // hold the last frame
    PingPong = 2,  // run back down without repeating either end
};

struct Cycle {
    std::string      name;
    std::vector<int> frames;               // indices into the frame list
    LoopMode         loop = LoopMode::Loop;
};

// ------------------------------------------------------------------ reading --

// The document's sprites, in order, with whatever Fast has recorded about each.
// This is the only place the frame list comes from.
std::vector<Frame> readFrames(Document& doc);

// Cycles recorded on the document, with any frame index that names no frame
// dropped. A cycle left with no frames is dropped entirely: an empty cycle in a
// list of cycles is a trap for every loop that reads it.
std::vector<Cycle> readCycles(Document& doc, int frameCount);

// ------------------------------------------------------------------ editing --
//
// Each of these brackets its own undo action, so a caller does not have to.

// Duplicates the frame at `index` and puts the copy directly after it, which is
// how a frame is nearly always made: from the one before it. Returns the new
// frame's position, or -1.
int duplicateFrame(Document& doc, int index);

// A new empty frame after `index` -- a blank sheet rather than a copy.
int addFrame(Document& doc, int index);

// Removes a frame, and any reference to it from every cycle. The last frame
// cannot be removed: a document with no frames has nothing to draw on.
bool deleteFrame(Document& doc, int index);

// Moves a frame, carrying cycle references with it so a cycle still names the
// same pictures in the same order after a drag.
bool moveFrame(Document& doc, int from, int to);

bool setFrameDuration(Document& doc, int index, int milliseconds);
bool setFrameName(Document& doc, int index, const std::string& name);

// Replaces the whole cycle list. Bounded and cleaned before it is written.
bool setCycles(Document& doc, const std::vector<Cycle>& cycles, int frameCount);

// ------------------------------------------------------------------ cycles --
//
// A cycle is a sequence of steps, and a step names a frame. That is a level of
// indirection over "the frames, in order", and it is the level that earns its
// keep: a step can name a frame another step already named, so a four-frame
// walk can be authored once and played 0 1 2 1, and two cycles can share the
// same drawings without either owning them.
//
// Each of these brackets its own undo action and writes the whole list, which a
// cycle list is small enough for. `frameCount` is what the indices are checked
// against -- a step naming a frame that does not exist is dropped rather than
// carried into the interface.

// A new cycle covering every frame in order, which is the useful thing to start
// from: it plays immediately, and refining it is subtraction. Returns its index,
// or -1.
int addCycle(Document& doc, const std::string& name, int frameCount);

bool renameCycle(Document& doc, int index, const std::string& name, int frameCount);
bool deleteCycle(Document& doc, int index, int frameCount);
bool setCycleLoop(Document& doc, int index, LoopMode loop, int frameCount);

// Puts `frame` into the cycle just after step `afterStep`; a step index outside
// the cycle appends. Returns the new step's position, or -1.
int addCycleStep(Document& doc, int cycleIndex, int afterStep, int frame, int frameCount);

// The last step of a cycle stays, for the same reason the last frame of a
// document does: an empty cycle is a trap for every loop that reads it, and
// deleting the cycle is the thing that was actually meant.
bool removeCycleStep(Document& doc, int cycleIndex, int step, int frameCount);
bool moveCycleStep(Document& doc, int cycleIndex, int from, int to, int frameCount);

// ---------------------------------------------------------------- playback --
//
// Deterministic, and stateless on purpose: given a time, these say what to show.
// A playhead that accumulates instead would drift, and scrubbing backwards would
// not land on what playing forwards showed.

// Total length of one pass. Zero if the cycle names no frames.
int cycleDurationMs(const std::vector<Frame>& frames, const Cycle& cycle);

// Which entry *of the cycle* is showing at `elapsedMs`, or -1 if the cycle is
// empty. Index this into `cycle.frames` to get the frame.
int cyclePositionAt(const std::vector<Frame>& frames, const Cycle& cycle, int64_t elapsedMs);

// Which frame is showing at `elapsedMs`, or -1. The one most callers want.
int frameAt(const std::vector<Frame>& frames, const Cycle& cycle, int64_t elapsedMs);

// A cycle covering every frame in order, which is what a document with no named
// cycles plays.
Cycle everyFrame(int frameCount);

// ----------------------------------------------------------- serialisation --
//
// Cycles are stored as text rather than JSON. The view entry's parser reads a
// flat object of numbers and nothing else, and cycles need names and lists --
// so this is a small line format of its own instead of a second, larger JSON
// parser reading data from files other people send.
//
//   lsfast-cycles 1
//   <escaped name>|<loop>|<index>,<index>,...
//
// `|`, newline and `%` in a name are percent-escaped, so a name can say anything
// without being able to say a new field or a new line.

std::string encodeCycles(const std::vector<Cycle>& cycles);

// Returns false only if the text is not this format at all. Anything malformed
// *within* the format is dropped rather than failing the whole read: one bad
// cycle should not cost a person the other seven.
bool decodeCycles(const std::string& text, std::vector<Cycle>* out);

} // namespace fast
