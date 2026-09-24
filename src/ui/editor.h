// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// editor.h — what the interface is holding on to.
//
// Deliberately small. The document is the state; this is only what is needed to
// talk about it: which tool, which layer, what a drag is currently doing. If
// something here could be asked of the document instead, it should be.

#include "app/animation.h"
#include "app/brush.h"
#include "app/bucket.h"
#include "app/dither.h"
#include "app/element.h"
#include "app/layers.h"
#include "app/library.h"
#include "app/paint.h"
#include "app/recovery.h"
#include "app/reference.h"
#include "app/palette.h"
#include "app/shape.h"
#include "app/sheet.h"
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
    bool  draggingLayer = false;     // the opacity slider in the layer panel

    int   renaming = -1;              // index of the layer being renamed, or -1
    char  renameBuffer[64] = {};

    // The stack. Selection is a set, because grouping wants several; the
    // active layer is the one tools draw on and is always in the set. A group
    // row can be selected instead, and then the properties strip edits the
    // group. The clipboard is a handle: copy remembers, paste clones -- undo
    // restores ids exactly, so a copied layer that was undone away and back
    // still pastes.
    std::vector<ls::LayerId> selectedLayers;
    ls::GroupId              activeGroup;
    ls::GroupId              renamingGroup;
    char                     groupNameBuffer[64] = {};
    std::vector<uint64_t>    collapsedGroups;
    ls::LayerId              clipboard;

    // A palette slot mid-rename, and one waiting for delete to be confirmed
    // because something paints through it.
    ls::ColorRole renamingSlot = ls::kColorRoleNone;
    ls::ColorRole confirmRemoveSlot = ls::kColorRoleNone;
    char  slotNameBuffer[64] = {};

    // The palette section of the panel: the quick row is always there, the
    // list of palettes opens for the rarer things -- rename, copy, delete,
    // and giving a frame a palette of its own.
    bool          palettesOpen = false;
    ls::PaletteId renamingPalette;
    char          paletteNameBuffer[64] = {};
    int   exportScale = 1;

    // The sheet export, and whether its window is up. Kept on the editor rather
    // than in the panel so the choices survive the dialog being opened and
    // closed -- a person setting up a sheet usually writes several.
    bool          sheetPanelOpen = false;
    bool          sheetFromCycle = true;    // the selected cycle, or every frame
    SheetSettings sheet;
    ls::Vec2i lastPixel { -1, -1 };
    ls::Vec2i hovered { -1, -1 };

    BucketSettings   bucket;
    BrushSettings    brush;
    PixelPerfect     pixelPerfect;       // the stroke in progress, filtered

    // A stylus, as SDL reports it beside the mouse events it also sends. The
    // mouse path draws; this says how hard, and which end. The eraser end
    // swaps the tool for as long as it touches, the way a real pencil does.
    struct Pen {
        bool  seen = false;              // one has been near the window
        bool  down = false;
        bool  eraser = false;
        float pressure = 1.f;
    } pen;
    Tool toolBeforeEraserTip = Tool::Pencil;
    bool eraserTipHeld = false;

    DitherSettings   dither;
    // Which end of a dithered layer's ramp a palette click assigns to. A
    // dithered layer has two colours where a solid one has one, so clicking a
    // slot has to know which it is for.
    int              rampEnd = 0;         // 0 dark, 1 light
    PreviewSettings  preview;
    TimelineSettings timeline;

    // The document's frames, re-read whenever they can have changed. Held
    // rather than re-read every draw because a panel asks for them several
    // times in one pass, and the list is the authority for nothing -- the
    // document's sprite order is.
    std::vector<Frame> frames;
    std::vector<Cycle> cycles;

    // The element of the active layer the shape panel edits, by the operation
    // that draws it. Null means the layer's freehand pixels, or whichever
    // element is first. Set when a shape is drawn, so the panel shows the
    // thing just made.
    ls::OperationId activeElement;

    // Where a shape tool puts what it draws: on the active layer, as one more
    // of its elements -- so a belt made of a rectangle, a line and a few
    // pixels is one layer -- or on a layer of its own, the old way, for
    // someone who wants every shape listed in the stack.
    bool shapesOnOwnLayer = false;

    // A shape being dragged out. It exists from the press: the shape is created
    // immediately and then driven as the mouse moves, so what is on the canvas
    // during the drag is the real thing rather than a preview that has to agree
    // with it afterwards.
    bool       draggingShape = false;
    ShapeLayer pendingShape;
    ls::Vec2f  shapeAnchor;
    float      shapeCorner = 0.f;

    // The references this document holds, re-read whenever they can have
    // changed, and which one the panel is editing.
    std::vector<Reference> references;
    std::string            activeReference;      // an id, or empty
    bool                   draggingReference = false;
    ls::Vec2f              referenceGrabbed { 0.f, 0.f };

    // The two library folders, and whether the window is up.
    LibraryFolders           libraryFolders;
    bool                     libraryOpen = false;
    bool                     libraryShowingReferences = false;
    std::vector<LibraryDocument> libraryDocuments;
    std::vector<LibraryImage>    libraryImages;
    bool                     libraryStale = true;   // the folder needs re-reading

    // The safety net. A copy of unsaved work, written to the settings folder
    // every couple of minutes and deleted the moment the work is safe -- so a
    // copy still there at startup is proof the last session ended badly.
    RecoverySession recovery;
    bool            autosaveOn = true;
    std::vector<RecoveredWork> recovered;    // found at startup, waiting to be asked about
    bool            askingToRecover = false;

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
               draggingPalette || editingShape || draggingShape || draggingLayer;
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

// Forgets every interaction that was about the previous document: a layer or
// frame mid-rename, a palette slot waiting for its removal to be confirmed, a
// selected cycle step, playback. Called when the document is replaced -- New,
// Open -- because each of these is an index or a handle into a document that
// no longer exists, and an Enter pressed afterwards would land it on whatever
// now sits at that index.
void forgetInteraction(Editor& editor);

// A blank frame after `index`, with one layer in the current colour so it can
// be drawn on at once. A sprite with no layers is a frame the pencil does
// nothing to, silently -- the document makes its first frame with a layer for
// the same reason, and a frame the timeline adds gets the same courtesy. One
// history entry for the pair. Returns the new frame's index, or -1.
int addEmptyFrame(Editor& editor, int index);

// Re-reads the frame and cycle lists from the document and clamps the active
// frame to them. Called after anything that can change what frames exist --
// adding, deleting, reordering, undo, redo, opening a file.
void resyncFrames(Editor& editor);

// Re-reads the document's references and drops textures for any that are
// gone. Called after an import, a removal, undo, redo and opening a file.
void resyncReferences(Editor& editor, CanvasView& canvas);

// The reference the panel is editing, or null.
Reference* activeReference(Editor& editor);

// Re-reads whichever library folder the window is showing.
void refreshLibrary(Editor& editor);

// Does something that replaces or ends the document, asking about unsaved
// work first. Defined in app_window.cpp, declared here because the library
// panel opens files too and must go through the same question.
void requestAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                   PendingAction action, const std::string& path = {});

// --- the stack, as the panel and the shortcuts both drive it ------------------
//
// Each keeps the active layer pointing at the same layer afterwards, re-adopts
// the list, and says what happened. None touches ImGui, so the self-test can
// drive them.

// Points the editor at a layer by handle, after anything that rebuilt the list.
void selectLayer(Editor& editor, ls::LayerId layer, bool extend = false);
bool layerSelected(const Editor& editor, ls::LayerId layer);

void duplicateActiveLayer(Editor& editor, CanvasView& canvas);
void copyActiveLayer(Editor& editor);
void pasteLayerHere(Editor& editor, CanvasView& canvas);
void deleteSelectedLayers(Editor& editor, CanvasView& canvas);
void raiseActiveLayer(Editor& editor, CanvasView& canvas);
void lowerActiveLayer(Editor& editor, CanvasView& canvas);
void groupSelectedLayers(Editor& editor, CanvasView& canvas);
void ungroupActiveLayer(Editor& editor, CanvasView& canvas);
void toggleActiveLayerClip(Editor& editor, CanvasView& canvas);
void toggleActiveLayerLock(Editor& editor);

// True when the active layer is locked: tools ask before drawing, and say so.
bool activeLayerLocked(Editor& editor);

// The swap, as one call: binds the document to `palette`, says so, and drops
// every cached frame, because every frame without a palette of its own just
// changed. Used by the panel's quick row and by the shortcut.
void swapPalette(Editor& editor, CanvasView& canvas, ls::PaletteId palette);

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

// Which frames a sheet would contain, in cell order: the selected cycle's steps,
// or every frame. A cycle that plays a frame twice therefore produces two cells,
// which is the useful behaviour -- the sheet plays by stepping through its cells
// in order, which is all a consumer wants to do.
std::vector<int> sheetSteps(const Editor& editor);

} // namespace fast
