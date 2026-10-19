// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// self_test.cpp — the window's bookkeeping, driven without the window (--self-test).

#include "ui/app_internal.h"

#include "app/palette_tools.h"
#include "app/tracks.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/transform.h"
#include "ui/layout.h"
#include "ui/shape_tools.h"
#include "ui/tabs.h"
#include "ui/panels.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fast {

// The state management the interface does, driven without the interface.
// resyncLayers and the undo/open glue around it are not reachable from a
// fast_core test, because they are the window's own bookkeeping -- and all of
// it was wrong once.
int runSelfTest() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* what) {
        if (!condition) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    };

    Editor editor;
    check(newDocument(editor, 16), "new document");
    check(editor.layers.size() == 1, "one layer to start");
    check(!paletteEntries(editor.doc).empty(), "a new document has a palette");

    editor.doc.beginAction("Pencil");
    paintPixels(editor.doc, *editor.active(), linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // Add a layer, then undo it. The panel must not be left holding a layer
    // that no longer exists -- this was the bug.
    PaintLayer added;
    check(createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                           ls::Color{40, 80, 220, 255}, &added), "add layer");
    editor.layers.push_back(added);
    check(editor.layers.size() == 2, "two layers");

    check(editor.doc.undo(), "undo the added layer");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "panel drops the undone layer");
    check(editor.activeLayer == 0, "active index stays in range");
    check(editor.active() != nullptr, "still something to draw on");

    check(editor.doc.canRedo(), "the undone layer can be redone");
    check(editor.doc.redo(), "redo");
    resyncLayers(editor);
    check(editor.layers.size() == 2, "the redone layer reappears");

    check(editor.doc.undo(), "undo it again");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "and goes away again");

    // Drawing after an undo discards the redo branch, as every editor does.
    editor.doc.beginAction("Pencil");
    check(paintPixels(editor.doc, *editor.active(), {{7, 7}}), "draw after undo");
    editor.doc.endAction();
    check(!editor.doc.canRedo(), "a new action drops the redo branch");

    // The palette drives the layer, and an ink through the same slot reads
    // it back: the colour control shows what the slot now means.
    const ls::ColorRole role = 8;
    check(setLayerRole(editor.doc, *editor.active(), role), "use a palette slot");
    check(setPaletteEntry(editor.doc, role, ls::Color{12, 200, 90, 255}), "set the slot");
    editor.inkRole = role;
    refreshInks(editor);
    check(toColor(editor.color).g == 200, "the control shows the palette colour");
    editor.inkRole = ls::kColorRoleNone;

    check(setPaintColor(editor.doc, *editor.active(),
                        effectiveLayerColor(editor.doc, editor.sprite, *editor.active())),
          "keep what is on screen");
    check(setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone), "detach");
    check(layerRole(editor.doc, *editor.active()) == ls::kColorRoleNone, "detached");

    // ------------------------------------------------------------- inks --
    //
    // Two colours on one layer through the window's own path: the left ink,
    // then the right one, into the same layer, each its own element; X swaps
    // them; and a dithered element is painted into only when asked.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer for two inks");
        if (layer != nullptr) {
            const ls::LayerId target = layer->layer;
            const size_t before = elementsOf(editor.doc, target).size();
            setForegroundInk(editor, Ink{ ls::Color{ 250, 10, 10, 255 }, ls::kColorRoleNone });
            setBackgroundInk(editor, Ink{ ls::Color{ 10, 10, 250, 255 }, ls::kColorRoleNone });

            InkStroke stroke;
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, false, &stroke), "the left ink begins");
            check(strokeInk(editor.doc, stroke, {{ 20, 20 }}), "and paints");
            editor.doc.endAction();
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, true, &stroke), "the right ink begins");
            check(strokeInk(editor.doc, stroke, {{ 21, 20 }}), "and paints");
            editor.doc.endAction();
            resyncLayers(editor);

            check(elementsOf(editor.doc, target).size() == before + 2,
                  "two colours, two elements, one layer");
            check(elementsWithInk(editor.doc, target, foregroundInk(editor)).size() == 1,
                  "the left colour has its element");
            swapInks(editor);
            check(toColor(editor.color).b == 250 && toColor(editor.backColor).r == 250,
                  "X swaps the two");
            swapInks(editor);

            check(editor.doc.undo() && editor.doc.undo(), "two strokes, two undos");
            resyncLayers(editor);
            check(elementsOf(editor.doc, target).size() == before,
                  "and both colours' elements go with them");
        }
    }

    // -------------------------------------------------------- selection --
    //
    // The commands the menu, the tools and the keys share: a move is one
    // undo step however it was nudged; Escape puts it back; a shortcut drops
    // it; copy and paste carry pixels, not a layer, while something is
    // selected; select-all and invert are about the canvas.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer to select on");
        if (layer != nullptr) {
            const ls::LayerId target = layer->layer;
            setForegroundInk(editor, Ink{ ls::Color{ 9, 200, 9, 255 }, ls::kColorRoleNone });
            InkStroke stroke;
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, false, &stroke) &&
                  strokeInk(editor.doc, stroke, {{ 2, 2 }, { 3, 2 }}), "two pixels to move");
            editor.doc.endAction();
            resyncLayers(editor);
            const std::string before = editor.doc.undoLabel();

            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(nudgeSelection(editor, { 1, 0 }), "nudge lifts and moves");
            check(nudgeSelection(editor, { 1, 0 }), "and again");
            check(editor.floating.active() && editor.floating.offset.x == 2,
                  "two nudges, one float, two pixels along");
            check(editor.selection.contains({ 4, 2 }) && !editor.selection.contains({ 2, 2 }),
                  "the ants follow");
            settleFloating(editor);
            check(!editor.floating.active(), "dropped");
            check(editor.doc.undoLabel() == "Move", "a run of nudges is one Move");
            check(editor.doc.undo(), "and one undo takes it back");
            resyncLayers(editor);
            check(editor.doc.undoLabel() == before, "to exactly before the move");

            // Escape: nothing happened.
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(nudgeSelection(editor, { 0, 3 }), "lift for escape");
            cancelFloating(editor);
            check(!editor.floating.active() && editor.doc.undoLabel() == before,
                  "Escape leaves no history entry");
            check(editor.selection.contains({ 2, 2 }), "and the selection is where it was");

            // A brush from the selection: the pixels, colour by colour, and
            // the pencil in hand to stamp them.
            editor.tool = Tool::Select;
            check(brushFromSelection(editor), "a brush from the selected pixels");
            check(editor.tool == Tool::Pencil && editor.customBrushOn &&
                  editor.customBrush.pieces.size() == 1 &&
                  ls::geom::pixelCount(editor.customBrush.mask) == 2, "two pixels, one colour");
            editor.customBrushOn = false;

            // Copy and paste carry pixels while something is selected.
            check(copySelectionPixels(editor) && editor.clipHoldsPixels, "copy pixels");
            check(pastePixels(editor) && editor.floating.active(), "paste floats them");
            check(editor.tool == Tool::Move, "with the move tool in hand");
            check(nudgeSelection(editor, { 0, 5 }), "place the paste");
            settleFloating(editor);
            check(editor.doc.undoLabel() == "Paste", "a paste is one Paste");
            check(editor.doc.undo(), "undo the paste");
            resyncLayers(editor);

            // Delete clears; select-all and invert are canvas-wide.
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(deleteSelectionPixels(editor), "delete");
            check(editor.doc.undoLabel() == "Delete", "one Delete");
            check(editor.doc.undo(), "undo the delete");
            resyncLayers(editor);
            selectAll(editor);
            check(ls::geom::pixelCount(editor.selection.mask) ==
                  ls::geom::pixelCount(canvasBounds(editor)), "select all is the canvas");
            invertSelection(editor);
            check(editor.selection.empty(), "the inverse of everything is nothing");
            editor.selection.mask = rectangleMask({ 0, 0 }, { 1, 1 });
            deselect(editor);
            check(editor.selection.empty(), "deselect");
            reselect(editor);
            check(ls::geom::pixelCount(editor.selection.mask) == 4, "reselect brings it back");
            deselect(editor);

            // A locked layer refuses to lift.
            setLayerLocked(editor.doc, target, true);
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(!nudgeSelection(editor, { 1, 0 }) && !editor.floating.active(),
                  "a locked layer's pixels stay put");
            setLayerLocked(editor.doc, target, false);
            deselect(editor);
            editor.tool = Tool::Pencil;

            check(editor.doc.undo(), "undo the two pixels");
            resyncLayers(editor);
        }
    }

    // ------------------------------------------------------------ frames --
    //
    // The window's own bookkeeping, none of which a fast_core test can reach:
    // that selecting a frame changes which layers the panel holds, and that a
    // tool then acts on the frame being looked at rather than on the first one.

    check(editor.frames.size() == 1, "a new document is one frame");
    check(editor.timeline.activeFrame == 0, "on the first frame");

    check(duplicateFrame(editor.doc, 0) == 1, "duplicate the frame");
    resyncFrames(editor);
    check(editor.frames.size() == 2, "the strip sees two frames");

    selectFrame(editor, 1);
    check(editor.timeline.activeFrame == 1, "the second frame is selected");
    check(editor.activeSprite() == editor.frames[1].sprite,
          "the active sprite is the selected frame");
    check(editor.active() != nullptr, "the second frame has a layer to draw on");

    // The one that matters: paint through the interface's own handles and only
    // the selected frame may change.
    {
        const auto pixelsOf = [&](ls::SpriteId sprite) {
            const ls::CompileProfile profile =
                compileProfile(ls::CompileProfileType::Export, 16, 16);
            auto compiled = editor.doc.engine().compileSprite(sprite, profile);
            return compiled.ok() ? compiled.value.raster.pixels
                                 : std::vector<uint8_t>{};
        };
        const std::vector<uint8_t> firstBefore = pixelsOf(editor.frames[0].sprite);

        editor.doc.beginAction("Pencil");
        check(paintPixels(editor.doc, *editor.active(), linePixels({11, 2}, {11, 13})),
              "draw on the second frame");
        editor.doc.endAction();

        check(pixelsOf(editor.frames[0].sprite) == firstBefore,
              "the first frame did not change");
        check(pixelsOf(editor.frames[1].sprite) != firstBefore,
              "the second frame did");
    }

    // Selecting back must give back the first frame's layers, not the second's.
    selectFrame(editor, 0);
    check(editor.activeSprite() == editor.frames[0].sprite, "back on the first frame");

    // Playing shows the frame the clock says, and does not move the selection.
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    check(frameToShow(editor, 0) == 0, "playback starts at the beginning");
    check(frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) + 5) == 1,
          "and moves on when the frame's time is up");
    check(editor.timeline.activeFrame == 0, "playing does not change the selection");
    // It is a function of time, so the same point of a later pass is the same
    // picture. Two frames of the default hold, so the cycle is twice that.
    {
        const uint64_t cycle = static_cast<uint64_t>(kDefaultFrameMs) * 2;
        const uint64_t at = static_cast<uint64_t>(kDefaultFrameMs) + 5;
        check(frameToShow(editor, at) == 1, "the second frame at that point");
        check(frameToShow(editor, at + cycle * 10) == frameToShow(editor, at),
              "the same point of a later pass is the same frame");
    }
    editor.timeline.playing = false;

    // An undone frame must not leave the strip holding one that is gone.
    selectFrame(editor, 1);
    check(editor.doc.undo(), "undo the drawing on frame two");
    check(editor.doc.undo(), "undo the frame itself");
    resyncFrames(editor);
    check(editor.frames.size() == 1, "the strip drops the undone frame");
    check(editor.timeline.activeFrame == 0, "and the selection comes back in range");
    resyncLayers(editor);
    check(editor.active() != nullptr, "still something to draw on");

    // The last frame cannot be deleted, so the editor can never reach a state
    // with nothing to draw on.
    check(!deleteFrame(editor.doc, 0), "the last frame stays");

    // -------------------------------------------------- the seams between --
    //
    // Each feature above holds on its own. These are the places where two of
    // them meet in the window's bookkeeping, and each was a way to leave an
    // index pointing at something that had gone.

    // An empty frame is one the pencil can draw on at once.
    check(addEmptyFrame(editor, 0) == 1, "add an empty frame");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(editor.active() != nullptr, "an empty frame has a layer to draw on");
    if (editor.active() != nullptr) {
        check(paintPixels(editor.doc, *editor.active(), {{3, 3}}),
              "and the pencil reaches it");
        check(layerRole(editor.doc, *editor.active()) == ls::kColorRoleNone,
              "it starts as a plain colour");
    }
    check(editor.doc.undo(), "one undo removes frame and layer together");
    resyncLayers(editor);
    check(editor.frames.size() == 1, "the empty frame is gone");
    check(editor.doc.undo(), "and the next undo is the drawing, not the layer");
    resyncLayers(editor);
    check(editor.frames.size() == 1, "still one frame");
    check(editor.doc.redo(), "redo the drawing");
    resyncLayers(editor);

    // Playback across an undo that removes the frames being played.
    check(duplicateFrame(editor.doc, 0) == 1, "a frame to play");
    check(duplicateFrame(editor.doc, 1) == 2, "and another");
    resyncFrames(editor);
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    check(frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) * 2 + 5) == 2,
          "playing the third frame");
    check(editor.doc.undo() && editor.doc.undo(), "undo both frames under playback");
    resyncLayers(editor);
    {
        const int shown = frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) * 2 + 5);
        check(shown >= 0 && shown < static_cast<int>(editor.frames.size()),
              "playback lands on a frame that exists");
        check(editor.activeSprite() == editor.frames.front().sprite,
              "and the tools draw into one that exists");
    }
    editor.timeline.playing = false;

    // Deleting the frame being edited moves the editor to a frame that exists.
    check(duplicateFrame(editor.doc, 0) == 1, "a frame to delete");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(deleteFrame(editor.doc, 1), "delete the active frame");
    resyncLayers(editor);
    check(editor.timeline.activeFrame == 0, "the selection moves");
    check(editor.sprite == editor.frames.front().sprite, "the sprite handle follows");
    check(editor.activeSprite() == editor.sprite, "and both agree");
    check(editor.active() != nullptr, "with a layer to draw on");

    // Stepping between frames keeps the layer position when it can. A frame
    // with fewer layers clamps rather than pointing past the end.
    {
        PaintLayer second;
        check(createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                               ls::Color{1, 2, 3, 255}, &second),
              "a second layer on the first frame");
    }
    resyncLayers(editor);
    check(editor.layers.size() == 2, "two layers on the first frame");
    editor.activeLayer = 1;
    check(addEmptyFrame(editor, 0) == 1, "an empty frame with one layer");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(editor.activeLayer >= 0 &&
          editor.activeLayer < static_cast<int>(editor.layers.size()),
          "the layer index is in range on a frame with fewer layers");
    check(editor.active() != nullptr, "and points at a layer");
    check(deleteFrame(editor.doc, 1), "drop that frame");
    resyncLayers(editor);

    // Loading a palette recolours the layer, and the ink painting through the
    // same slot with it.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer to colour");
        if (layer != nullptr) {
            editor.doc.beginAction("Use slot 0");
            check(setLayerRole(editor.doc, *layer, 0), "draw through slot 0");
            editor.doc.endAction();
            editor.inkRole = 0;
            PaletteFile file;
            std::string parseError;
            check(parsePalette("123456\n", &file, &parseError), "a one-colour file");
            int dropped = 0;
            check(applyPaletteFile(editor.doc, editor.sprite, file, &dropped),
                  "load it");
            resyncLayers(editor);
            refreshInks(editor);
            const ls::Color shown = toColor(editor.color);
            check(shown.r == 0x12 && shown.g == 0x34 && shown.b == 0x56,
                  "the colour control shows what the layer now draws");
            check(editor.doc.undo(), "undo the load");
            resyncLayers(editor);            // `layer` is stale from here on
            refreshInks(editor);
            editor.doc.beginAction("Detach");
            check(editor.active() != nullptr &&
                  setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone),
                  "detach");
            editor.doc.endAction();
            editor.inkRole = ls::kColorRoleNone;
        }
    }

    // The swap, through the window's own call: every frame recolours, a frame
    // with a palette of its own does not, and undo is one step.
    {
        CanvasView view(nullptr);
        const ls::PaletteId day = documentPalette(editor.doc);
        const ls::PaletteId night = addPalette(editor.doc, "night", day);
        check(night.valid(), "a second palette");
        PaintLayer* layer = editor.active();
        if (layer != nullptr && night.valid()) {
            editor.doc.beginAction("Use slot 0");
            setLayerRole(editor.doc, *layer, 0);
            editor.doc.endAction();
            editor.inkRole = 0;
            setPaletteEntry(editor.doc, night, 0, ls::Color{7, 8, 9, 255});
            swapPalette(editor, view, night);
            check(documentPalette(editor.doc) == night, "the document swapped");
            const ls::Color shown = toColor(editor.color);
            check(shown.r == 7 && shown.g == 8 && shown.b == 9,
                  "the colour control follows the swap");
            check(editor.doc.undo(), "one undo");
            resyncLayers(editor);
            refreshInks(editor);
            check(documentPalette(editor.doc) == day, "back to the first palette");
            swapPalette(editor, view, day);
            check(!editor.doc.canUndo() || editor.doc.undoLabel() != "Swap palette",
                  "swapping to the palette in use is not an action");
            editor.doc.beginAction("Detach");
            setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone);
            editor.doc.endAction();
            editor.inkRole = ls::kColorRoleNone;
        }
    }

    // The stack, driven the way the panel and the shortcuts drive it. Each
    // step leaves the active layer pointing at a layer that exists and the
    // selection inside the list.
    {
        CanvasView view(nullptr);
        const size_t base = editor.layers.size();
        const ls::LayerId first = editor.active()->layer;
        const int firstAt = indexOfLayer(editor.doc, editor.sprite, first);
        duplicateActiveLayer(editor, view);
        check(editor.layers.size() == base + 1, "a duplicate");
        const ls::LayerId copy = editor.active()->layer;
        check(copy != first, "and it is the active one");
        check(editor.selectedLayers == std::vector<ls::LayerId>({ copy }),
              "the selection is the copy");
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt + 1,
              "right above the original");

        lowerActiveLayer(editor, view);
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt, "lowered");
        check(editor.active()->layer == copy, "still active after the move");
        raiseActiveLayer(editor, view);
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt + 1, "raised");

        // Copy it, paste it into a new frame.
        copyActiveLayer(editor);
        check(editor.clipboard == copy, "copied");
        check(addEmptyFrame(editor, 0) == 1, "a frame to paste into");
        resyncFrames(editor);
        selectFrame(editor, 1);
        pasteLayerHere(editor, view);
        check(editor.layers.size() == 2, "pasted beside the empty frame's own layer");
        check(editor.activeSprite() == editor.frames[1].sprite, "in the second frame");
        check(editor.doc.engine().getLayerInfo(editor.active()->layer).value.sprite ==
              editor.frames[1].sprite, "and the pasted layer belongs to it");
        selectFrame(editor, 0);

        // Group both, then ungroup; the active layer survives each.
        selectLayer(editor, first);
        selectLayer(editor, copy, true);
        check(editor.selectedLayers.size() == 2, "two selected");
        groupSelectedLayers(editor, view);
        check(groupOf(editor.doc, first).valid() && groupOf(editor.doc, first) == groupOf(editor.doc, copy),
              "grouped together");
        check(editor.active() != nullptr && editor.active()->layer == copy, "active kept");
        check(editor.selectedLayers.size() == 2, "selection kept");
        ungroupActiveLayer(editor, view);
        check(!groupOf(editor.doc, copy).valid(), "ungrouped");
        check(editor.active()->layer == copy, "active kept through ungroup");

        // Clip, lock, and the pencil's respect for the lock.
        toggleActiveLayerClip(editor, view);
        {
            LayerProps props;
            readLayerProps(editor.doc, copy, &props);
            check(props.clipBase == first, "clipped to the layer below");
        }
        toggleActiveLayerClip(editor, view);
        toggleActiveLayerLock(editor);
        check(activeLayerLocked(editor), "locked");
        toggleActiveLayerLock(editor);
        check(!activeLayerLocked(editor), "unlocked");

        // Delete the copy; land on what is left. Undo brings it back selected
        // by handle, not by a stale index.
        deleteSelectedLayers(editor, view);
        check(editor.layers.size() == base, "deleted");
        check(editor.active()->layer == first, "landed on the original");
        check(editor.doc.undo(), "undo the delete");
        resyncLayers(editor);
        check(editor.layers.size() == base + 1, "back");
        check(editor.activeLayer < static_cast<int>(editor.layers.size()), "index in range");
        check(!editor.selectedLayers.empty(), "something selected");
        for (ls::LayerId id : editor.selectedLayers) {
            bool found = false;
            for (const PaintLayer& layer : editor.layers) { found = found || layer.layer == id; }
            check(found, "every selected layer exists");
        }

        // Back to one layer and one frame for what follows.
        selectLayer(editor, copy);
        deleteSelectedLayers(editor, view);
        check(deleteFrame(editor.doc, 1), "drop the paste frame");
        resyncLayers(editor);
        editor.clipboard = ls::LayerId{};
    }

    // References: imported, placed, undone, and gone when the document is.
    {
        CanvasView view(nullptr);
        ls::RasterBuffer picture = ls::makeRaster(10, 6);
        for (uint32_t y = 0; y < picture.height; ++y) {
            for (uint32_t x = 0; x < picture.width; ++x) {
                uint8_t* pixel = picture.row(y) + static_cast<size_t>(x) * 4u;
                pixel[0] = 200; pixel[3] = 255;
            }
        }
        std::vector<uint8_t> png;
        std::string pngError;
        check(encodeImageAsPng(picture, &png, &pngError), "encode a reference");

        Reference made;
        std::string error;
        check(addReference(editor.doc, "study.png", png, &made, &error),
              "import a reference");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "the panel sees it");
        check(editor.activeReference == made.id, "and it is selected");

        // Placed by the panel's own call, and read back.
        Reference* active = activeReference(editor);
        check(active != nullptr, "the selected reference resolves");
        if (active != nullptr) {
            Reference moved = *active;
            moved.x = 2.f;
            moved.opacity = 0.3f;
            editor.doc.beginAction("Place reference");
            check(updateReference(editor.doc, moved), "move it");
            editor.doc.endAction();
            resyncReferences(editor, view);
            check(activeReference(editor) != nullptr &&
                  activeReference(editor)->x == 2.f, "the move stuck");
        }

        // It changes no pixel of the artwork: the canvas draws it, the
        // compile does not know it exists.
        const ls::SpriteId sprite = editor.activeSprite();
        auto before = editor.doc.engine().compileSprite(sprite, ls::CompileProfile{});
        check(before.ok(), "the sprite still compiles with a reference in the file");

        check(editor.doc.undo(), "undo the move");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "still one reference");
        check(editor.doc.undo(), "undo the import");
        resyncReferences(editor, view);
        check(editor.references.empty(), "the reference went with it");
        check(editor.activeReference.empty(), "and nothing is selected");
        check(editor.doc.redo(), "redo the import");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "it came back");

        // A file that is not an image is refused, and stores nothing.
        Reference bad;
        error.clear();
        check(!addReference(editor.doc, "notes.txt", { 'n', 'o' }, &bad, &error),
              "a non-image is refused");
        check(!error.empty(), "and says why");
        check(editor.references.size() == 1, "nothing was stored");

        check(removeReference(editor.doc, editor.references[0]), "remove it");
        resyncReferences(editor, view);
        check(editor.references.empty(), "gone");
    }

    // Replacing the document forgets every interaction that was about the old
    // one. Each of these is an index that would otherwise be pressed into a
    // document it was never about.
    editor.renaming = 0;
    editor.timeline.renamingFrame = 0;
    editor.renamingSlot = 2;
    editor.confirmRemoveSlot = 2;
    editor.timeline.selectedStep = 3;
    editor.timeline.playing = true;
    editor.renamingPalette = documentPalette(editor.doc);
    check(newDocument(editor, 16), "new document mid-everything");
    check(!editor.renamingPalette.valid(), "no palette rename in flight");
    check(!editor.activeGroup.valid() && !editor.clipboard.valid(), "no group or clipboard");
    check(editor.references.empty() && editor.activeReference.empty(),
          "the new document has no references");
    check(editor.selectedLayers.size() == 1, "the new document's one layer is selected");
    check(listPalettes(editor.doc).size() == 1, "a new document has one palette");
    check(editor.renaming == -1, "no layer rename in flight");
    check(editor.timeline.renamingFrame == -1, "no frame rename in flight");
    check(editor.renamingSlot == ls::kColorRoleNone, "no slot rename in flight");
    check(editor.confirmRemoveSlot == ls::kColorRoleNone, "no removal pending");
    check(editor.timeline.selectedStep == 0, "no step selected");
    check(!editor.timeline.playing, "not playing");
    check(editor.timeline.activeCycle == -1, "no cycle selected");
    check(editor.frames.size() == 1 && editor.layers.size() == 1, "one frame, one layer");
    check(!editor.doc.canUndo(), "nothing to undo in a new document");

    // Put the first frame's drawing back for the sections that follow.
    editor.doc.beginAction("Pencil");
    paintPixels(editor.doc, *editor.active(), linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // ------------------------------------------------------------ cycles --
    //
    // The window's half of the cycle editor: that selecting one changes what
    // plays and what the canvas shows, and that a step is not a frame.

    check(duplicateFrame(editor.doc, 0) == 1, "a second frame to cycle over");
    check(duplicateFrame(editor.doc, 1) == 2, "and a third");
    resyncFrames(editor);
    check(editor.frames.size() == 3, "three frames");

    check(editor.timeline.activeCycle == -1, "no cycle to start");
    check(activeCycle(editor).frames.size() == 3,
          "with none selected, every frame plays");

    check(addCycle(editor.doc, "walk", 3) == 0, "make a cycle");
    resyncFrames(editor);
    selectCycle(editor, 0);
    check(editor.timeline.activeCycle == 0, "it is selected");
    check(editor.timeline.activeFrame == 0, "and it starts on its first picture");

    // 0 1 2 -> 0 2, then 0 2 2: a cycle that leaves a frame out and plays
    // another twice, which is the whole reason steps are a separate list.
    check(removeCycleStep(editor.doc, 0, 1, 3), "drop the middle step");
    resyncFrames(editor);
    check(addCycleStep(editor.doc, 0, 1, 2, 3) == 2, "and play the last twice");
    resyncFrames(editor);
    check(activeCycle(editor).frames == std::vector<int>({ 0, 2, 2 }),
          "the cycle is a sequence, not a set");

    // Playback follows the selected cycle rather than the frame list, so the
    // frame that was left out never shows.
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    {
        bool sawTheDroppedFrame = false;
        for (uint64_t t = 0; t < static_cast<uint64_t>(kDefaultFrameMs) * 3; t += 10) {
            if (frameToShow(editor, t) == 1) {
                sawTheDroppedFrame = true;
            }
        }
        check(!sawTheDroppedFrame, "a frame no step names never plays");
    }
    editor.timeline.playing = false;

    // Deleting the frame two steps named must clean up both of them.
    check(deleteFrame(editor.doc, 2), "delete the repeated frame");
    resyncFrames(editor);
    check(activeCycle(editor).frames == std::vector<int>({ 0 }),
          "both steps naming it are gone");

    // And a cycle can be got rid of, leaving every frame playing again.
    check(deleteCycle(editor.doc, 0, 2), "delete the cycle");
    resyncFrames(editor);
    selectCycle(editor, -1);
    check(editor.cycles.empty(), "it is gone");
    check(activeCycle(editor).frames.size() == 2, "every frame plays again");

    // ------------------------------------------------------------- sheets --
    //
    // What the window contributes to a sheet is deciding which cells go in it,
    // which is the half fast_core cannot know.

    check(addCycle(editor.doc, "walk", 2) == 0, "a cycle to export");
    resyncFrames(editor);
    selectCycle(editor, 0);
    check(addCycleStep(editor.doc, 0, 1, 0, 2) == 2, "that plays a frame twice");
    resyncFrames(editor);

    editor.sheetFromCycle = true;
    check(sheetSteps(editor) == std::vector<int>({ 0, 1, 0 }),
          "a sheet of the cycle is its steps, repeats and all");

    editor.sheetFromCycle = false;
    check(sheetSteps(editor) == std::vector<int>({ 0, 1 }),
          "a sheet of every frame is every frame once");

    // And the whole way out, through the same call the dialog makes.
    {
        const std::string sheetPath = "ui_selftest_sheet.png";
        editor.sheetFromCycle = true;
        editor.sheet.scale = 2;
        editor.sheet.layout = SheetLayout::Row;
        std::string sheetError;
        check(exportSheetToPng(editor.doc, editor.frames, sheetSteps(editor),
                               editor.cycles, sheetPath, editor.sheet, &sheetError),
              "write a sheet");
        check(fileExists(sheetPath), "the sheet is there");
        check(fileExists("ui_selftest_sheet.json"), "and its description beside it");
        deleteFile(sheetPath);
        deleteFile("ui_selftest_sheet.json");
        editor.sheet = SheetSettings{};
    }

    check(deleteCycle(editor.doc, 0, 2), "tidy the cycle away");
    resyncFrames(editor);
    selectCycle(editor, -1);

    while (editor.frames.size() > 1) {
        check(deleteFrame(editor.doc, static_cast<int>(editor.frames.size()) - 1),
              "back to one frame");
        resyncFrames(editor);
    }
    resyncLayers(editor);

    // ------------------------------------------------------ file handling --
    //
    // This touches the user's real recent list, so it is put back afterwards. A
    // test that leaves someone's settings changed is a bad test however green.
    RecentFiles theirs;
    theirs.load();
    editor.files.recent.clear();

    CanvasView canvas(nullptr);      // nothing here draws; it carries zoom and pan

    std::string error;
    const std::string path = "ui_selftest.lsprite";

    check(windowTitle(editor.doc).find('*') != std::string::npos,
          "an edited document is marked modified");
    canvas.setZoom(19.f);
    canvas.setPan(-33.f, 21.f);
    check(saveTo(editor, canvas, path), "save");
    check(!editor.doc.modified(), "saving clears the modified mark");
    check(editor.files.recent.entries().size() == 1, "the save is remembered");
    check(windowTitle(editor.doc).find("ui_selftest") != std::string::npos,
          "the title names the file");

    Editor reopened;
    // An ink naming a slot the opened palette does not have is let go on
    // open rather than kept pointing at nothing.
    reopened.inkRole = 250;
    canvas.resetView();
    check(canvas.zoom() == 8.f, "the view is reset before loading");

    openPath(reopened, canvas, path);
    check(!reopened.layers.empty(), "the reopened file has layers");
    check(canvas.zoom() == 19.f, "zoom comes back");
    check(canvas.panX() == -33.f, "pan x comes back");
    check(canvas.panY() == 21.f, "pan y comes back");
    check(!paletteEntries(reopened.doc).empty(), "the palette came back");

    check(reopened.active() != nullptr, "there is an active layer");
    check(reopened.inkRole == ls::kColorRoleNone,
          "an ink naming a slot the file lacks is let go");

    // A hostile view has to be clamped rather than trusted.
    reopened.doc.setUiState("{\"zoom\":1e6,\"panX\":-9e9,\"activeLayer\":9999}");
    check(reopened.doc.save(path, &error), "save a hostile view");
    openPath(reopened, canvas, path);
    check(canvas.zoom() <= 64.f, "an absurd zoom is clamped");
    check(canvas.panX() >= -20000.f, "an absurd pan is clamped");
    check(reopened.activeLayer < static_cast<int>(reopened.layers.size()),
          "an out-of-range layer index is clamped");

    check(reopened.doc.open(path, &error), "open again");
    check(reopened.doc.engine().documents().size() == 1, "one document held");

    theirs.save();
    deleteFile(path);

    // File > New as the window drives it: a size that is not square, a
    // background of its own at the bottom, a preset palette, and the drawing
    // layer above the background, selected, with nothing to undo.
    {
        Editor fresh;
        FileState::NewDocument spec;
        spec.width = 12;
        spec.height = 5;
        spec.background = 1;
        spec.preset = 0;
        check(newDocument(fresh, spec), "a new document from the window's choices");
        auto size = fresh.doc.engine().getCanvasSize(fresh.doc.id());
        check(size.ok() && size.value.x == 12 && size.value.y == 5, "its own width and height");
        check(fresh.layers.size() == 2, "a background and a layer to draw on");
        check(fresh.active() != nullptr && fresh.activeLayer == 1, "the drawing layer is active");
        const ls::RasterBuffer shown = [&] {
            auto compiled = fresh.doc.engine().compileSprite(
                fresh.sprite, compileProfile(ls::CompileProfileType::Export, 12, 5));
            return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
        }();
        check(!shown.empty() && ls::readPixel(shown, 11, 4).r == 255, "the background is white");
        check(paletteEntries(fresh.doc).size() == palettePresets()[0].colours.size(),
              "the preset is the palette");
        check(!fresh.doc.canUndo() && !fresh.doc.modified(), "a new document has nothing to undo");
    }

    // Rotating a selection freely: its pixels on a layer of their own with a
    // RotSprite rotation, the original layer without them, one undo step.
    {
        Editor turned;
        check(newDocument(turned, 16), "a document to turn");
        turned.doc.beginAction("Pencil");
        paintPixels(turned.doc, *turned.active(), linePixels({ 2, 2 }, { 9, 2 }));
        turned.doc.endAction();
        const size_t before = turned.layers.size();
        turned.selection.mask = rectangleMask({ 2, 2 }, { 9, 2 });
        check(rotateSelectionFreely(turned), "rotate the selection freely");
        check(turned.layers.size() == before + 1, "onto a layer of its own");
        const std::vector<TransformEntry> spin = listTransforms(turned.doc, turned.active()->layer);
        check(spin.size() == 1 && spin.front().sampling == ls::SamplingPolicy::RotSprite,
              "with a RotSprite rotation");
        check(turned.doc.undo(), "one undo");
        resyncLayers(turned);
        check(turned.layers.size() == before, "puts it back");
    }

    // Tracks through the editor: a layer added in one frame is in the other,
    // and merging down merges in both, one undo step each.
    {
        Editor tracked;
        CanvasView view(nullptr);
        check(newDocument(tracked, 8), "a document for tracks");
        check(tracksOn(tracked.doc), "a new document keeps its frames' layers in step");
        check(addEmptyFrame(tracked, 0) == 1, "a second frame");
        resyncFrames(tracked);
        const ls::SpriteId one = tracked.frames[0].sprite;
        const ls::SpriteId two = tracked.frames[1].sprite;
        check(layerOrder(tracked.doc, one).size() == layerOrder(tracked.doc, two).size(),
              "a new frame has every layer");
        PaintLayer cape;
        tracked.doc.beginAction("Add layer");
        check(createPaintLayer(tracked.doc, one, "cape", ls::Color{ 90, 200, 90, 255 }, &cape),
              "a layer in the first frame");
        tracked.doc.endAction();
        const size_t count = layerOrder(tracked.doc, one).size();
        check(layerOrder(tracked.doc, two).size() == count, "is in the second too");
        check(!trackKey(tracked.doc, cape.layer).empty() &&
              layerOfTrack(tracked.doc, two, trackKey(tracked.doc, cape.layer)).valid(),
              "as the same track");
        resyncLayers(tracked);
        selectLayer(tracked, cape.layer);
        mergeActiveLayerDown(tracked, view);
        check(layerOrder(tracked.doc, one).size() == count - 1 &&
              layerOrder(tracked.doc, two).size() == count - 1, "merging down merges in both");
        check(tracked.doc.undo() && layerOrder(tracked.doc, two).size() == count,
              "and one undo takes both back");
    }

    // Edit > Fill and Stroke: the selection painted in the current colour,
    // and a stroke only round its edge; each one undo step.
    {
        Editor filled;
        check(newDocument(filled, 16), "a document to fill");
        filled.selection.mask = rectangleMask({ 2, 2 }, { 9, 9 });
        filled.color[0] = 0.9f; filled.color[1] = 0.1f; filled.color[2] = 0.1f; filled.color[3] = 1.f;
        filled.brush.size = 2;
        check(fillSelection(filled, true), "stroke the selection");
        const auto redAt = [&](int x, int y) {
            ls::RasterBuffer picture;
            if (!compileForExport(filled.doc, filled.sprite, 1, &picture, nullptr)) {
                return false;
            }
            const uint8_t* px = picture.row(static_cast<uint32_t>(y)) + x * 4;
            return px[0] > 200 && px[1] < 60 && px[3] > 200;
        };
        check(redAt(2, 2) && redAt(3, 5) && !redAt(4, 4) && !redAt(5, 5), "a band two wide inside the edge");
        check(fillSelection(filled, false) && redAt(5, 5) && !redAt(10, 10), "filling paints the inside");
        check(filled.doc.undo() && !redAt(5, 5) && redAt(2, 2), "one undo each");
    }

    // Tweens through the editor: an offset keyed at 0 and 8 puts the middle
    // frame's pixel halfway, and one undo takes it back.
    {
        Editor tweened;
        check(newDocument(tweened, 16), "a document to tween");
        PaintLayer ball;
        tweened.doc.beginAction("Ball");
        check(createPaintLayer(tweened.doc, tweened.sprite, "ball", ls::Color{ 220, 40, 40, 255 }, &ball),
              "a layer to move");
        paintPixels(tweened.doc, ball, {{ 2, 2 }});
        tweened.doc.endAction();
        check(duplicateFrame(tweened.doc, 0) == 1 && duplicateFrame(tweened.doc, 1) == 2,
              "three frames");
        resyncFrames(tweened);
        const std::string key = trackKey(tweened.doc, ball.layer);
        const ls::LayerId first = layerOfTrack(tweened.doc, tweened.frames[0].sprite, key);
        const ls::LayerId last = layerOfTrack(tweened.doc, tweened.frames[2].sprite, key);
        tweened.doc.beginAction("Keys");
        addOffset(tweened.doc, first, { 0.f, 0.f });
        addOffset(tweened.doc, last, { 8.f, 0.f });
        tweened.doc.endAction();
        std::vector<ls::SpriteId> run;
        for (const Frame& f : tweened.frames) { run.push_back(f.sprite); }
        tweened.doc.beginAction("Tween");
        check(tweenTransforms(tweened.doc, run, key, TweenEasing::Linear, nullptr), "tween the run");
        tweened.doc.endAction();
        const auto redAt = [&](int x, int y) {
            ls::RasterBuffer picture;
            if (!compileForExport(tweened.doc, tweened.frames[1].sprite, 1, &picture, nullptr)) {
                return false;
            }
            const uint8_t* px = picture.row(static_cast<uint32_t>(y)) + x * 4;
            return px[0] > 150 && px[1] < 100 && px[3] > 200;
        };
        check(redAt(6, 2) && !redAt(2, 2), "the middle frame's pixel is halfway");
        check(tweened.doc.undo() && redAt(2, 2), "and one undo puts it back");
    }

    // Tabs: a second document opens beside the first rather than over it; each
    // keeps its own pixels, layers, history and selection across a switch;
    // closing one shows its neighbour; a blank untouched document is replaced
    // rather than kept.
    {
        Editor tabbed;
        CanvasView view(nullptr);
        check(newDocument(tabbed, 16), "a first document");
        initTabs(tabbed);
        check(documentIsBlank(tabbed), "an untouched document is blank");
        tabbed.doc.beginAction("Pencil");
        paintPixels(tabbed.doc, *tabbed.active(), {{ 3, 3 }});
        tabbed.doc.endAction();
        tabbed.selection.mask = rectangleMask({ 1, 1 }, { 4, 4 });
        check(!documentIsBlank(tabbed), "drawn on, it is not");

        openTab(tabbed, view);
        check(newDocument(tabbed, 8), "a second document in its own tab");
        check(tabCount(tabbed) == 2 && tabbed.activeTab == 1, "two tabs, the new one showing");
        check(tabbed.selection.empty() && !tabbed.doc.canUndo(), "with nothing of the first's");
        auto size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(size.ok() && size.value.x == 8, "its own canvas");

        switchToTab(tabbed, view, 0);
        size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(tabbed.activeTab == 0 && size.ok() && size.value.x == 16, "back to the first");
        check(!tabbed.selection.empty() && tabbed.doc.canUndo(), "its selection and history kept");
        check(tabbed.active() != nullptr && tabbed.doc.modified(), "and its layers and changes");
        check(tabModified(tabbed, 0) && !tabModified(tabbed, 1), "which tab has unsaved work");
        check(modifiedOtherTab(tabbed) < 0, "no other tab has");

        closeActiveTab(tabbed, view);
        size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(tabCount(tabbed) == 1 && size.ok() && size.value.x == 8, "closing shows the other");
        closeActiveTab(tabbed, view);
        check(tabCount(tabbed) == 1 && tabbed.active() != nullptr,
              "closing the last leaves a new blank one");
    }

    // A polygon and a curve placed point by point: each lands as an element
    // of its kind, is the active shape with a handle per point, and finishing
    // with too few points places nothing.
    {
        Editor paths;
        CanvasView view(nullptr);
        check(newDocument(paths, 16), "a document for paths");
        paths.tool = Tool::Polygon;
        paths.placingPath = true;
        paths.pathPoints = { { 2.5f, 2.5f }, { 12.5f, 2.5f }, { 7.5f, 12.5f } };
        paths.pathHandles.assign(3, { 0.f, 0.f });
        check(finishPath(paths, view, true), "a polygon is placed");
        check(!paths.placingPath && paths.pathPoints.empty(), "and the tool is ready again");
        ShapeLayer placed;
        check(activeShape(paths, &placed) && placed.kind == ShapeKind::Polygon,
              "the polygon is the active shape");
        ShapeParams read;
        check(readShapeParams(paths.doc, placed, &read) &&
              shapeHandles(placed.kind, read).size() == 3, "with a handle per corner");

        paths.tool = Tool::Curve;
        paths.placingPath = true;
        paths.pathPoints = { { 2.5f, 14.5f }, { 8.5f, 4.5f }, { 14.5f, 14.5f } };
        paths.pathHandles = { { 0.f, 0.f }, { 3.f, 0.f }, { 0.f, 0.f } };
        check(finishPath(paths, view, false), "a curve is placed");
        check(activeShape(paths, &placed) && placed.kind == ShapeKind::Curve,
              "the curve is the active shape");
        check(readShapeParams(paths.doc, placed, &read) && read.points.size() == 7 &&
              shapeHandles(placed.kind, read).size() == 7, "three anchors and four controls");
        {
            size_t shapes = 0;
            for (const Element& element : elementsOf(paths.doc, paths.active()->layer)) {
                shapes += element.isGeometry() ? 1u : 0u;
            }
            check(shapes == 2, "both on the one layer");
        }

        paths.placingPath = true;
        paths.pathPoints = { { 1.5f, 1.5f } };
        paths.pathHandles = { { 0.f, 0.f } };
        check(!finishPath(paths, view, false) && !paths.placingPath,
              "one point is not a curve, and is let go");
    }

    // A run of frames selected in the strip: it is the run, it plays as a
    // loop of its own when no cycle is chosen, and speed scales the clock.
    {
        Editor anim;
        check(newDocument(anim, 8), "a document to animate");
        for (int i = 0; i < 3; ++i) {
            check(addEmptyFrame(anim, i) == i + 1, "an empty frame");
        }
        resyncFrames(anim);
        anim.timeline.activeFrame = 3;
        anim.timeline.rangeAnchor = 1;
        int first = 0;
        int last = 0;
        check(frameRange(anim, &first, &last) && first == 1 && last == 3, "the run is 1 to 3");
        check(activeCycle(anim).frames == std::vector<int>({ 1, 2, 3 }), "the run plays alone");
        anim.timeline.playing = true;
        anim.timeline.startedAtMs = 1000;
        anim.timeline.speed = 2.f;
        // Holds of 100 ms at double speed: 100 ms in is the run's third step.
        check(frameToShow(anim, 1100) == 3, "speed scales the clock, not the holds");
        anim.timeline.rangeAnchor = 3;
        check(!frameRange(anim, &first, &last), "one frame is not a run");
    }

    // ----------------------------------------------------------- layout --
    //
    // Which panels are open is kept in the layout file with where they are:
    // read back as it was written.
    {
        Editor laidOut;
        ImGuiContext* context = ImGui::CreateContext();
        registerPanelSettings(laidOut);
        const char* kept = "[FastPanels][Open]\nreferences=1\nelements=0\ntimeline=1\n\n";
        ImGui::LoadIniSettingsFromMemory(kept, std::strlen(kept));
        check(laidOut.panels.references && !laidOut.panels.elements && laidOut.timeline.visible,
              "the panels open as they were left");
        check(laidOut.panels.properties, "and the ones not mentioned as they start");
        laidOut.panels.properties = false;
        size_t size = 0;
        const std::string written = ImGui::SaveIniSettingsToMemory(&size);
        check(written.find("properties=0") != std::string::npos &&
                  written.find("references=1") != std::string::npos,
              "and are written back as they are");
        ImGui::DestroyContext(context);
    }

    if (failures == 0) {
        std::printf("ui_selftest: all checks passed\n");
        return 0;
    }
    std::printf("ui_selftest: %d check(s) failed\n", failures);
    return 1;
}


} // namespace fast
