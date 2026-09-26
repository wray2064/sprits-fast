// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/animation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fast {
namespace {

std::string trimName(const std::string& name) {
    return name.size() > kMaxNameLength ? name.substr(0, kMaxNameLength) : name;
}

int clampDuration(int64_t milliseconds) {
    if (milliseconds < kMinFrameMs) { return kMinFrameMs; }
    if (milliseconds > kMaxFrameMs) { return kMaxFrameMs; }
    return static_cast<int>(milliseconds);
}

// The document's sprite list, which is the frame list.
std::vector<ls::SpriteId> spritesOf(Document& doc) {
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return {};
    }
    return info.value.sprites;
}

bool inRange(int index, size_t count) {
    return index >= 0 && static_cast<size_t>(index) < count;
}

// Writing the cycle list back is the same three lines everywhere it happens.
bool writeCycles(Document& doc, const std::vector<Cycle>& cycles) {
    return doc.engine().setMetadata(doc.id().value, kCyclesKey, encodeCycles(cycles)).ok();
}

// --- the name escape ------------------------------------------------------
//
// Only three characters can hurt: the field separator, the line separator, and
// the escape itself. Everything else, including any UTF-8, passes through
// untouched -- escaping bytes above 127 would mangle every name that is not
// English.

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '|':  out += "%7C"; break;
            case '\n': out += "%0A"; break;
            case '\r': out += "%0D"; break;
            case '%':  out += "%25"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

std::string unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 2 >= text.size()) {
            // A trailing, truncated escape is kept as the literal character it
            // is rather than reading past the end of the string.
            out.push_back(text[i]);
            continue;
        }
        const int high = hexDigit(text[i + 1]);
        const int low  = hexDigit(text[i + 2]);
        if (high < 0 || low < 0) {
            out.push_back(text[i]);
            continue;
        }
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
    }
    return out;
}

std::vector<std::string> splitOn(const std::string& text, char separator, size_t limit) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            if (parts.size() >= limit) {
                return parts;
            }
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    parts.push_back(current);
    return parts;
}

// A whole number, or false. strtol would accept "12abc" and leading spaces.
bool readInt(const std::string& text, int* out) {
    if (text.empty() || text.size() > 11) {
        return false;
    }
    size_t at = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        at = 1;
        if (text.size() == 1) { return false; }
    }
    int64_t value = 0;
    for (; at < text.size(); ++at) {
        if (text[at] < '0' || text[at] > '9') {
            return false;
        }
        value = value * 10 + (text[at] - '0');
        if (value > 2147483647LL) {
            return false;
        }
    }
    *out = static_cast<int>(negative ? -value : value);
    return true;
}

} // namespace

// ------------------------------------------------------------------ reading --

std::vector<Frame> readFrames(Document& doc) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    std::vector<Frame> frames;
    frames.reserve(sprites.size());

    for (ls::SpriteId sprite : sprites) {
        Frame frame;
        frame.sprite = sprite;

        auto duration = doc.engine().getMetadata(sprite.value, kFrameDurationKey);
        int parsed = kDefaultFrameMs;
        // A frame written by an older build, or by another application, has no
        // duration recorded. That is not an error; it is the default.
        frame.durationMs = (duration.ok() && readInt(duration.value, &parsed))
            ? clampDuration(parsed)
            : kDefaultFrameMs;

        auto name = doc.engine().getMetadata(sprite.value, kFrameNameKey);
        if (name.ok()) {
            frame.name = trimName(name.value);
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

std::vector<Cycle> readCycles(Document& doc, int frameCount) {
    auto stored = doc.engine().getMetadata(doc.id().value, kCyclesKey);
    if (stored.fail()) {
        return {};
    }
    std::vector<Cycle> cycles;
    if (!decodeCycles(stored.value, &cycles)) {
        return {};
    }

    // Drop what no longer exists. A file can name frame 40 of a document that
    // holds four, and every loop downstream of here would index on that.
    std::vector<Cycle> kept;
    for (Cycle& cycle : cycles) {
        std::vector<int> frames;
        for (int index : cycle.frames) {
            if (inRange(index, static_cast<size_t>(std::max(frameCount, 0)))) {
                frames.push_back(index);
            }
        }
        if (frames.empty()) {
            continue;
        }
        cycle.frames = std::move(frames);
        kept.push_back(std::move(cycle));
    }
    return kept;
}

// ------------------------------------------------------------------ editing --

namespace {

// The body of duplicate and add: make a sprite, put it after `index`, record a
// duration on it. Assumes an action is already open.
int insertFrameAfter(Document& doc, int index, ls::SpriteId made, int durationMs) {
    std::vector<ls::SpriteId> order = spritesOf(doc);

    // A fresh sprite lands at the end of the document; move it into place.
    order.erase(std::remove(order.begin(), order.end(), made), order.end());
    const size_t at = std::min(static_cast<size_t>(index) + 1, order.size());
    order.insert(order.begin() + static_cast<ptrdiff_t>(at), made);

    if (!doc.engine().setSpriteOrder(doc.id(), order).ok()) {
        return -1;
    }

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", clampDuration(durationMs));
    doc.engine().setMetadata(made.value, kFrameDurationKey, buffer);
    return static_cast<int>(at);
}

// Every cycle, with frame indices renumbered by `moved`. An index mapped to -1
// is dropped.
std::vector<Cycle> renumbered(const std::vector<Cycle>& cycles,
                              const std::vector<int>& moved) {
    std::vector<Cycle> out;
    for (const Cycle& cycle : cycles) {
        Cycle updated;
        updated.name = cycle.name;
        updated.loop = cycle.loop;
        for (int index : cycle.frames) {
            if (!inRange(index, moved.size())) {
                continue;
            }
            if (moved[static_cast<size_t>(index)] >= 0) {
                updated.frames.push_back(moved[static_cast<size_t>(index)]);
            }
        }
        if (!updated.frames.empty()) {
            out.push_back(std::move(updated));
        }
    }
    return out;
}

} // namespace

int duplicateFrame(Document& doc, int index) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(index, sprites.size()) || sprites.size() >= kMaxFrames) {
        return -1;
    }

    // Carry the source frame's timing into the copy: duplicating a frame and
    // getting a different duration is a surprise nobody wants.
    const std::vector<Frame> frames = readFrames(doc);
    const int duration = inRange(index, frames.size())
        ? frames[static_cast<size_t>(index)].durationMs
        : kDefaultFrameMs;

    doc.beginAction("Duplicate frame");
    auto clone = doc.engine().cloneSprite(sprites[static_cast<size_t>(index)]);
    if (clone.fail()) {
        doc.abandonAction();
        return -1;
    }
    // A copy is its own pixels: a linked cel's copy is not linked (see
    // tracks.h -- linking is asked for).
    auto cloned = doc.engine().getSpriteInfo(clone.value);
    if (cloned.ok()) {
        for (ls::LayerId layer : cloned.value.layers) {
            doc.engine().clearMetadata(layer.value, "fast.link");
        }
    }
    const int at = insertFrameAfter(doc, index, clone.value, duration);
    if (at < 0) {
        doc.abandonAction();
        return -1;
    }
    doc.endAction();
    return at;
}

int addFrame(Document& doc, int index) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (sprites.size() >= kMaxFrames) {
        return -1;
    }
    // An index outside the list means "at the end", which is what a plain New
    // Frame does.
    const int after = inRange(index, sprites.size())
        ? index
        : static_cast<int>(sprites.size()) - 1;

    doc.beginAction("Add frame");
    auto made = doc.engine().createSprite(doc.id());
    if (made.fail()) {
        doc.abandonAction();
        return -1;
    }
    const int at = insertFrameAfter(doc, after, made.value, kDefaultFrameMs);
    if (at < 0) {
        doc.abandonAction();
        return -1;
    }
    doc.endAction();
    return at;
}

bool deleteFrame(Document& doc, int index) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    // The last frame stays. A document with no sprites has nothing to draw on
    // and no way back except undo, which is not a state to leave someone in.
    if (!inRange(index, sprites.size()) || sprites.size() <= 1) {
        return false;
    }

    const std::vector<Cycle> cycles = readCycles(doc, static_cast<int>(sprites.size()));

    doc.beginAction("Delete frame");
    if (!doc.engine().deleteSprite(sprites[static_cast<size_t>(index)]).ok()) {
        doc.abandonAction();
        return false;
    }

    // Everything after the hole shifts down one; the deleted frame maps to -1
    // and falls out of every cycle that named it.
    std::vector<int> moved(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        moved[i] = (static_cast<int>(i) == index)
            ? -1
            : static_cast<int>(i) - (static_cast<int>(i) > index ? 1 : 0);
    }
    writeCycles(doc, renumbered(cycles, moved));

    doc.endAction();
    return true;
}

bool moveFrame(Document& doc, int from, int to) {
    std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(from, sprites.size()) || !inRange(to, sprites.size()) || from == to) {
        return false;
    }
    const std::vector<Cycle> cycles = readCycles(doc, static_cast<int>(sprites.size()));

    const ls::SpriteId moving = sprites[static_cast<size_t>(from)];
    sprites.erase(sprites.begin() + from);
    sprites.insert(sprites.begin() + to, moving);

    doc.beginAction("Move frame");
    if (!doc.engine().setSpriteOrder(doc.id(), sprites).ok()) {
        doc.abandonAction();
        return false;
    }

    // A cycle names pictures, not positions: after a drag it has to still name
    // the same pictures, in the same order.
    std::vector<int> moved(sprites.size());
    for (int i = 0; i < static_cast<int>(moved.size()); ++i) {
        if (i == from)                        { moved[static_cast<size_t>(i)] = to; }
        else if (from < to && i > from && i <= to) { moved[static_cast<size_t>(i)] = i - 1; }
        else if (to < from && i >= to && i < from) { moved[static_cast<size_t>(i)] = i + 1; }
        else                                  { moved[static_cast<size_t>(i)] = i; }
    }
    writeCycles(doc, renumbered(cycles, moved));

    doc.endAction();
    return true;
}

bool reverseFrames(Document& doc, int first, int last) {
    std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(first, sprites.size()) || !inRange(last, sprites.size()) || first >= last) {
        return false;
    }
    const std::vector<Cycle> cycles = readCycles(doc, static_cast<int>(sprites.size()));
    std::reverse(sprites.begin() + first, sprites.begin() + last + 1);
    doc.beginAction("Reverse frames");
    if (!doc.engine().setSpriteOrder(doc.id(), sprites).ok()) {
        doc.abandonAction();
        return false;
    }
    // Every cycle keeps naming the same pictures: a walk that played the run
    // forwards now plays it backwards, which is the point of reversing it.
    std::vector<int> moved(sprites.size());
    for (int i = 0; i < static_cast<int>(moved.size()); ++i) {
        moved[static_cast<size_t>(i)] = (i >= first && i <= last) ? first + last - i : i;
    }
    writeCycles(doc, renumbered(cycles, moved));
    doc.endAction();
    return true;
}

bool deleteFrames(Document& doc, int first, int last) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(first, sprites.size()) || !inRange(last, sprites.size()) || first > last) {
        return false;
    }
    const int count = last - first + 1;
    if (count >= static_cast<int>(sprites.size())) {
        return false;
    }
    doc.beginAction(count == 1 ? "Delete frame" : "Delete frames");
    for (int i = 0; i < count; ++i) {
        if (!deleteFrame(doc, first)) {
            doc.abandonAction();
            return false;
        }
    }
    doc.endAction();
    return true;
}

bool setFramesDuration(Document& doc, int first, int last, int milliseconds) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(first, sprites.size()) || !inRange(last, sprites.size()) || first > last) {
        return false;
    }
    doc.beginAction("Frame durations");
    for (int i = first; i <= last; ++i) {
        if (!setFrameDuration(doc, i, milliseconds)) {
            doc.abandonAction();
            return false;
        }
    }
    doc.endAction();
    return true;
}

int duplicateFrames(Document& doc, int first, int last) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(first, sprites.size()) || !inRange(last, sprites.size()) || first > last) {
        return -1;
    }
    const int count = last - first + 1;
    if (sprites.size() + static_cast<size_t>(count) > kMaxFrames) {
        return -1;
    }
    // Each frame is copied beside itself and the copy carried to the end of
    // the run, so the originals stay put and the copies follow in order.
    doc.beginAction(count == 1 ? "Duplicate frame" : "Duplicate frames");
    for (int k = 0; k < count; ++k) {
        const int copy = duplicateFrame(doc, first + k);
        if (copy < 0 || (copy != last + 1 + k && !moveFrame(doc, copy, last + 1 + k))) {
            doc.abandonAction();
            return -1;
        }
    }
    doc.endAction();
    return last + 1;
}

bool setFrameDuration(Document& doc, int index, int milliseconds) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(index, sprites.size())) {
        return false;
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", clampDuration(milliseconds));

    doc.beginAction("Frame duration");
    const bool ok = doc.engine()
        .setMetadata(sprites[static_cast<size_t>(index)].value, kFrameDurationKey, buffer)
        .ok();
    if (!ok) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

bool setFrameName(Document& doc, int index, const std::string& name) {
    const std::vector<ls::SpriteId> sprites = spritesOf(doc);
    if (!inRange(index, sprites.size())) {
        return false;
    }
    doc.beginAction("Rename frame");
    const bool ok = doc.engine()
        .setMetadata(sprites[static_cast<size_t>(index)].value, kFrameNameKey, trimName(name))
        .ok();
    if (!ok) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

bool setCycles(Document& doc, const std::vector<Cycle>& cycles, int frameCount) {
    std::vector<Cycle> cleaned;
    for (const Cycle& cycle : cycles) {
        if (cleaned.size() >= kMaxCycles) {
            break;
        }
        Cycle kept;
        kept.name = trimName(cycle.name);
        kept.loop = cycle.loop;
        for (int index : cycle.frames) {
            if (kept.frames.size() >= kMaxFramesPerCycle) {
                break;
            }
            if (inRange(index, static_cast<size_t>(std::max(frameCount, 0)))) {
                kept.frames.push_back(index);
            }
        }
        if (kept.frames.empty()) {
            continue;
        }
        cleaned.push_back(std::move(kept));
    }

    doc.beginAction("Edit cycles");
    if (!writeCycles(doc, cleaned)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

// ------------------------------------------------------------------ cycles --

namespace {

// Read the list, change one cycle, write it back. Every cycle edit is this
// shape, and doing it in one place is what keeps the bounds and the undo
// bracket from being remembered separately each time.
template<typename Change>
bool editCycles(Document& doc, int frameCount, const Change& change) {
    std::vector<Cycle> cycles = readCycles(doc, frameCount);
    if (!change(cycles)) {
        return false;
    }
    return setCycles(doc, cycles, frameCount);
}

bool namesACycle(const std::vector<Cycle>& cycles, int index) {
    return index >= 0 && static_cast<size_t>(index) < cycles.size();
}

} // namespace

int addCycle(Document& doc, const std::string& name, int frameCount) {
    if (frameCount <= 0) {
        return -1;
    }
    std::vector<Cycle> cycles = readCycles(doc, frameCount);
    if (cycles.size() >= kMaxCycles) {
        return -1;
    }
    Cycle made = everyFrame(frameCount);
    made.name = trimName(name);
    cycles.push_back(std::move(made));

    if (!setCycles(doc, cycles, frameCount)) {
        return -1;
    }
    return static_cast<int>(cycles.size()) - 1;
}

bool renameCycle(Document& doc, int index, const std::string& name, int frameCount) {
    return editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, index)) {
            return false;
        }
        cycles[static_cast<size_t>(index)].name = trimName(name);
        return true;
    });
}

bool deleteCycle(Document& doc, int index, int frameCount) {
    return editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, index)) {
            return false;
        }
        cycles.erase(cycles.begin() + index);
        return true;
    });
}

bool setCycleLoop(Document& doc, int index, LoopMode loop, int frameCount) {
    return editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, index)) {
            return false;
        }
        cycles[static_cast<size_t>(index)].loop = loop;
        return true;
    });
}

int addCycleStep(Document& doc, int cycleIndex, int afterStep, int frame,
                 int frameCount) {
    int landed = -1;
    const bool ok = editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, cycleIndex) || !inRange(frame, static_cast<size_t>(
                std::max(frameCount, 0)))) {
            return false;
        }
        std::vector<int>& steps = cycles[static_cast<size_t>(cycleIndex)].frames;
        if (steps.size() >= kMaxFramesPerCycle) {
            return false;
        }
        const size_t at = inRange(afterStep, steps.size())
            ? static_cast<size_t>(afterStep) + 1
            : steps.size();
        steps.insert(steps.begin() + static_cast<ptrdiff_t>(at), frame);
        landed = static_cast<int>(at);
        return true;
    });
    return ok ? landed : -1;
}

bool removeCycleStep(Document& doc, int cycleIndex, int step, int frameCount) {
    return editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, cycleIndex)) {
            return false;
        }
        std::vector<int>& steps = cycles[static_cast<size_t>(cycleIndex)].frames;
        // The last step stays: setCycles drops a cycle with none, so removing
        // it would silently delete the cycle rather than empty it, and deleting
        // the cycle is a different thing a person asks for differently.
        if (!inRange(step, steps.size()) || steps.size() <= 1) {
            return false;
        }
        steps.erase(steps.begin() + step);
        return true;
    });
}

bool moveCycleStep(Document& doc, int cycleIndex, int from, int to, int frameCount) {
    return editCycles(doc, frameCount, [&](std::vector<Cycle>& cycles) {
        if (!namesACycle(cycles, cycleIndex)) {
            return false;
        }
        std::vector<int>& steps = cycles[static_cast<size_t>(cycleIndex)].frames;
        if (!inRange(from, steps.size()) || !inRange(to, steps.size()) || from == to) {
            return false;
        }
        const int moving = steps[static_cast<size_t>(from)];
        steps.erase(steps.begin() + from);
        steps.insert(steps.begin() + to, moving);
        return true;
    });
}

// ---------------------------------------------------------------- playback --

int cycleDurationMs(const std::vector<Frame>& frames, const Cycle& cycle) {
    int64_t total = 0;
    for (int index : cycle.frames) {
        if (inRange(index, frames.size())) {
            total += clampDuration(frames[static_cast<size_t>(index)].durationMs);
        }
    }
    return static_cast<int>(std::min<int64_t>(total, 2147483647LL));
}

int cyclePositionAt(const std::vector<Frame>& frames, const Cycle& cycle, int64_t elapsedMs) {
    if (cycle.frames.empty()) {
        return -1;
    }
    const int64_t length = cycleDurationMs(frames, cycle);
    if (length <= 0) {
        return 0;      // every frame is minimum length or missing; show the first
    }
    const int64_t count = static_cast<int64_t>(cycle.frames.size());

    // Before the start counts as the start: a negative time is a caller whose
    // clock has not begun, not a request to play backwards.
    int64_t time = elapsedMs < 0 ? 0 : elapsedMs;

    switch (cycle.loop) {
        case LoopMode::Once:
            if (time >= length) {
                return static_cast<int>(count - 1);    // hold the last frame
            }
            break;

        case LoopMode::PingPong: {
            // Down and back without playing either end twice, so a 4-frame
            // ping-pong is 6 steps rather than 8.
            if (count == 1) {
                return 0;
            }
            int64_t span = length;
            for (int64_t i = 1; i + 1 < count; ++i) {
                const int index = cycle.frames[static_cast<size_t>(i)];
                if (inRange(index, frames.size())) {
                    span += clampDuration(frames[static_cast<size_t>(index)].durationMs);
                }
            }
            if (span <= 0) {
                return 0;
            }
            time %= span;
            if (time >= length) {
                // On the way back down.
                int64_t back = time - length;
                for (int64_t i = count - 2; i >= 1; --i) {
                    const int index = cycle.frames[static_cast<size_t>(i)];
                    const int64_t hold = inRange(index, frames.size())
                        ? clampDuration(frames[static_cast<size_t>(index)].durationMs)
                        : kMinFrameMs;
                    if (back < hold) {
                        return static_cast<int>(i);
                    }
                    back -= hold;
                }
                return 0;
            }
            break;
        }

        case LoopMode::Loop:
        default:
            time %= length;
            break;
    }

    for (int64_t i = 0; i < count; ++i) {
        const int index = cycle.frames[static_cast<size_t>(i)];
        const int64_t hold = inRange(index, frames.size())
            ? clampDuration(frames[static_cast<size_t>(index)].durationMs)
            : kMinFrameMs;
        if (time < hold) {
            return static_cast<int>(i);
        }
        time -= hold;
    }
    return static_cast<int>(count - 1);
}

int frameAt(const std::vector<Frame>& frames, const Cycle& cycle, int64_t elapsedMs) {
    const int position = cyclePositionAt(frames, cycle, elapsedMs);
    if (position < 0 || !inRange(position, cycle.frames.size())) {
        return -1;
    }
    return cycle.frames[static_cast<size_t>(position)];
}

Cycle everyFrame(int frameCount) {
    Cycle cycle;
    cycle.loop = LoopMode::Loop;
    for (int i = 0; i < frameCount && cycle.frames.size() < kMaxFramesPerCycle; ++i) {
        cycle.frames.push_back(i);
    }
    return cycle;
}

// ----------------------------------------------------------- serialisation --

std::string encodeCycles(const std::vector<Cycle>& cycles) {
    std::string out = "lsfast-cycles 1\n";
    for (const Cycle& cycle : cycles) {
        out += escape(cycle.name);
        out += '|';
        out += std::to_string(static_cast<int>(cycle.loop));
        out += '|';
        for (size_t i = 0; i < cycle.frames.size(); ++i) {
            if (i != 0) {
                out += ',';
            }
            out += std::to_string(cycle.frames[i]);
        }
        out += '\n';
    }
    return out;
}

bool decodeCycles(const std::string& text, std::vector<Cycle>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    const std::string header = "lsfast-cycles 1";
    if (text.compare(0, header.size(), header) != 0) {
        return false;
    }

    // +1 for the header line itself, and a hard cap so a pathological file
    // cannot make this walk forever.
    const std::vector<std::string> lines = splitOn(text, '\n', kMaxCycles + 1);
    for (size_t line = 1; line < lines.size(); ++line) {
        if (lines[line].empty()) {
            continue;                 // the trailing newline, and blank lines
        }
        const std::vector<std::string> fields = splitOn(lines[line], '|', 3);
        if (fields.size() < 3) {
            continue;                 // not a cycle; skip it rather than fail
        }

        Cycle cycle;
        cycle.name = trimName(unescape(fields[0]));

        int loop = 0;
        if (!readInt(fields[1], &loop) || loop < 0 || loop > 2) {
            loop = 0;                 // an unknown mode plays as a plain loop
        }
        cycle.loop = static_cast<LoopMode>(loop);

        for (const std::string& piece : splitOn(fields[2], ',', kMaxFramesPerCycle)) {
            int index = 0;
            // Negative indices are dropped here; indices past the end of the
            // frame list are dropped by readCycles, which is the only place
            // that knows how many frames there are.
            if (readInt(piece, &index) && index >= 0) {
                cycle.frames.push_back(index);
            }
        }
        if (cycle.frames.empty()) {
            continue;
        }
        out->push_back(std::move(cycle));
        if (out->size() >= kMaxCycles) {
            break;
        }
    }
    return true;
}

} // namespace fast
