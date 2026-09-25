// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/editor.h"
#include "ui/os_clipboard.h"

#include "app/clip_image.h"
#include "app/file_io.h"
#include "app/palette_io.h"
#include "app/palette_tools.h"

#include <algorithm>

namespace fast {

// --- the stack -----------------------------------------------------------------

namespace {

int indexInList(const Editor& editor, ls::LayerId layer) {
    for (size_t i = 0; i < editor.layers.size(); ++i) {
        if (editor.layers[i].layer == layer) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// After the list was rebuilt: keep the selection to layers that still exist,
// and the active one among them.
void tidySelection(Editor& editor) {
    std::vector<ls::LayerId> kept;
    for (ls::LayerId id : editor.selectedLayers) {
        if (indexInList(editor, id) >= 0) {
            kept.push_back(id);
        }
    }
    editor.selectedLayers = kept;
    if (PaintLayer* active = editor.active()) {
        if (!layerSelected(editor, active->layer)) {
            editor.selectedLayers.push_back(active->layer);
        }
    }
    if (editor.activeGroup.valid() &&
        editor.doc.engine().getGroupInfo(editor.activeGroup).fail()) {
        editor.activeGroup = ls::GroupId{};
    }
}

} // namespace


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
    if (editor.timeline.rangeAnchor >= static_cast<int>(editor.frames.size())) {
        editor.timeline.rangeAnchor = -1;
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
    tidySelection(editor);
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
    refreshInks(editor);
}

bool frameRange(const Editor& editor, int* first, int* last) {
    const int anchor = editor.timeline.rangeAnchor;
    const int active = editor.timeline.activeFrame;
    const int count = static_cast<int>(editor.frames.size());
    if (anchor < 0 || anchor >= count || anchor == active) {
        return false;
    }
    *first = std::min(anchor, active);
    *last = std::max(anchor, active);
    return true;
}

Cycle activeCycle(const Editor& editor) {
    if (editor.timeline.activeCycle >= 0 &&
        editor.timeline.activeCycle < static_cast<int>(editor.cycles.size())) {
        return editor.cycles[static_cast<size_t>(editor.timeline.activeCycle)];
    }
    // A run selected in the strip plays as a loop of its own: the section
    // being worked on, without making a cycle for it.
    int first = 0;
    int last = 0;
    if (frameRange(editor, &first, &last)) {
        Cycle run;
        run.name = "selection";
        for (int i = first; i <= last; ++i) {
            run.frames.push_back(i);
        }
        return run;
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
    const int64_t elapsed = static_cast<int64_t>(
        static_cast<double>(nowMs - editor.timeline.startedAtMs) *
        static_cast<double>(editor.timeline.speed));
    const int at = frameAt(editor.frames, cycle, elapsed);
    return at < 0 ? editor.timeline.activeFrame : at;
}

bool selectedPixels(Editor& editor, PaintLayer* out) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr || out == nullptr) {
        return false;
    }
    const Element* first = nullptr;
    const std::vector<Element> elements = elementsOf(editor.doc, layer->layer);
    for (const Element& element : elements) {
        if (element.kind != ElementKind::Paint) {
            continue;
        }
        if (first == nullptr) {
            first = &element;
        }
        if (element.fill == editor.activeElement) {
            first = &element;
            break;
        }
    }
    if (first == nullptr) {
        return false;
    }
    out->layer = layer->layer;
    out->fill = first->fill;
    out->region = first->region;
    return true;
}

void loadSettings(Editor& editor) {
    std::vector<uint8_t> bytes;
    std::string error;
    const std::string prefs = preferencesPath();
    if (!prefs.empty() && readFile(prefs, bytes, &error)) {
        editor.prefs = loadPreferences(std::string(bytes.begin(), bytes.end()));
    }
    const std::string keys = keymapPath();
    bytes.clear();
    if (!keys.empty() && readFile(keys, bytes, &error)) {
        editor.keys.load(std::string(bytes.begin(), bytes.end()));
    }
    // What the preferences say a new document is, the New window starts from.
    editor.files.newDocument.width = editor.prefs.newWidth;
    editor.files.newDocument.height = editor.prefs.newHeight;
    editor.files.newDocument.background = editor.prefs.newBackground;
    editor.files.newDocument.preset = editor.prefs.newPreset;
}

void saveSettings(const Editor& editor) {
    std::string error;
    const std::string prefs = preferencesPath();
    if (!prefs.empty()) {
        const std::string text = savePreferences(editor.prefs);
        writeFileAtomic(prefs, std::vector<uint8_t>(text.begin(), text.end()), &error);
    }
    const std::string keys = keymapPath();
    if (!keys.empty()) {
        const std::string text = editor.keys.save();
        writeFileAtomic(keys, std::vector<uint8_t>(text.begin(), text.end()), &error);
    }
}

Symmetry symmetryNow(Editor& editor) {
    Symmetry symmetry;
    symmetry.across = editor.symmetryAcross;
    symmetry.down = editor.symmetryDown;
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    const int width = size.ok() ? size.value.x : 0;
    const int height = size.ok() ? size.value.y : 0;
    symmetry.axisX = editor.symmetryAxisX >= 0 ? editor.symmetryAxisX : width;
    symmetry.axisY = editor.symmetryAxisY >= 0 ? editor.symmetryAxisY : height;
    return symmetry;
}

Ink foregroundInk(const Editor& editor) {
    Ink ink;
    ink.colour = toColor(editor.color);
    ink.role = editor.inkRole;
    return ink;
}

Ink backgroundInk(const Editor& editor) {
    Ink ink;
    ink.colour = toColor(editor.backColor);
    ink.role = editor.backRole;
    return ink;
}

void setForegroundInk(Editor& editor, const Ink& ink) {
    fromColor(ink.colour, editor.color);
    editor.inkRole = ink.role;
}

void setBackgroundInk(Editor& editor, const Ink& ink) {
    fromColor(ink.colour, editor.backColor);
    editor.backRole = ink.role;
}

void swapInks(Editor& editor) {
    const Ink front = foregroundInk(editor);
    setForegroundInk(editor, backgroundInk(editor));
    setBackgroundInk(editor, front);
}

void refreshInks(Editor& editor) {
    // The palette this frame draws with, since that is the one a slot means
    // here -- a frame with a palette of its own shows its own colours.
    const ls::PaletteId palette = paletteFor(editor.doc, editor.activeSprite());
    const auto refresh = [&](float rgba[4], ls::ColorRole& role) {
        if (role == ls::kColorRoleNone) {
            return;
        }
        ls::Color colour;
        if (resolvePaletteRole(editor.doc, palette, role, &colour)) {
            fromColor(colour, rgba);
        } else {
            role = ls::kColorRoleNone;
        }
    };
    refresh(editor.color, editor.inkRole);
    refresh(editor.backColor, editor.backRole);
}


bool layerSelected(const Editor& editor, ls::LayerId layer) {
    for (ls::LayerId id : editor.selectedLayers) {
        if (id == layer) {
            return true;
        }
    }
    return false;
}

void selectLayer(Editor& editor, ls::LayerId layer, bool extend) {
    resyncLayers(editor);
    const int at = indexInList(editor, layer);
    if (at < 0) {
        tidySelection(editor);
        return;
    }
    if (editor.activeLayer != at) {
        editor.activeElement = ls::OperationId{};   // a different layer, its own elements
    }
    editor.activeLayer = at;
    editor.activeGroup = ls::GroupId{};
    if (!extend) {
        editor.selectedLayers.clear();
    }
    if (!layerSelected(editor, layer)) {
        editor.selectedLayers.push_back(layer);
    }
    refreshInks(editor);
}

bool activeLayerLocked(Editor& editor) {
    PaintLayer* layer = editor.active();
    return layer != nullptr && layerLocked(editor.doc, layer->layer);
}

void duplicateActiveLayer(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    const ls::LayerId made = duplicateLayer(editor.doc, layer->layer);
    if (!made.valid()) {
        editor.say("Could not duplicate the layer");
        return;
    }
    selectLayer(editor, made);
    canvas.invalidate();
    editor.say("Duplicated; the copy is its own from the first stroke");
}

void mergeActiveLayerDown(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    std::string why;
    const ls::LayerId into = mergeDown(editor.doc, layer->layer, &why);
    if (!into.valid()) {
        editor.say("Cannot merge down: " + why);
        return;
    }
    editor.activeElement = ls::OperationId{};
    selectLayer(editor, into);
    canvas.invalidate();
    editor.say("Merged down; every element is still its own");
}

void copyActiveLayer(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    editor.clipboard = layer->layer;
    // Other programs get the layer's picture, the canvas's size.
    if (editor.systemClipboard) {
        ls::RasterBuffer image;
        if (layerImage(editor.doc, layer->layer, &image)) {
            putImageOnClipboard(image);
        }
    }
    LayerProps props;
    readLayerProps(editor.doc, layer->layer, &props);
    editor.say("Copied " + props.name + " -- paste into any frame");
}

void pasteLayerHere(Editor& editor, CanvasView& canvas) {
    if (!editor.clipboard.valid()) {
        editor.say("Nothing copied");
        return;
    }
    if (editor.doc.engine().getLayerInfo(editor.clipboard).fail()) {
        editor.say("The copied layer no longer exists");
        editor.clipboard = ls::LayerId{};
        return;
    }
    const ls::SpriteId into = editor.activeSprite();
    int at = -1;
    if (PaintLayer* layer = editor.active()) {
        at = indexOfLayer(editor.doc, into, layer->layer) + 1;
    }
    const ls::LayerId made = pasteLayer(editor.doc, editor.clipboard, into, at);
    if (!made.valid()) {
        editor.say("Could not paste here");
        return;
    }
    selectLayer(editor, made);
    canvas.invalidate();
    editor.say("Pasted");
}

void deleteSelectedLayers(Editor& editor, CanvasView& canvas) {
    std::vector<ls::LayerId> doomed = editor.selectedLayers;
    if (doomed.empty()) {
        if (PaintLayer* layer = editor.active()) {
            doomed.push_back(layer->layer);
        }
    }
    if (doomed.empty() || doomed.size() >= editor.layers.size()) {
        editor.say("A frame needs at least one layer");
        return;
    }
    // Land on the layer below the lowest deleted one, or the bottom.
    const ls::SpriteId sprite = editor.activeSprite();
    int lowest = static_cast<int>(layerOrder(editor.doc, sprite).size());
    for (ls::LayerId id : doomed) {
        lowest = std::min(lowest, indexOfLayer(editor.doc, sprite, id));
    }
    editor.doc.beginAction(doomed.size() == 1 ? "Delete layer" : "Delete layers");
    for (ls::LayerId id : doomed) {
        const ls::GroupId group = groupOf(editor.doc, id);
        editor.doc.engine().deleteLayer(id);
        if (group.valid()) {
            auto info = editor.doc.engine().getGroupInfo(group);
            if (info.ok() && info.value.layers.empty()) {
                editor.doc.engine().deleteGroup(group);
            }
        }
    }
    editor.doc.endAction();
    editor.selectedLayers.clear();
    const std::vector<ls::LayerId> order = layerOrder(editor.doc, sprite);
    const int land = std::max(0, std::min(lowest - 1, static_cast<int>(order.size()) - 1));
    if (!order.empty()) {
        selectLayer(editor, order[static_cast<size_t>(land)]);
    } else {
        resyncLayers(editor);
    }
    canvas.invalidate();
    editor.say(doomed.size() == 1 ? "Layer deleted"
                                  : std::to_string(doomed.size()) + " layers deleted");
}

void raiseActiveLayer(Editor& editor, CanvasView& canvas) {
    if (PaintLayer* layer = editor.active()) {
        const ls::LayerId id = layer->layer;
        if (raiseLayer(editor.doc, id)) {
            selectLayer(editor, id);
            canvas.invalidate();
        }
    }
}

void lowerActiveLayer(Editor& editor, CanvasView& canvas) {
    if (PaintLayer* layer = editor.active()) {
        const ls::LayerId id = layer->layer;
        if (lowerLayer(editor.doc, id)) {
            selectLayer(editor, id);
            canvas.invalidate();
        }
    }
}

void groupSelectedLayers(Editor& editor, CanvasView& canvas) {
    std::vector<ls::LayerId> members = editor.selectedLayers;
    if (members.empty()) {
        if (PaintLayer* layer = editor.active()) {
            members.push_back(layer->layer);
        }
    }
    if (members.empty()) {
        return;
    }
    const std::string name = "Group " +
        std::to_string(groupOrder(editor.doc, editor.activeSprite()).size() + 1);
    const ls::GroupId made = groupLayers(editor.doc, members, name);
    if (!made.valid()) {
        editor.say("Could not group those layers");
        return;
    }
    PaintLayer* layer = editor.active();
    const ls::LayerId keep = layer != nullptr ? layer->layer : members.front();
    selectLayer(editor, keep);
    for (ls::LayerId id : members) {
        if (id != keep) {
            editor.selectedLayers.push_back(id);
        }
    }
    canvas.invalidate();
    editor.say(members.size() == 1 ? "Grouped -- add more with Ctrl+click"
                                   : "Grouped " + std::to_string(members.size()) + " layers");
}

void ungroupActiveLayer(Editor& editor, CanvasView& canvas) {
    ls::GroupId group = editor.activeGroup;
    if (!group.valid()) {
        if (PaintLayer* layer = editor.active()) {
            group = groupOf(editor.doc, layer->layer);
        }
    }
    if (!group.valid()) {
        editor.say("Not in a group");
        return;
    }
    PaintLayer* layer = editor.active();
    const ls::LayerId keep = layer != nullptr ? layer->layer : ls::LayerId{};
    if (ungroup(editor.doc, group)) {
        editor.activeGroup = ls::GroupId{};
        if (keep.valid()) {
            selectLayer(editor, keep);
        } else {
            resyncLayers(editor);
        }
        canvas.invalidate();
        editor.say("Ungrouped; the layers stay where they are");
    }
}

void toggleActiveLayerClip(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    LayerProps props;
    readLayerProps(editor.doc, layer->layer, &props);
    const bool on = !props.clipBase.valid();
    editor.doc.beginAction(on ? "Clip to layer below" : "Unclip");
    if (!clipToBelow(editor.doc, layer->layer, on)) {
        editor.doc.abandonAction();
        editor.say("Nothing below to clip to");
        return;
    }
    editor.doc.endAction();
    canvas.invalidate();
    editor.say(on ? "Draws only where the layer below does" : "Unclipped");
}

void toggleActiveLayerLock(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    const bool locked = !layerLocked(editor.doc, layer->layer);
    setLayerLocked(editor.doc, layer->layer, locked);
    editor.say(locked ? "Locked -- tools leave this layer alone" : "Unlocked");
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
    refreshInks(editor);
    canvas.invalidate();
    std::string name;
    for (const PaletteInfo& info : listPalettes(editor.doc)) {
        if (info.id == palette) { name = info.name; }
    }
    editor.say("Palette: " + name + " -- every frame recoloured");
}

void resyncReferences(Editor& editor, CanvasView& canvas) {
    editor.references = readReferences(editor.doc);
    canvas.referenceTextures().retainOnly(editor.references);
    // An id that no longer names anything stops being the selection rather
    // than pointing at a reference that was undone away.
    bool stillThere = false;
    for (const Reference& reference : editor.references) {
        stillThere = stillThere || reference.id == editor.activeReference;
    }
    if (!stillThere) {
        editor.activeReference = editor.references.empty()
            ? std::string() : editor.references.front().id;
    }
}

Reference* activeReference(Editor& editor) {
    for (Reference& reference : editor.references) {
        if (reference.id == editor.activeReference) {
            return &reference;
        }
    }
    return nullptr;
}

void refreshLibrary(Editor& editor) {
    editor.libraryDocuments.clear();
    editor.libraryImages.clear();
    if (editor.libraryShowingReferences) {
        if (!editor.libraryFolders.references.empty()) {
            editor.libraryImages = listImages(editor.libraryFolders.references);
        }
    } else {
        if (!editor.libraryFolders.project.empty()) {
            editor.libraryDocuments = listDocuments(editor.libraryFolders.project);
        }
    }
    editor.libraryStale = false;
}

void forgetInteraction(Editor& editor) {
    editor.renaming = -1;
    editor.renamingGroup = ls::GroupId{};
    editor.activeElement = ls::OperationId{};
    editor.activeReference.clear();
    editor.references.clear();
    editor.draggingReference = false;
    editor.activeGroup = ls::GroupId{};
    editor.selectedLayers.clear();
    editor.collapsedGroups.clear();
    editor.clipboard = ls::LayerId{};
    editor.renamingPalette = ls::PaletteId{};
    editor.renamingSlot = ls::kColorRoleNone;
    editor.confirmRemoveSlot = ls::kColorRoleNone;
    editor.timeline.renamingFrame = -1;
    editor.timeline.selectedStep = 0;
    editor.timeline.playing = false;
    editor.timeline.activeCycle = -1;
    editor.timeline.activeFrame = 0;
    // The selection and a float were about the old document's pixels. A float
    // cannot be up here in practice -- a click off the artwork drops it first
    // -- but if it were, its action belonged to the document now gone.
    editor.selection = Selection{};
    editor.floating = Floating{};
    editor.draggingFloat = false;
    editor.selecting = false;
    // A shape or stroke cannot be in progress here: every path that replaces
    // the document stands aside while busy(). The tool itself is kept, as is
    // the colour -- those are the person's, not the document's.
}

bool newDocument(Editor& editor, uint32_t size) {
    FileState::NewDocument spec;
    spec.width = size;
    spec.height = size;
    return newDocument(editor, spec);
}

bool newDocument(Editor& editor, const FileState::NewDocument& spec) {
    if (!editor.doc.create("untitled", spec.width, spec.height)) {
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
    // the first stroke rather than something to be organised later -- the
    // starter, or a preset chosen in the New window.
    ensurePalette(editor.doc, editor.sprite);
    const std::vector<PalettePreset>& presets = palettePresets();
    if (spec.preset >= 0 && spec.preset < static_cast<int>(presets.size())) {
        PaletteFile file;
        file.name = presets[static_cast<size_t>(spec.preset)].name;
        for (size_t n = 0; n < presets[static_cast<size_t>(spec.preset)].colours.size(); ++n) {
            file.entries.push_back({ static_cast<ls::ColorRole>(n),
                                     presets[static_cast<size_t>(spec.preset)].colours[n], "" });
        }
        int dropped = 0;
        applyPaletteFile(editor.doc, editor.sprite, file, &dropped);
    }

    // A background, when asked for: a layer of its own at the bottom, filled,
    // and through a palette slot where the colour is one -- so the backdrop
    // follows a palette swap like everything else.
    if (spec.background != 0) {
        const std::vector<PaletteEntry> entries = paletteEntries(editor.doc);
        Ink fill;
        fill.colour = spec.background == 1 ? ls::Color{ 255, 255, 255, 255 }
                    : spec.background == 2 ? ls::Color{ 0, 0, 0, 255 }
                    : (entries.empty() ? ls::Color{ 0, 0, 0, 255 } : entries.front().color);
        for (const PaletteEntry& entry : entries) {
            if (entry.color.r == fill.colour.r && entry.color.g == fill.colour.g &&
                entry.color.b == fill.colour.b && entry.color.a == fill.colour.a) {
                fill.role = entry.role;
                break;
            }
        }
        PaintLayer background;
        if (createPaintLayer(editor.doc, editor.sprite, "Background", fill.colour, &background)) {
            std::vector<ls::Vec2i> all;
            all.reserve(static_cast<size_t>(spec.width) * spec.height);
            for (uint32_t y = 0; y < spec.height; ++y) {
                for (uint32_t x = 0; x < spec.width; ++x) {
                    all.push_back({ static_cast<int32_t>(x), static_cast<int32_t>(y) });
                }
            }
            InkStroke stroke;
            if (beginInkStroke(editor.doc, background.layer, fill, &stroke)) {
                strokeInk(editor.doc, stroke, all);
                pruneEmptyInks(editor.doc, background.layer, stroke.target.fill);
            }
        }
    }

    PaintLayer layer;
    if (!createPaintLayer(editor.doc, editor.sprite, "Layer 1",
                          toColor(editor.color), &layer)) {
        return false;
    }
    resyncLayers(editor);
    selectLayer(editor, layer.layer);
    editor.doc.setUiState({});

    // Setting up a document is not editing it. Without this a brand-new file is
    // born dirty, and every File > New asks whether to save nothing -- and the
    // first layer is an undo entry, so Ctrl+Z removes the only layer.
    editor.doc.markUnmodified();
    editor.doc.clearHistory();
    editor.say("New " + std::to_string(spec.width) + " x " + std::to_string(spec.height) +
               " document");
    return true;
}

} // namespace fast
