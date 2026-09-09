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

void resyncLayers(Editor& editor) {
    std::vector<PaintLayer> found;
    ls::SpriteId sprite;
    if (!adoptPaintLayers(editor.doc, &sprite, &found)) {
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

bool newDocument(Editor& editor, uint32_t size) {
    if (!editor.doc.create("untitled", size, size)) {
        return false;
    }
    editor.layers.clear();
    editor.activeLayer = 0;
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
    editor.doc.setUiState({});

    // Setting up a document is not editing it. Without this a brand-new file is
    // born dirty, and every File > New asks whether to save nothing.
    editor.doc.markUnmodified();
    editor.say("New " + std::to_string(size) + " x " + std::to_string(size) +
               " document");
    return true;
}

} // namespace fast
