// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/editor.h"

namespace fast {

// Deliberately free of any ImGui call. This is reached from the headless
// self-test, where there is no context, and a status message is not worth a
// crash -- nor worth a context check on every call.
void Editor::say(const std::string& message) {
    status = message;
}

ls::Color toColor(const float rgba[4]) {
    return { static_cast<uint8_t>(rgba[0] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[1] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[2] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[3] * 255.f + 0.5f) };
}

void fromColor(ls::Color colour, float rgba[4]) {
    rgba[0] = static_cast<float>(colour.r) / 255.f;
    rgba[1] = static_cast<float>(colour.g) / 255.f;
    rgba[2] = static_cast<float>(colour.b) / 255.f;
    rgba[3] = static_cast<float>(colour.a) / 255.f;
}

void resyncFrames(Editor& editor) {
    editor.frames = readFrames(editor.doc);
    editor.cycles = readCycles(editor.doc, static_cast<int>(editor.frames.size()));

    // Undo, redo and a delete can all leave the index past the end.
    if (editor.frames.empty()) {
        editor.timeline.activeFrame = 0;
    } else if (editor.timeline.activeFrame >= static_cast<int>(editor.frames.size())) {
        editor.timeline.activeFrame = static_cast<int>(editor.frames.size()) - 1;
    }
    if (editor.timeline.activeFrame < 0) {
        editor.timeline.activeFrame = 0;
    }
    if (editor.timeline.activeCycle >= static_cast<int>(editor.cycles.size())) {
        editor.timeline.activeCycle = -1;
    }
    if (!editor.frames.empty()) {
        editor.sprite =
            editor.frames[static_cast<size_t>(editor.timeline.activeFrame)].sprite;
    }
}

void resyncLayers(Editor& editor) {
    // Which sprite the layers come from is the active frame's, not the
    // document's first. Before frames those were the same thing, which is
    // exactly the assumption that had to come out.
    resyncFrames(editor);

    std::vector<PaintLayer> found;
    ls::SpriteId sprite = editor.activeSprite();
    if (!sprite.valid()) {
        if (!adoptPaintLayers(editor.doc, &sprite, &found)) {
            return;
        }
    } else if (!adoptPaintLayers(editor.doc, sprite, &found)) {
        return;
    }
    editor.sprite = sprite;
    editor.layers = std::move(found);

    if (editor.activeLayer >= static_cast<int>(editor.layers.size())) {
        editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
    }
    if (editor.activeLayer < 0) {
        editor.activeLayer = 0;
    }
}

int addEmptyFrame(Editor& editor, int index) {
    editor.doc.beginAction("Empty frame");
    const int at = addFrame(editor.doc, index);
    if (at < 0) {
        editor.doc.abandonAction();
        return -1;
    }
    const std::vector<Frame> frames = readFrames(editor.doc);
    PaintLayer layer;
    if (at >= static_cast<int>(frames.size()) ||
        !createPaintLayer(editor.doc, frames[static_cast<size_t>(at)].sprite,
                          "Layer 1", toColor(editor.color), &layer)) {
        editor.doc.abandonAction();
        return -1;
    }
    editor.doc.endAction();
    return at;
}

void selectFrame(Editor& editor, int index) {
    if (editor.frames.empty()) {
        return;
    }
    const int last = static_cast<int>(editor.frames.size()) - 1;
    editor.timeline.activeFrame = index < 0 ? 0 : (index > last ? last : index);

    // A different frame is a different set of layers, so the panel and the
    // colour control both have to follow. Selecting the layer at the same
    // position keeps the obvious thing working: step through frames while
    // drawing on "the outline layer" and stay on it.
    const int wasActive = editor.activeLayer;
    resyncLayers(editor);
    if (wasActive < static_cast<int>(editor.layers.size())) {
        editor.activeLayer = wasActive;
    }
    syncColorFromLayer(editor);
}

Cycle activeCycle(const Editor& editor) {
    if (editor.timeline.activeCycle >= 0 &&
        editor.timeline.activeCycle < static_cast<int>(editor.cycles.size())) {
        return editor.cycles[static_cast<size_t>(editor.timeline.activeCycle)];
    }
    return everyFrame(static_cast<int>(editor.frames.size()));
}

void selectCycle(Editor& editor, int index) {
    const int count = static_cast<int>(editor.cycles.size());
    editor.timeline.activeCycle = (index >= 0 && index < count) ? index : -1;
    editor.timeline.selectedStep = 0;
    editor.timeline.playing = false;

    // Land on the cycle's first picture, so selecting one shows what it starts
    // with rather than leaving the canvas on whatever was there before.
    const Cycle cycle = activeCycle(editor);
    if (!cycle.frames.empty()) {
        selectFrame(editor, cycle.frames.front());
    }
}

std::vector<int> sheetSteps(const Editor& editor) {
    if (editor.sheetFromCycle) {
        const Cycle cycle = activeCycle(editor);
        return cycle.frames;
    }
    std::vector<int> every;
    every.reserve(editor.frames.size());
    for (int i = 0; i < static_cast<int>(editor.frames.size()); ++i) {
        every.push_back(i);
    }
    return every;
}

int frameToShow(const Editor& editor, uint64_t nowMs) {
    if (!editor.timeline.playing || editor.frames.empty()) {
        return editor.timeline.activeFrame;
    }
    const Cycle cycle = activeCycle(editor);

    // A function of elapsed time rather than a counter that is stepped. A UI
    // frame that took too long therefore costs nothing: the next one lands
    // where the clock says, not one step further on.
    const int64_t elapsed = static_cast<int64_t>(nowMs - editor.timeline.startedAtMs);
    const int at = frameAt(editor.frames, cycle, elapsed);
    return at < 0 ? editor.timeline.activeFrame : at;
}

void syncColorFromLayer(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    const ls::Color colour = effectiveLayerColor(editor.doc, editor.sprite, *layer);
    if (colour.a == 0) {
        return;                 // nothing resolved; leave the control alone
    }
    fromColor(colour, editor.color);
}

void swapPalette(Editor& editor, CanvasView& canvas, ls::PaletteId palette) {
    if (!palette.valid() || palette == documentPalette(editor.doc)) {
        return;
    }
    editor.doc.beginAction("Swap palette");
    const bool ok = usePalette(editor.doc, palette);
    if (!ok) {
        editor.doc.abandonAction();
        return;
    }
    editor.doc.endAction();
    syncColorFromLayer(editor);
    canvas.invalidate();
    std::string name;
    for (const PaletteInfo& info : listPalettes(editor.doc)) {
        if (info.id == palette) { name = info.name; }
    }
    editor.say("Palette: " + name + " -- every frame recoloured");
}

void forgetInteraction(Editor& editor) {
    editor.renaming = -1;
    editor.renamingPalette = ls::PaletteId{};
    editor.renamingSlot = ls::kColorRoleNone;
    editor.confirmRemoveSlot = ls::kColorRoleNone;
    editor.timeline.renamingFrame = -1;
    editor.timeline.selectedStep = 0;
    editor.timeline.playing = false;
    editor.timeline.activeCycle = -1;
    editor.timeline.activeFrame = 0;
    // A shape or stroke cannot be in progress here: every path that replaces
    // the document stands aside while busy(). The tool itself is kept, as is
    // the colour -- those are the person's, not the document's.
}

bool newDocument(Editor& editor, uint32_t size) {
    if (!editor.doc.create("untitled", size, size)) {
        return false;
    }
    editor.layers.clear();
    editor.activeLayer = 0;
    forgetInteraction(editor);
    // The document already has its first sprite -- Document::create makes one,
    // because a document with no sprite has nothing to draw on. Making another
    // here would open every new file on frame two of two.
    auto info = editor.doc.engine().getDocumentInfo(editor.doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return false;
    }
    editor.sprite = info.value.sprites.front();

    // Every document starts with a palette, so the colours are a named set from
    // the first stroke rather than something to be organised later.
    ensurePalette(editor.doc, editor.sprite);

    PaintLayer layer;
    if (!createPaintLayer(editor.doc, editor.sprite, "Layer 1",
                          toColor(editor.color), &layer)) {
        return false;
    }
    editor.layers.push_back(layer);
    resyncFrames(editor);
    editor.doc.setUiState({});

    // Setting up a document is not editing it. Without this a brand-new file is
    // born dirty, and every File > New asks whether to save nothing -- and the
    // first layer is an undo entry, so Ctrl+Z removes the only layer.
    editor.doc.markUnmodified();
    editor.doc.clearHistory();
    editor.say("New " + std::to_string(size) + " x " + std::to_string(size) +
               " document");
    return true;
}

} // namespace fast
