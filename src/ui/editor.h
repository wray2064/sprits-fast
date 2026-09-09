// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// editor.h — what the interface is holding on to.
//
// Deliberately small. The document is the state; this is only what is needed to
// talk about it: which tool, which layer, what a drag is currently doing. If
// something here could be asked of the document instead, it should be.

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

    BucketSettings bucket;
    DitherSettings dither;

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

} // namespace fast
