// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/selection_tools.h"

#include "app/layers.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace fast {

// --------------------------------------------------------------- commands --

ls::IntervalSet canvasBounds(Editor& editor) {
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return {};
    }
    return rectangleMask({ 0, 0 }, { size.value.x - 1, size.value.y - 1 });
}

bool changeCanvas(Editor& editor, CanvasView& canvas,
                  const std::function<bool(std::string*)>& change, const std::string& done) {
    settleFloating(editor);
    std::string error;
    if (!change(&error)) {
        editor.say(error.empty() ? std::string("That did not change anything") : error);
        return false;
    }
    // The selection was in the old canvas's pixels; it means nothing now.
    editor.selection = Selection{};
    resyncLayers(editor);
    canvas.requestFit();
    canvas.invalidate();
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    editor.say(size.ok() ? done + " -- now " + std::to_string(size.value.x) + " x " +
                               std::to_string(size.value.y)
                         : done);
    return true;
}

void selectAll(Editor& editor) {
    settleFloating(editor);
    editor.selection.mask = canvasBounds(editor);
    editor.say("Selected the whole canvas");
}

void deselect(Editor& editor) {
    settleFloating(editor);
    if (!editor.selection.empty()) {
        editor.selection.previous = editor.selection.mask;
    }
    editor.selection.mask.clear();
}

void reselect(Editor& editor) {
    settleFloating(editor);
    if (!editor.selection.previous.empty()) {
        editor.selection.mask = editor.selection.previous;
        editor.say("Reselected");
    }
}

void invertSelection(Editor& editor) {
    settleFloating(editor);
    const ls::IntervalSet all = canvasBounds(editor);
    editor.selection.mask = ls::geom::subtractSets(all, editor.selection.mask);
    editor.say(editor.selection.empty() ? "Nothing left selected" : "Selection inverted");
}

bool liftSelection(Editor& editor, const char* label) {
    if (editor.floating.active()) {
        return true;
    }
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return false;
    }
    if (layerLocked(editor.doc, layer->layer)) {
        editor.say("This layer is locked -- unlock it in the Layers panel");
        return false;
    }
    if (!layerTakesSelections(editor.doc, layer->layer)) {
        editor.say("This layer has a transform; remove it in the Transform panel "
                   "to move its pixels");
        return false;
    }
    // With nothing selected, the move tool moves the whole layer, which is
    // what it does in every editor.
    const ls::IntervalSet mask = editor.selection.empty() ? canvasBounds(editor)
                                                          : editor.selection.mask;
    editor.doc.beginAction(label);
    if (!liftPixels(editor.doc, layer->layer, mask, &editor.floating)) {
        editor.doc.abandonAction();
        editor.say("Nothing on this layer there to move");
        return false;
    }
    return true;
}

void settleFloating(Editor& editor) {
    if (!editor.floating.active()) {
        return;
    }
    // The selection follows the pixels to where they landed, clipped as they
    // were, so the ants go round what is really there.
    const ls::IntervalSet landed =
        ls::geom::intersectSets(floatingMask(editor.floating), canvasBounds(editor));
    const bool hadSelection = !editor.selection.empty();
    dropFloating(editor.doc, editor.floating);
    editor.doc.endAction();
    editor.draggingFloat = false;
    if (hadSelection) {
        editor.selection.mask = landed;
    }
    resyncLayers(editor);
}

void cancelFloating(Editor& editor) {
    if (!editor.floating.active()) {
        return;
    }
    // Put back exactly as it was, and the selection where it started.
    const ls::IntervalSet started = editor.floating.originalMask;
    editor.doc.abandonAction();
    editor.floating = Floating{};
    editor.draggingFloat = false;
    if (!editor.selection.empty()) {
        editor.selection.mask = ls::geom::intersectSets(started, canvasBounds(editor));
    }
    resyncLayers(editor);
    editor.say("Move abandoned; the pixels are back where they were");
}

bool nudgeSelection(Editor& editor, ls::Vec2i by) {
    if (!liftSelection(editor, "Move")) {
        return false;
    }
    const ls::Vec2i offset { editor.floating.offset.x + by.x, editor.floating.offset.y + by.y };
    moveFloating(editor.doc, editor.floating, offset);
    if (!editor.selection.empty()) {
        editor.selection.mask = floatingMask(editor.floating);
    }
    return true;
}

bool turnSelection(Editor& editor, FloatTurn turn) {
    if (!liftSelection(editor, turn == FloatTurn::FlipHorizontal ? "Flip horizontally"
                             : turn == FloatTurn::FlipVertical   ? "Flip vertically"
                                                                 : "Rotate")) {
        return false;
    }
    turnFloating(editor.doc, editor.floating, turn);
    if (!editor.selection.empty()) {
        editor.selection.mask = floatingMask(editor.floating);
    }
    return true;
}

bool copySelectionPixels(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr || editor.selection.empty()) {
        return false;
    }
    // Copying a float copies what floats, which is where it is now.
    const ls::IntervalSet mask = editor.floating.active() ? floatingMask(editor.floating)
                                                          : editor.selection.mask;
    PixelClip clip;
    if (!copyPixels(editor.doc, layer->layer, mask, &clip)) {
        editor.say(layerTakesSelections(editor.doc, layer->layer)
                       ? "Nothing on this layer inside the selection"
                       : "This layer has a transform; its pixels cannot be copied "
                         "through a selection");
        return true;       // it was a pixel copy that found nothing, not a layer copy
    }
    editor.pixelClip = std::move(clip);
    editor.clipHoldsPixels = true;
    editor.say("Copied " + std::to_string(ls::geom::pixelCount(editor.pixelClip.mask)) +
               " pixel(s)");
    return true;
}

bool cutSelectionPixels(Editor& editor) {
    if (!copySelectionPixels(editor) || !editor.clipHoldsPixels) {
        return false;
    }
    return deleteSelectionPixels(editor);
}

bool deleteSelectionPixels(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr || editor.selection.empty()) {
        return false;
    }
    if (editor.floating.active()) {
        // Deleting a float: the pixels that were lifted do not come back down.
        for (const Floating::Piece& piece : editor.floating.pieces) {
            editor.doc.engine().removeOperation(editor.floating.layer, piece.fill);
            editor.doc.engine().deleteRegion(piece.region);
        }
        editor.floating.pieces.clear();
        settleFloating(editor);
        editor.say("Deleted");
        return true;
    }
    if (layerLocked(editor.doc, layer->layer)) {
        editor.say("This layer is locked -- unlock it in the Layers panel");
        return true;
    }
    editor.doc.beginAction("Delete");
    if (!clearPixels(editor.doc, layer->layer, editor.selection.mask)) {
        editor.doc.abandonAction();
        editor.say("Nothing on this layer inside the selection");
        return true;
    }
    editor.doc.endAction();
    resyncLayers(editor);
    editor.say("Deleted the selected pixels; shapes are removed in the Element panel");
    return true;
}

bool pastePixels(Editor& editor) {
    if (!editor.clipHoldsPixels || editor.pixelClip.empty()) {
        return false;
    }
    settleFloating(editor);
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return false;
    }
    if (layerLocked(editor.doc, layer->layer)) {
        editor.say("This layer is locked -- unlock it in the Layers panel");
        return true;
    }
    editor.doc.beginAction("Paste");
    if (!floatClip(editor.doc, layer->layer, editor.pixelClip, &editor.floating)) {
        editor.doc.abandonAction();
        editor.say("Could not paste onto this layer");
        return true;
    }
    editor.selection.mask = floatingMask(editor.floating);
    // Straight to the move tool: a paste is always followed by putting it
    // somewhere, and the tool that does that should already be in hand.
    editor.tool = Tool::Move;
    editor.say("Pasted -- drag it into place, Enter to drop it, Escape to take it back");
    return true;
}

bool pastePixelsAsLayer(Editor& editor) {
    if (!editor.clipHoldsPixels || editor.pixelClip.empty()) {
        return false;
    }
    settleFloating(editor);
    PaintLayer made;
    editor.doc.beginAction("Paste as layer");
    if (!createPaintLayer(editor.doc, editor.activeSprite(), "Pasted", toColor(editor.color),
                          &made)) {
        editor.doc.abandonAction();
        return false;
    }
    // Just above the active layer, where the eye already is.
    if (PaintLayer* active = editor.active()) {
        const int at = indexOfLayer(editor.doc, editor.activeSprite(), active->layer);
        if (at >= 0) {
            moveLayer(editor.doc, made.layer, at + 1);
        }
    }
    if (!floatClip(editor.doc, made.layer, editor.pixelClip, &editor.floating)) {
        editor.doc.abandonAction();
        return false;
    }
    // The float keeps the action open; settling it closes it, as with a paste.
    resyncLayers(editor);
    selectLayer(editor, made.layer);
    editor.selection.mask = floatingMask(editor.floating);
    editor.tool = Tool::Move;
    editor.say("Pasted onto a new layer -- drag it into place, Enter to drop it");
    return true;
}

// ------------------------------------------------------------------ input --

namespace {

ls::IntervalSet shapeFor(const Editor& editor, ls::Vec2i to) {
    switch (editor.tool) {
        case Tool::Select:        return rectangleMask(editor.selectAnchor, to);
        case Tool::SelectEllipse: return ellipseMask(editor.selectAnchor, to);
        case Tool::Lasso:         return lassoMask(editor.lassoPoints);
        default:                  return {};
    }
}

uint32_t canvasWidth(Editor& editor) {
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    return size.ok() ? static_cast<uint32_t>(size.value.x) : 0;
}

uint32_t canvasHeight(Editor& editor) {
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    return size.ok() ? static_cast<uint32_t>(size.value.y) : 0;
}

} // namespace

bool handleSelectionInput(Editor& editor, CanvasView& canvas, bool overCanvas,
                          ls::Vec2i pixel) {
    const ImGuiIO& io = ImGui::GetIO();
    // Past the edge of the artwork a drag still knows where it is.
    const ls::Vec2i pointer = canvas.pointerPixel();

    // A float being dragged goes on being dragged, whatever the tool.
    if (editor.draggingFloat) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ls::Vec2i offset { editor.floatGrabOffset.x + pointer.x - editor.floatGrab.x,
                                     editor.floatGrabOffset.y + pointer.y - editor.floatGrab.y };
            if (offset.x != editor.floating.offset.x || offset.y != editor.floating.offset.y) {
                moveFloating(editor.doc, editor.floating, offset);
                if (!editor.selection.empty()) {
                    editor.selection.mask = floatingMask(editor.floating);
                }
            }
        } else {
            editor.draggingFloat = false;
        }
        return true;
    }

    const bool moveTool = editor.tool == Tool::Move;
    const bool selectTool = isSelectionTool(editor.tool);
    if (!moveTool && !selectTool) {
        return false;
    }

    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsKeyDown(ImGuiKey_Space)) {
        const bool inFloat = editor.floating.active() &&
                             ls::geom::contains(floatingMask(editor.floating), pixel);
        const bool inSelection = !editor.selection.empty() && editor.selection.contains(pixel);
        const SelectMode mode = io.KeyShift && io.KeyAlt ? SelectMode::Intersect
                              : io.KeyShift              ? SelectMode::Add
                              : io.KeyAlt                ? SelectMode::Subtract
                                                         : SelectMode::Replace;
        // The move tool always grabs. A selection tool grabs when pressed
        // inside what is selected, or with Ctrl, and otherwise selects.
        const bool grabs = moveTool ||
            (mode == SelectMode::Replace && (inFloat || inSelection || io.KeyCtrl));
        if (grabs) {
            if (editor.floating.active() && !inFloat) {
                settleFloating(editor);
            }
            if (!editor.floating.active() && !liftSelection(editor, "Move")) {
                return true;
            }
            editor.draggingFloat = true;
            editor.floatGrab = pixel;
            editor.floatGrabOffset = editor.floating.offset;
            return true;
        }

        settleFloating(editor);
        editor.selectMode = mode;
        if (editor.tool == Tool::Wand) {
            const ls::IntervalSet found =
                wandMask(editor.doc, editor.sprite, pixel, editor.wand);
            if (mode == SelectMode::Replace && !editor.selection.empty()) {
                editor.selection.previous = editor.selection.mask;
            }
            editor.selection.mask = clipToCanvas(combine(editor.selection.mask, found, mode),
                                                 canvasWidth(editor), canvasHeight(editor));
            editor.say(editor.selection.empty() ? "Nothing selected"
                       : std::to_string(ls::geom::pixelCount(editor.selection.mask)) +
                             " pixel(s) selected");
            return true;
        }
        editor.selecting = true;
        editor.selectAnchor = pixel;
        editor.lassoPoints.assign(1, pixel);
        editor.selectPreview = shapeFor(editor, pixel);
        return true;
    }

    if (editor.selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (editor.tool == Tool::Lasso) {
            const ls::Vec2i last = editor.lassoPoints.back();
            if (last.x != pointer.x || last.y != pointer.y) {
                editor.lassoPoints.push_back(pointer);
            }
        }
        editor.selectPreview = shapeFor(editor, pointer);
        return true;
    }

    if (editor.selecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        editor.selecting = false;
        const bool click = editor.lassoPoints.size() <= 1 &&
                           pointer.x == editor.selectAnchor.x &&
                           pointer.y == editor.selectAnchor.y;
        const ls::IntervalSet shape = shapeFor(editor, pointer);
        editor.selectPreview.clear();
        editor.lassoPoints.clear();
        if (click && editor.selectMode == SelectMode::Replace) {
            // A click without a drag lets go of the selection, as in every
            // editor: selecting one pixel by clicking it is never the intent.
            deselect(editor);
            editor.say("Deselected");
            return true;
        }
        if (editor.selectMode == SelectMode::Replace && !editor.selection.empty()) {
            editor.selection.previous = editor.selection.mask;
        }
        editor.selection.mask = clipToCanvas(
            combine(editor.selection.mask, shape, editor.selectMode),
            canvasWidth(editor), canvasHeight(editor));
        const ls::Rect2i box = editor.selection.bounds();
        editor.say(editor.selection.empty() ? "Nothing selected"
                   : std::to_string(box.width()) + " x " + std::to_string(box.height()) +
                         " selected");
        return true;
    }

    return overCanvas;
}

bool handleSelectionKeys(Editor& editor) {
    const ImGuiIO& io = ImGui::GetIO();
    if (editor.floating.active()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancelFloating(editor);
            return true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            settleFloating(editor);
            editor.say("Dropped");
            return true;
        }
    }
    const bool hasSomething = editor.floating.active() || !editor.selection.empty();
    if (!hasSomething || io.KeyCtrl) {
        return false;
    }
    // Arrows nudge by a pixel, or by eight with Shift -- a tile's width on the
    // grids most sprites are drawn to.
    const int step = io.KeyShift ? 8 : 1;
    struct Arrow { ImGuiKey key; ls::Vec2i by; };
    const Arrow arrows[] = {
        { ImGuiKey_LeftArrow,  { -step, 0 } }, { ImGuiKey_RightArrow, { step, 0 } },
        { ImGuiKey_UpArrow,    { 0, -step } }, { ImGuiKey_DownArrow,  { 0, step } },
    };
    for (const Arrow& arrow : arrows) {
        if (ImGui::IsKeyPressed(arrow.key, true)) {
            nudgeSelection(editor, arrow.by);
            return true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        deleteSelectionPixels(editor);
        return true;
    }
    if (editor.floating.active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelFloating(editor);
        return true;
    }
    if (!editor.floating.active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        deselect(editor);
        editor.say("Deselected");
        return true;
    }
    return false;
}

void drawSelectionOverlay(const Editor& editor, ImDrawList* draw, ImVec2 origin, float zoom) {
    // A float with no selection behind it is a whole layer on the move; the
    // ants then go round the layer's bounds as they travel.
    const ls::IntervalSet shown = editor.floating.active() && editor.selection.empty()
        ? floatingMask(editor.floating)
        : editor.selection.mask;
    const auto ants = [&](const ls::IntervalSet& mask, bool live) {
        if (mask.empty()) {
            return;
        }
        // Marching: each unit of edge is black or white by its position and
        // the clock, so the pattern crawls along the outline. Two-tone, so it
        // reads over any colour the artwork has.
        const int phase = live ? static_cast<int>(SDL_GetTicks() / 120 % 8) : 0;
        const ImU32 dark = IM_COL32(0, 0, 0, 230);
        const ImU32 light = IM_COL32(255, 255, 255, 240);
        for (const MaskEdge& edge : maskOutline(mask)) {
            const int dx = edge.to.x > edge.from.x ? 1 : 0;
            const int dy = edge.to.y > edge.from.y ? 1 : 0;
            const int length = dx ? edge.to.x - edge.from.x : edge.to.y - edge.from.y;
            for (int i = 0; i < length; ++i) {
                const float x = static_cast<float>(edge.from.x + dx * i);
                const float y = static_cast<float>(edge.from.y + dy * i);
                const ImVec2 a(origin.x + x * zoom, origin.y + y * zoom);
                const ImVec2 b(a.x + static_cast<float>(dx) * zoom,
                               a.y + static_cast<float>(dy) * zoom);
                // Four screen pixels to a dash whatever the zoom, so the ants
                // are the same size at 2x and at 30x.
                const float span = std::max(zoom, 1.f);
                const int dashes = std::max(1, static_cast<int>(span / 4.f));
                for (int d = 0; d < dashes; ++d) {
                    const float t0 = static_cast<float>(d) / static_cast<float>(dashes);
                    const float t1 = static_cast<float>(d + 1) / static_cast<float>(dashes);
                    const bool on = ((static_cast<int>(x + y) * dashes + d + phase) / 2) % 2 == 0;
                    draw->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                                  ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1),
                                  on ? dark : light, 1.f);
                }
            }
        }
    };
    ants(shown, true);
    if (editor.selecting) {
        ants(editor.selectPreview, false);
    }
}

} // namespace fast
