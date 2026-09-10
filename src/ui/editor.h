// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// editor.h — what the interface is holding on to.
//
// Deliberately small. The document is the state; this is only what is needed to
// talk about it: which tool, which layer, what a drag is currently doing. If
// something here could be asked of the document instead, it should be.

#include "app/animation.h"
#include "app/bucket.h"
#include "app/dither.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/shape.h"
#include "ui/canvas_view.h"
#include "ui/file_commands.h"

#include <string>
#include <vector>

namespace fast {

enum class Tool { Pencil, Eraser, Bucket, Picker, Rectangle, Ellipse, Line };

// The corner preview.
//
// Working at 26x, a person loses all sense of what the sprite reads like at the
// size it will actually be seen. The preview shows it at 1x beside the work, and
// the background matters as much as the scale: a sprite that reads perfectly on
// the chequer can vanish against sky blue or lose its outline against black.
// Checking that is the whole point, so the background is a control rather than
// a fixed choice.
struct PreviewSettings {
    bool  visible = true;
    int   scale = 1;                 // whole numbers only, like everything else
    bool  transparent = true;        // the chequer rather than a colour
    float color[4] = { 0.36f, 0.55f, 0.78f, 1.f };   // a sky, to start somewhere
};

// The timeline.
//
// Playback is not a playhead that accumulates: it is a start time, and which
// frame shows is a function of how long ago that was. Scrubbing therefore lands
// on exactly what playing showed, and a dropped UI frame costs nothing rather
// than putting the animation permanently behind.
struct TimelineSettings {
    bool visible = false;
    bool playing = false;
    int  activeFrame = 0;
    int  activeCycle = -1;          // -1 is every frame, in order

    // Milliseconds since SDL started, at the moment play began.
    uint64_t startedAtMs = 0;

    // Onion skin: the frames either side, drawn faint under the live one. It
    // costs nothing here that it would not cost anyway, because both neighbours
    // already have textures in the cache.
    bool onion = false;
    int  onionBefore = 1;
    int  onionAfter  = 1;

    int  renamingFrame = -1;
    char renameBuffer[64] = {};

    // The step of the selected cycle being looked at. A step is not a frame:
    // two steps can name the same frame, so which *step* is selected is a
    // separate question from which frame is, and the strip and the step row
    // answer different halves of it.
    int  selectedStep = 0;
    char cycleNameBuffer[64] = {};
};

struct Editor {
    Document                doc;
    ls::SpriteId            sprite;
    std::vector<PaintLayer> layers;
    int                     activeLayer = 0;

    Tool  tool = Tool::Pencil;
    Tool  toolBeforePicker = Tool::Pencil;   // so the picker can hand control back
    float color[4] = { 0.88f, 0.56f, 0.25f, 1.f };

    // Interaction in progress. Each of these keeps a history bracket open, which
    // is why shortcuts stand aside while any of them is true.
    bool  stroking = false;
    bool  recolouring = false;
    bool  draggingTransform = false;
    bool  draggingDither = false;
    bool  draggingPalette = false;
    bool  editingShape = false;      // a slider in the shape panel

    int   renaming = -1;              // index of the layer being renamed, or -1
    char  renameBuffer[64] = {};
    int   exportScale = 1;
    ls::Vec2i lastPixel { -1, -1 };
    ls::Vec2i hovered { -1, -1 };

    BucketSettings   bucket;
    DitherSettings   dither;
    PreviewSettings  preview;
    TimelineSettings timeline;

    // The document's frames, re-read whenever they can have changed. Held
    // rather than re-read every draw because a panel asks for them several
    // times in one pass, and the list is the authority for nothing -- the
    // document's sprite order is.
    std::vector<Frame> frames;
    std::vector<Cycle> cycles;

    // A shape being dragged out. It exists from the press: the shape is created
    // immediately and then driven as the mouse moves, so what is on the canvas
    // during the drag is the real thing rather than a preview that has to agree
    // with it afterwards.
    bool       draggingShape = false;
    ShapeLayer pendingShape;
    ls::Vec2f  shapeAnchor;
    float      shapeCorner = 0.f;

    FileState   files;
    bool        quitRequested = false;
    std::string lastTitle;
    std::string status = "Ready";

    PaintLayer* active() {
        if (activeLayer < 0 || activeLayer >= static_cast<int>(layers.size())) {
            return nullptr;
        }
        return &layers[static_cast<size_t>(activeLayer)];
    }

    // True while any interaction holds a history bracket open. Shortcuts stand
    // aside for all of them: acting on one mid-drag would step over an entry
    // that has not been committed yet.
    bool busy() const {
        return stroking || recolouring || draggingTransform || draggingDither ||
               draggingPalette || editingShape || draggingShape;
    }

    // The frame being edited, which is the sprite every tool draws into. Falls
    // back to the document's first frame, so a stale index can never point a
    // tool at nothing.
    ls::SpriteId activeSprite() const {
        if (timeline.activeFrame >= 0 &&
            timeline.activeFrame < static_cast<int>(frames.size())) {
            return frames[static_cast<size_t>(timeline.activeFrame)].sprite;
        }
        return sprite;
    }

    void say(const std::string& message);
};

ls::Color toColor(const float rgba[4]);
void      fromColor(ls::Color colour, float rgba[4]);

// Rebuilds the layer list from the document.
//
// The interface holds handles; undo, redo and open all change what exists. Undo
// restores ids exactly, so re-adopting gives back the same handles -- but an
// undone "Add layer" would otherwise leave the panel holding one that is gone,
// and drawing on it would fail silently.
void resyncLayers(Editor& editor);

// Points the colour control at the active layer, so it shows what that layer is
// rather than what was last typed.
void syncColorFromLayer(Editor& editor);

bool newDocument(Editor& editor, uint32_t size);

// Re-reads the frame and cycle lists from the document and clamps the active
// frame to them. Called after anything that can change what frames exist --
// adding, deleting, reordering, undo, redo, opening a file.
void resyncFrames(Editor& editor);

// Points the editor at a frame: clamps the index, re-adopts that frame's
// layers, and puts the colour control on the layer that is now active.
void selectFrame(Editor& editor, int index);

// Where playback is now. Returns the frame index to show, which is the active
// frame when nothing is playing.
int frameToShow(const Editor& editor, uint64_t nowMs);

// Points the editor at a cycle, or at -1 for every frame in order. Stops
// playback, because which cycle is playing changing under a running clock is
// a jump with no cause a person can see.
void selectCycle(Editor& editor, int index);

// The cycle currently driving playback: the selected one, or every frame in
// order when none is selected.
Cycle activeCycle(const Editor& editor);

} // namespace fast
