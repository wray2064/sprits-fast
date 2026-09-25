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
#include "app/canvas_ops.h"
#include "app/bucket.h"
#include "app/dither.h"
#include "app/element.h"
#include "app/export_anim.h"
#include "app/floating.h"
#include "app/ink.h"
#include "app/grid_snap.h"
#include "app/keymap.h"
#include "app/preferences.h"
#include "app/layers.h"
#include "app/library.h"
#include "app/paint.h"
#include "app/recovery.h"
#include "app/reference.h"
#include "app/palette.h"
#include "app/shape.h"
#include "app/selection.h"
#include "app/sheet.h"
#include "app/text.h"
#include "ui/canvas_view.h"
#include "ui/file_commands.h"

#include <functional>
#include <string>
#include <vector>

namespace fast {

enum class Tool { Pencil, Eraser, Bucket, Picker, Rectangle, Ellipse, Line,
                  Select, SelectEllipse, Lasso, Wand, Move,
                  Spray, Contour, Hand, Zoom, Gradient, Text, PolygonLasso };

// The tools that make a selection rather than a mark.
inline bool isSelectionTool(Tool tool) {
    return tool == Tool::Select || tool == Tool::SelectEllipse ||
           tool == Tool::Lasso || tool == Tool::Wand || tool == Tool::PolygonLasso;
}

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

    // How fast playback runs against the holds: a preview at half speed to
    // judge the timing, or double to see the motion.
    float speed = 1.f;

    // A run of frames selected with Shift+click, from here to the active
    // frame; -1 when only the active frame is selected. The strip's buttons
    // act on the whole run, and Play loops it when no cycle is chosen.
    int rangeAnchor = -1;

    // Onion skin: the frames either side, drawn faint under the live one. It
    // costs nothing here that it would not cost anyway, because both neighbours
    // already have textures in the cache.
    bool onion = false;
    int  onionBefore = 1;
    int  onionAfter  = 1;
    // Which neighbours: the frame list's, or the selected cycle's steps --
    // what plays either side of this frame, which is what an animator is
    // matching. And whether the ends wrap, for a loop drawn as one.
    bool onionInCycle = false;
    bool onionWraps = false;
    float onionBehind[4] = { 1.f, 0.55f, 0.35f, 1.f };
    float onionAhead[4]  = { 0.47f, 0.78f, 1.f, 1.f };

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

    // The two inks: what the left button paints with and what the right one
    // does, as in every pixel editor. Each is a colour and, when it came from
    // the palette, the slot -- so a pixel painted with it follows the slot.
    // The colour is kept as floats because that is what the picker edits.
    float         color[4]     = { 0.88f, 0.56f, 0.25f, 1.f };
    ls::ColorRole inkRole      = ls::kColorRoleNone;
    float         backColor[4] = { 0.10f, 0.10f, 0.12f, 1.f };
    // The shade strip's colour: the current colour, except that picking one
    // of its own shades does not move it, so the strip holds still while it
    // is being picked from.
    ls::Color shadeBase { 92, 140, 199, 255 };
    ls::Color shadePicked { 0, 0, 0, 0 };
    ls::ColorRole backRole     = ls::kColorRoleNone;

    // The stroke in progress: which element gains the pixels and which lose
    // them, resolved once on press. And which button pressed, since the right
    // one paints the background ink.
    InkStroke inkStroke;
    bool      strokeWithBack = false;

    // The selection, and pixels on the move.
    //
    // A float holds a history action open from the lift until it is dropped,
    // so a move is one undo step however long it was dragged and nudged for.
    // It is dropped by Enter, by a click anywhere off the artwork, by any
    // shortcut that is not about it, and by switching tools; Escape abandons
    // it and puts the pixels back.
    Selection        selection;
    Floating         floating;
    bool             draggingFloat = false;
    ls::Vec2i        floatGrab { 0, 0 };        // the pixel the drag started on
    ls::Vec2i        floatGrabOffset { 0, 0 };  // where the float was then
    ls::Vec2i        floatGrabCorner { 0, 0 };  // its top-left corner then, for snapping
    bool             selecting = false;         // a marquee or lasso mid-drag
    SelectMode       selectMode = SelectMode::Replace;
    ls::Vec2i        selectAnchor { 0, 0 };
    ls::Vec2f        selectAnchorExact { 0.f, 0.f };   // the same, between pixels
    // Marquees, moves and shapes land on the tile grid. The grid itself is
    // the canvas view's; the editor holds a copy each frame for the tools.
    bool             snapToGrid = false;
    Grid             snapGrid;
    std::vector<ls::Vec2i> lassoPoints;
    // A polygon lasso between clicks: its corners so far. Unlike a drag it is
    // not busy -- the button is up between corners -- but the keys that close
    // or abandon it come to it first.
    bool                   drawingPolygon = false;
    ls::IntervalSet  selectPreview;             // the shape being dragged out
    BucketSettings   wand;                      // the magic wand's own settings
    PixelClip        pixelClip;

    // A custom brush: pixels taken from a selection, stamped by the pencil
    // colour by colour -- in their own colours, or all in the current one.
    // The strokes are one per colour, made as the stroke begins.
    PixelClip              customBrush;
    bool                   customBrushOn = false;
    bool                   customBrushOwnColours = true;
    std::vector<InkStroke> brushStrokes;
    bool             clipHoldsPixels = false;   // the last copy was pixels, not a layer
    // Whether copies go onto the system clipboard too, and pastes look there
    // for another program's image. Off for headless runs, which must not
    // overwrite the clipboard of whoever runs them.
    bool             systemClipboard = true;
    bool             canvasHovered = false;     // last frame's, for a click elsewhere

    // A dithered element picked in the element list can be painted into as
    // itself, so the pencil lays down the dither rather than a flat colour.
    // Off unless asked for: selecting an element to look at it should not
    // change what the pencil does.
    bool      paintIntoElement = false;

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

    // The layer properties window: which layer, and its name and notes as
    // they are being typed (read in when the window opens on a layer).
    ls::LayerId propertiesLayer;
    ls::LayerId propertiesLoaded;
    bool  draggingLayerProperties = false;
    char  propertiesName[64] = {};
    char  propertiesNotes[1024] = {};

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

    // A whole-palette adjustment in progress: the palette as it was when the
    // window opened, so dragging a slider back to zero returns it exactly.
    struct PaletteAdjust {
        std::vector<PaletteEntry> base;
        float hue = 0.f;
        float saturation = 0.f;
        float lightness = 0.f;
    } paletteAdjust;
    ls::PaletteId renamingPalette;
    char          paletteNameBuffer[64] = {};
    int   exportScale = 1;
    bool  exportIndexed = false;    // PNG export as a colour table and indices

    // A sprite sheet chosen for import, waiting for its grid to be said. The
    // picture is held decoded so the window can say how many frames each
    // grid would make before anything is replaced.
    struct SheetImport {
        bool             open = false;
        std::string      path;
        ls::RasterBuffer picture;
        int              cellWidth = 32;
        int              cellHeight = 32;
        int              holdMs = 100;
    } sheetImport;

    // The canvas size window: the new size and which way the drawing is
    // pinned while the canvas grows or shrinks around it.
    struct CanvasSizeDialog {
        bool         open = false;
        int          width = 32;
        int          height = 32;
        CanvasAnchor anchor = CanvasAnchor::Centre;
    } canvasDialog;

    // The sprite size window: the size being chosen, in pixels or percent,
    // and whether the two sides keep their proportion.
    struct SpriteSizeDialog {
        bool  open = false;
        int   width = 32;
        int   height = 32;
        float percentX = 100.f;
        float percentY = 100.f;
        bool  lockRatio = true;
    } spriteDialog;

    // The animation export, and whether its window is up. Like the sheet's,
    // kept here so the choices survive the window closing.
    bool              animationPanelOpen = false;
    bool              animationFromCycle = true;
    AnimationSettings animation;

    // Whether the New document window is up.
    bool newDocumentOpen = false;

    // The person's keys and preferences, read at start and written as they
    // change; the Preferences window, and the command whose key is being
    // given (with which of its chords, or -1 for a new one).
    Keymap      keys;
    Preferences prefs;
    bool        preferencesOpen = false;
    bool        preferencesShowKeys = false;   // open on the Keys tab, once
    std::string rebinding;
    int         rebindingChord = -1;

    // Whether the history window is up.
    bool historyOpen = false;

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

    // Drawing on one side draws on the other. The axes are doubled pixel
    // positions (see brush.h), -1 meaning the canvas's own middle, so a new
    // document or a resized one needs nothing set.
    bool             symmetryAcross = false;
    bool             symmetryDown = false;
    int              symmetryAxisX = -1;
    int              symmetryAxisY = -1;

    // What a pencil or spray stroke is allowed to touch, and with what (see
    // ink.h), and that stroke's own view of the layer, taken as it began.
    InkMode      inkMode = InkMode::Simple;
    InkModeState inkModeState;

    // The spray: its reach, how many pixels a frame, and a count that seeds
    // each burst, so a stroke is the same stroke however it is replayed.
    int      sprayRadius = 6;
    int      sprayDensity = 10;
    uint32_t sprayBursts = 0;

    // Text being placed: where it was clicked, what it will say, how large.
    bool      textOpen = false;
    ls::Vec2i textAt { 0, 0 };
    char      textBuffer[kMaxTextLength + 1] = {};
    int       textScale = 1;

    // A gradient mid-drag: the element made on the press, driven by the drag.
    bool           drawingGradient = false;
    PaintLayer     gradientElement;
    DitherSettings gradientSettings;

    // A contour mid-drag: the outline so far, filled on release.
    bool                   drawingContour = false;
    std::vector<ls::Vec2i> contourPoints;

    // Where the last stroke ended, so Shift+click draws a straight line from
    // it -- the pixel editor's oldest trick for a clean straight edge.
    ls::Vec2i        lastStrokeEnd { -1, -1 };
    PixelPerfect     pixelPerfect;       // the stroke in progress, filtered
    Stabiliser       stabiliser;         // and smoothed, when the string is on

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
    // Rectangles and ellipses drawn as their edge rather than their area.
    bool       shapeOutline = false;
    float      shapeOutlineWidth = 1.f;

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
               draggingPalette || editingShape || draggingShape || draggingLayer ||
               draggingLayerProperties ||
               selecting || draggingFloat || drawingContour || drawingGradient;
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

// The freehand element the element panel is editing, as a PaintLayer the
// dither and colour helpers take: the selected element when it is pixels, and
// otherwise the layer's first pixels. False when the layer has none.
bool selectedPixels(Editor& editor, PaintLayer* out);

// Reads the keys and preferences from the settings folder, and writes them
// back. Writing is atomic; a missing folder means nothing is kept, silently.
void loadSettings(Editor& editor);
void saveSettings(const Editor& editor);

// The symmetry in force, with the axes resolved against the canvas.
Symmetry symmetryNow(Editor& editor);

// The inks as values the engine takes, and setting them from one.
Ink  foregroundInk(const Editor& editor);
Ink  backgroundInk(const Editor& editor);
void setForegroundInk(Editor& editor, const Ink& ink);
void setBackgroundInk(Editor& editor, const Ink& ink);
void swapInks(Editor& editor);

// Brings an ink that names a slot up to date with the slot: after a palette
// edit, a swap, a frame with a palette of its own, or undo. An ink whose slot
// has gone keeps its colour and stops naming it, rather than painting pixels
// that point at nothing.
void refreshInks(Editor& editor);

bool newDocument(Editor& editor, uint32_t size);
bool newDocument(Editor& editor, const FileState::NewDocument& spec);

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
void mergeActiveLayerDown(Editor& editor, CanvasView& canvas);
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

// --- the selection, as the tools, the menu and the shortcuts all drive it ------
//
// None touches ImGui, so the self-test can drive them.

void selectAll(Editor& editor);
void deselect(Editor& editor);        // drops a float first
void reselect(Editor& editor);
void invertSelection(Editor& editor);

// Starts the selected pixels floating -- or, with nothing selected, everything
// on the active layer -- and opens the history action the float holds. Says
// why when it cannot: a locked layer, a transformed one, nothing there.
bool liftSelection(Editor& editor, const char* label);

// Drops the float into its layer and closes its action; or abandons it,
// putting everything back as it was.
void settleFloating(Editor& editor);
void cancelFloating(Editor& editor);

// Moves or turns the selected pixels, lifting them first if they are not
// already floating. The float stays up afterwards, so a run of nudges is one
// move.
bool nudgeSelection(Editor& editor, ls::Vec2i by);
bool turnSelection(Editor& editor, FloatTurn turn);

// The clipboard, for pixels. Copy and cut need a selection; paste floats what
// was copied where it came from, ready to be dragged into place.
bool copySelectionPixels(Editor& editor);
bool cutSelectionPixels(Editor& editor);
bool deleteSelectionPixels(Editor& editor);
bool pastePixels(Editor& editor);

// When another program's image is the newest thing on the clipboard, makes it
// the clip a paste will use, and says what it held in `note`. False when the
// clip is Fast's own, or the clipboard holds no image.
bool adoptSystemClipboard(Editor& editor, std::string* note);

// Whether a paste has pixels to paste: Fast's own, or another program's image.
bool pixelsToPaste(Editor& editor);

// Copy: the selected pixels while there is a selection, the layer otherwise.
// Paste: another program's image if that is the newest thing on the
// clipboard, then Fast's own pixels, then a copied layer (not as a layer).
void copyCommand(Editor& editor);
void pasteCommand(Editor& editor, CanvasView& canvas, bool asLayer);
// The selected pixels of the active layer become the pencil's brush.
bool brushFromSelection(Editor& editor);
// The same, onto a new layer of its own above the active one.
bool pastePixelsAsLayer(Editor& editor);

// Changes the canvas through one of canvas_ops' calls, then tidies what the
// window holds about the old one: the selection, the view, the layer list.
// `change` returns false with a reason, which is said.
bool changeCanvas(Editor& editor, CanvasView& canvas,
                  const std::function<bool(std::string*)>& change, const std::string& done);

// The canvas's own bounds as a mask, for select-all and invert.
ls::IntervalSet canvasBounds(Editor& editor);

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

// The cycle currently driving playback: the selected one, or the selected run
// of frames, or every frame in order.
Cycle activeCycle(const Editor& editor);

// The run of frames selected in the strip, first to last. False when only one
// frame is selected.
bool frameRange(const Editor& editor, int* first, int* last);

// Which frames a sheet would contain, in cell order: the selected cycle's steps,
// or every frame. A cycle that plays a frame twice therefore produces two cells,
// which is the useful behaviour -- the sheet plays by stepping through its cells
// in order, which is all a consumer wants to do.
std::vector<int> sheetSteps(const Editor& editor);

} // namespace fast
