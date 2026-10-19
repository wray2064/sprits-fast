// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_references.cpp -- the references panel and the library.
// Declared in panels.h; what the panel files share is in panels_internal.h.

#include "ui/panels_internal.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/reference_section.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/tile_tools.h"
#include "ui/ui_script.h"

#include "app/reference.h"
#include "app/library.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/palette_tools.h"
#include "app/shape.h"
#include "app/transform.h"
#include "app/tracks.h"
#include "app/tilemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fast {

// ------------------------------------------------------------- references --

void drawReferences(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                    ImVec2 origin, float zoom, bool behind) {
    for (const Reference& reference : editor.references) {
        if (!reference.visible || reference.behind != behind) {
            continue;
        }
        const ReferenceCache::Entry* entry =
            canvas.referenceTextures().entryFor(editor.doc, reference);
        if (entry == nullptr) {
            continue;
        }
        // Placement is in canvas pixels, so it lines up with the drawing at
        // every zoom and stays lined up when the view moves.
        // Only the part shown (Reference::clip*): its size on the canvas, and
        // its place in the image as the texture's coordinates.
        const ImVec2 at = canvas.pixelToScreen(origin, reference.x, reference.y);
        const ImVec2 corner = canvas.pixelToScreen(
            origin,
            reference.x + static_cast<float>(reference.shownWidth()) * reference.scale,
            reference.y + static_cast<float>(reference.shownHeight()) * reference.scale);
        const ImU32 tint = IM_COL32(255, 255, 255,
                                    static_cast<int>(reference.opacity * 255.f + 0.5f));
        const float w = static_cast<float>(std::max(1u, reference.width));
        const float h = static_cast<float>(std::max(1u, reference.height));
        const ImVec2 uv0(static_cast<float>(reference.shownX()) / w,
                         static_cast<float>(reference.shownY()) / h);
        const ImVec2 uv1(static_cast<float>(reference.shownX() + reference.shownWidth()) / w,
                         static_cast<float>(reference.shownY() + reference.shownHeight()) / h);
        // Magnified on screen -- a sprite sheet on a zoomed canvas nearly
        // always is -- a reference is drawn pixel for pixel, as the canvas is,
        // which also keeps a section from picking up the edge of the cell
        // beside it; a photograph shrunk to fit stays smooth.
        const bool sharp = reference.scale * zoom >= 1.f;
        if (sharp) {
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
        }
        draw->AddImage(reinterpret_cast<ImTextureID>(entry->texture), at, corner, uv0, uv1, tint);
        if (sharp) {
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
        }

        // The selected one gets a border, so "which am I moving" is answered
        // without hiding the others.
        if (reference.id == editor.activeReference && !reference.locked) {
            draw->AddRect(at, corner, IM_COL32(255, 255, 255, 70), 0.f, 0, 1.f);
        }
    }
    (void)zoom;
}

bool pasteReference(Editor& editor, CanvasView& canvas) {
    ls::RasterBuffer image;
    std::string error;
    if (!imageFromClipboard(&image, &error)) {
        editor.say("Nothing to paste as a reference: " + error);
        return false;
    }
    std::vector<uint8_t> png;
    Reference made;
    if (!encodeImageAsPng(image, &png, &error) ||
        !addReference(editor.doc, "Pasted " + std::to_string(image.width) + " x " +
                                      std::to_string(image.height),
                      png, &made, &error)) {
        editor.say("Could not keep the pasted image: " + error);
        return false;
    }
    resyncReferences(editor, canvas);
    editor.activeReference = made.id;
    // Floating, and opened when there is something in it to look at.
    editor.panels.references = true;
    canvas.invalidate();
    editor.say("Pasted as a reference -- it travels with this file");
    return true;
}

void drawReferencePanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    const char* const importLabels[] = { "Import image...", "Paste image" };
    const widgets::ButtonRow import = widgets::buttonRow(importLabels, 2);
    const float half = import.width;
    if (ImGui::Button(tr("Import image..."), ImVec2(half, 0.f))) {
        showImportReferenceDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("A photograph, a sketch, a pose to draw from. It is "
                          "stored in this document, so it travels with the "
                          "work and is never exported."));
    }
    widgets::beside(import, 1);
    ImGui::BeginDisabled(!clipboardHasImage());
    if (ImGui::Button(tr("Paste image"), ImVec2(half, 0.f))) {
        pasteReference(editor, canvas);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr("An image copied in a browser or another program, as a "
                          "reference."));
    }

    if (editor.references.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("Nothing to draw from yet. An image imported here "
                           "sits over or under the canvas at whatever opacity "
                           "you want, and changes no pixel of the artwork."));
        ImGui::PopStyleColor();
        return;
    }

    // The list. One row each: visible, name, remove.
    for (size_t i = 0; i < editor.references.size(); ++i) {
        Reference reference = editor.references[i];
        ImGui::PushID(reference.id.c_str());
        if (theme::eyeToggle("##visible", reference.visible, theme::px(16.f))) {
            reference.visible = !reference.visible;
            editor.doc.beginAction(reference.visible ? "Show reference"
                                                     : "Hide reference");
            updateReference(editor.doc, reference);
            editor.doc.endAction();
            resyncReferences(editor, canvas);
            ImGui::PopID();
            break;                    // the list was just rebuilt
        }
        ImGui::SameLine();
        const bool selected = reference.id == editor.activeReference;
        if (ImGui::Selectable(reference.name.c_str(), selected, 0,
                              ImVec2(ImGui::GetContentRegionAvail().x -
                                     (widgets::buttonWidth("x") +
                                      ImGui::GetStyle().ItemSpacing.x), 0.f))) {
            editor.activeReference = reference.id;
        }
        if (ImGui::IsItemHovered()) {
            if (reference.clipped()) {
                ImGui::SetTooltip("%u x %u of %u x %u", reference.shownWidth(),
                                  reference.shownHeight(), reference.width, reference.height);
            } else {
                ImGui::SetTooltip("%u x %u", reference.width, reference.height);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("x"))) {
            removeReference(editor.doc, reference);
            resyncReferences(editor, canvas);
            editor.say("Reference removed");
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    Reference* active = activeReference(editor);
    if (active == nullptr) {
        return;
    }

    // The selected one's placement. Every control writes straight through,
    // and the drag brackets are what make a drag one history entry.
    ImGui::Dummy(ImVec2(0.f, 4.f));
    Reference edited = *active;
    bool changed = false;

    ImGui::SetNextItemWidth(-1.f);
    if (widgets::sliderFloat("##opacity", &edited.opacity, 0.f, 1.f, "opacity %.2f")) {
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Reference opacity");

    float position[2] = { edited.x, edited.y };
    ImGui::SetNextItemWidth(-widgets::labelRoom("at"));
    if (ImGui::DragFloat2("at", position, 0.25f, 0.f, 0.f, "%.1f")) {
        edited.x = position[0];
        edited.y = position[1];
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Move reference");

    ImGui::SetNextItemWidth(-widgets::labelRoom(tr("scale")));
    if (ImGui::DragFloat(tr("scale"), &edited.scale, 0.01f, 0.01f, 64.f, "%.3f")) {
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Scale reference");

    if (ImGui::Button(tr("Fit"))) {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.ok() && size.value.x > 0 && size.value.y > 0) {
            fitReference(edited, static_cast<uint32_t>(size.value.x),
                         static_cast<uint32_t>(size.value.y));
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Centre it on the canvas at the largest size that fits."));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Section..."))) {
        openReferenceSection(editor, edited.id, false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Show only part of the image: a cell of a sprite sheet."));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("behind"), &edited.behind)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Under the artwork to trace from, or over it to "
                          "compare against."));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("lock"), &edited.locked)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("A locked reference will not move when dragged."));
    }

    if (changed) {
        // Always bracketed. While a slider is being dragged bracketDrag holds
        // an action open and this nests inside it, so the drag is one entry;
        // a checkbox or Fit, which no drag covers, gets an entry of its own
        // rather than folding into whatever happened to be open.
        editor.doc.beginAction("Place reference");
        updateReference(editor.doc, edited);
        editor.doc.endAction();
        *active = edited;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("%s", tr("Alt+drag on the canvas moves the selected reference."));
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- library --

namespace {

// Decodes per frame, so opening a folder of fifty sprites fills in over the
// next few frames rather than stalling one.
constexpr int kThumbnailsPerFrame = 4;

void drawFolderRow(Editor& editor, SDL_Window* window, bool references) {
    const std::string& folder = references ? editor.libraryFolders.references
                                           : editor.libraryFolders.project;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("%s", folder.empty() ? "No folder chosen." : folder.c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button(folder.empty() ? "Choose a folder..." : "Change folder...")) {
        const std::string startingAt = folder.empty() ? documentsDirectory() : folder;
        if (references) {
            showReferenceFolderDialog(editor.files, window, startingAt);
        } else {
            showProjectFolderDialog(editor.files, window, startingAt);
        }
    }
    if (!folder.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Reload"))) {
            editor.libraryStale = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Up"))) {
            const std::string parent = parentDirectory(folder);
            if (!parent.empty() && directoryExists(parent)) {
                (references ? editor.libraryFolders.references
                            : editor.libraryFolders.project) = parent;
                editor.libraryFolders.save();
                editor.libraryStale = true;
            }
        }
    }
}

// One tile: a picture, a name, and what a click does.
bool drawTile(CanvasView& canvas, const ReferenceCache::Entry* picture,
              const char* name, bool selected, float size) {
    ImGui::PushID(name);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(
        "tile", ImVec2(size, size + ImGui::GetTextLineHeight() + theme::px(4.f)));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const theme::Palette& c = theme::palette();

    draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size),
                        ImGui::GetColorU32(c.canvasBackground),
                        theme::metrics().rounding);
    if (picture != nullptr && picture->width > 0 && picture->height > 0) {
        const float fit = std::min(size / static_cast<float>(picture->width),
                                   size / static_cast<float>(picture->height));
        const float w = static_cast<float>(picture->width) * fit;
        const float h = static_cast<float>(picture->height) * fit;
        const ImVec2 origin(at.x + (size - w) * 0.5f, at.y + (size - h) * 0.5f);
        draw->AddImage(reinterpret_cast<ImTextureID>(picture->texture), origin,
                       ImVec2(origin.x + w, origin.y + h));
    } else {
        // No thumbnail: a file saved before they existed, or one still being
        // read. An empty square says "nothing yet" rather than "broken".
        draw->AddRect(at, ImVec2(at.x + size, at.y + size),
                      ImGui::GetColorU32(c.border), theme::metrics().rounding);
    }
    if (selected || hovered) {
        draw->AddRect(at, ImVec2(at.x + size, at.y + size),
                      selected ? ImGui::ColorConvertFloat4ToU32(c.accent)
                               : ImGui::GetColorU32(c.text),
                      theme::metrics().rounding, 0, selected ? 2.f : 1.f);
    }
    draw->AddText(ImVec2(at.x, at.y + size + theme::px(3.f)),
                  ImGui::GetColorU32(hovered ? c.text : c.textDim), name);
    ImGui::PopID();
    (void)canvas;
    return clicked;
}

} // namespace

void drawLibraryPanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.libraryOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(theme::px(520.f), theme::px(420.f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Library", &editor.libraryOpen)) {
        ImGui::End();
        return;
    }
    if (widgets::escapePutsAway()) {
        editor.libraryOpen = false;
    }

    if (ImGui::BeginTabBar("libraryTabs")) {
        const bool sprites = ImGui::BeginTabItem(tr("Sprites"));
        if (sprites) {
            if (editor.libraryShowingReferences) {
                editor.libraryShowingReferences = false;
                editor.libraryStale = true;
            }
            ImGui::EndTabItem();
        }
        const bool images = ImGui::BeginTabItem(tr("References"));
        if (images) {
            if (!editor.libraryShowingReferences) {
                editor.libraryShowingReferences = true;
                editor.libraryStale = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (editor.libraryStale) {
        refreshLibrary(editor);
    }
    drawFolderRow(editor, window, editor.libraryShowingReferences);
    ImGui::Separator();

    const float tile = theme::px(84.f);
    const float step = tile + theme::metrics().itemSpacing;
    const int perRow = std::max(1, static_cast<int>(
        (ImGui::GetContentRegionAvail().x + theme::metrics().itemSpacing) / step));
    int budget = kThumbnailsPerFrame;
    int column = 0;

    ImGui::BeginChild("items", ImVec2(0.f, 0.f), false);
    if (editor.libraryShowingReferences) {
        std::vector<std::string> live;
        for (const LibraryImage& image : editor.libraryImages) {
            live.push_back(image.path);
        }
        canvas.libraryThumbnails().retainOnlyKeys(live);

        for (const LibraryImage& image : editor.libraryImages) {
            const ReferenceCache::Entry* picture = nullptr;
            if (canvas.libraryThumbnails().holds(image.path) || budget > 0) {
                std::vector<uint8_t> bytes;
                std::string error;
                if (canvas.libraryThumbnails().holds(image.path)) {
                    picture = canvas.libraryThumbnails().entryForBytes(image.path, bytes);
                } else if (readFile(image.path, bytes, &error)) {
                    picture = canvas.libraryThumbnails().entryForBytes(image.path, bytes,
                                                                       &budget);
                }
            }
            if (column > 0 && column < perRow) {
                ImGui::SameLine(0.f, theme::metrics().itemSpacing);
            }
            if (drawTile(canvas, picture, image.name.c_str(), false, tile)) {
                std::vector<uint8_t> bytes;
                std::string error;
                Reference made;
                if (readFile(image.path, bytes, &error) &&
                    addReference(editor.doc, image.name, bytes, &made, &error)) {
                    resyncReferences(editor, canvas);
                    editor.activeReference = made.id;
                    // Floating, and opened when there is something in it to look at.
                    editor.panels.references = true;
                    editor.say("Imported " + made.name + " as a reference");
                } else {
                    editor.say("Could not import " + image.name + ": " + error);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nClick to bring it into this document as a "
                                  "reference.", image.path.c_str());
            }
            column = (column + 1) % perRow;
        }
        if (editor.libraryImages.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s", tr("No images in this folder. Point it at wherever "
                               "you keep the pictures you draw from -- the same "
                               "pose sheet gets used across a dozen sprites."));
            ImGui::PopStyleColor();
        }
    } else {
        std::vector<std::string> live;
        for (const LibraryDocument& document : editor.libraryDocuments) {
            live.push_back(document.path);
        }
        canvas.libraryThumbnails().retainOnlyKeys(live);

        for (const LibraryDocument& document : editor.libraryDocuments) {
            const ReferenceCache::Entry* picture = nullptr;
            if (canvas.libraryThumbnails().holds(document.path)) {
                std::vector<uint8_t> none;
                picture = canvas.libraryThumbnails().entryForBytes(document.path, none);
            } else if (budget > 0) {
                std::vector<uint8_t> png;
                if (readThumbnail(document.path, &png)) {
                    picture = canvas.libraryThumbnails().entryForBytes(document.path, png,
                                                                       &budget);
                } else {
                    --budget;         // the read cost something even with no picture
                }
            }
            const bool isOpen = document.path == editor.doc.path();
            if (column > 0 && column < perRow) {
                ImGui::SameLine(0.f, theme::metrics().itemSpacing);
            }
            if (drawTile(canvas, picture, document.name.c_str(), isOpen, tile) && !isOpen) {
                // Through the same door a recent entry uses, so an unsaved
                // document still gets asked about first.
                requestAction(editor, canvas, window, PendingAction::OpenPath,
                              document.path);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", document.path.c_str(),
                                  isOpen ? "Open in this window."
                                         : "Click to open.");
            }
            column = (column + 1) % perRow;
        }
        if (editor.libraryDocuments.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s", tr("No sprites in this folder. A project here is "
                               "just a folder: point it at the one your work "
                               "lives in and everything beside this file is one "
                               "click away."));
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}


} // namespace fast
