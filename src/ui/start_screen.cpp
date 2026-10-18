// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/start_screen.h"

#include "app/file_io.h"
#include "app/i18n.h"
#include "app/image_io.h"
#include "app/templates.h"
#include "ui/tabs.h"
#include "ui/theme.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <ctime>

namespace fast {

namespace {

constexpr const char* kPopup = "Start";

// What the program is called and its picture, as the window has them; the
// logo becomes a texture the first time it is drawn, because only then is a
// renderer to hand. A logo that will not decode is simply not drawn.
struct Branding {
    std::string          title = "Sprit's'fast";
    std::vector<uint8_t> logoPng;
    SDL_Texture*         logo = nullptr;
    bool                 tried = false;
};

Branding& branding() {
    static Branding b;
    return b;
}

SDL_Texture* logoTexture(SDL_Window* window) {
    Branding& b = branding();
    if (b.tried || b.logoPng.empty()) {
        return b.logo;
    }
    b.tried = true;
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    ls::RasterBuffer image;
    std::string why;
    if (renderer == nullptr || !decodeImage(b.logoPng, &image, &why) || image.empty()) {
        return nullptr;
    }
    b.logo = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                               static_cast<int>(image.width), static_cast<int>(image.height));
    if (b.logo != nullptr) {
        SDL_UpdateTexture(b.logo, nullptr, image.pixels.data(), static_cast<int>(image.stride));
        SDL_SetTextureBlendMode(b.logo, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(b.logo, SDL_SCALEMODE_LINEAR);
    }
    return b.logo;
}

// A new file from a template is File > New with that template's answers, so
// the New window keeps what the person last chose there: the choice is put
// back once the document is made.
void newFromTemplate(Editor& editor, CanvasView& canvas, SDL_Window* window,
                     const NewTemplate& t) {
    const FileState::NewDocument kept = editor.files.newDocument;
    editor.files.newDocument.width = t.width;
    editor.files.newDocument.height = t.height;
    editor.files.newDocument.background = t.background;
    editor.files.newDocument.preset = t.preset;
    requestAction(editor, canvas, window, PendingAction::NewDocument);
    editor.files.newDocument = kept;
}

void drawActions(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGui::TextColored(theme::palette().textDim, "%s", tr("Start"));
    ImGui::Dummy(ImVec2(0.f, 2.f));

    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(tr("New file..."), ImVec2(width, 38.f))) {
        editor.startOpen = false;
        editor.newDocumentOpen = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.f, 10.f));
    if (ImGui::Button(tr("Open..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        requestAction(editor, canvas, window, PendingAction::OpenDialog);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(tr("Import sprite sheet..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        showImportSheetDialog(editor.files, window, editor.doc);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(tr("Library..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        editor.libraryOpen = true;
        editor.libraryStale = true;
        ImGui::CloseCurrentPopup();
    }
}

// One card: its label (which a script finds it by), and under it the shape
// of the canvas it makes, in its proportions (each fitted to the card, not to
// one another), on the chequer the canvas will show.
// Shapes are drawn, not pictured: a preview is a rectangle and a few squares.
void drawCard(const Editor& editor, const NewTemplate& t, const ImVec2& size, bool* chosen) {
    const std::string label = templateLabel(t);
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable(label.c_str(), false, 0, size)) {
        *chosen = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float labelHeight = ImGui::GetTextLineHeightWithSpacing() + 4.f;
    const ImVec2 box(size.x - 16.f, size.y - labelHeight - 10.f);
    const float scale = std::min(box.x / static_cast<float>(t.width),
                                 box.y / static_cast<float>(t.height));
    const ImVec2 shape(static_cast<float>(t.width) * scale, static_cast<float>(t.height) * scale);
    const ImVec2 from(corner.x + (size.x - shape.x) * 0.5f,
                      corner.y + labelHeight + (box.y - shape.y) * 0.5f + 2.f);
    const ImVec2 to(from.x + shape.x, from.y + shape.y);

    const auto rgb = [](uint32_t c) {
        return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
    };
    const ImU32 light = rgb(editor.prefs.checkerLight);
    const ImU32 dark = rgb(editor.prefs.checkerDark);
    draw->AddRectFilled(from, to, dark);
    const float square = std::max(4.f, std::min(shape.x, shape.y) / 6.f);
    draw->PushClipRect(from, to, true);
    for (float y = 0.f; y < shape.y; y += square) {
        for (float x = 0.f; x < shape.x; x += square) {
            if ((static_cast<int>(x / square) + static_cast<int>(y / square)) % 2 == 0) {
                draw->AddRectFilled(ImVec2(from.x + x, from.y + y),
                                    ImVec2(from.x + x + square, from.y + y + square), light);
            }
        }
    }
    draw->PopClipRect();
    draw->AddRect(from, to, ImGui::GetColorU32(theme::palette().textDim));
}

void drawTemplates(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGui::TextColored(theme::palette().textDim, "%s", tr("New from a template"));
    ImGui::Dummy(ImVec2(0.f, 2.f));

    const ImVec2 card(150.f, 112.f);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((available + gap) / (card.x + gap)));
    int column = 0;
    const NewTemplate* chosen = nullptr;
    for (const NewTemplate& t : newTemplates()) {
        if (column > 0 && column < perRow) {
            ImGui::SameLine();
        }
        ImGui::PushID(t.name.c_str());
        bool picked = false;
        drawCard(editor, t, card, &picked);
        ImGui::PopID();
        if (picked) {
            chosen = &t;
        }
        column = (column + 1) % perRow;
    }
    if (chosen != nullptr) {
        editor.startOpen = false;
        newFromTemplate(editor, canvas, window, *chosen);
        ImGui::CloseCurrentPopup();
    }
}

void drawRecent(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGui::TextColored(theme::palette().textDim, "%s", tr("Recent"));
    ImGui::Dummy(ImVec2(0.f, 2.f));
    if (editor.startRecent.empty()) {
        ImGui::TextDisabled("%s", tr("Files you open or save appear here."));
        return;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::string chosen;
    std::string removed;
    for (size_t i = 0; i < editor.startRecent.size(); ++i) {
        const RecentInfo& info = editor.startRecent[i];
        ImGui::PushID(static_cast<int>(i));

        std::string label = fileName(info.path);
        if (!info.exists) {
            label += " (missing)";
        }
        ImGui::BeginDisabled(!info.exists);
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0.f, 0.f))) {
            chosen = info.path;
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopupContextItem("recentRow")) {
            if (ImGui::MenuItem(tr("Remove from this list"))) {
                removed = info.path;
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", info.path.c_str());
        }

        std::string detail = directoryOf(info.path);
        const std::string age = ageText(info.modifiedSeconds, now);
        if (!age.empty()) {
            detail += detail.empty() ? age : "   " + age;
        }
        ImGui::TextDisabled("%s", detail.c_str());
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (ImGui::SmallButton(tr("Clear list"))) {
        editor.files.recent.clear();
        editor.files.recent.save();
        editor.startRecent.clear();
    }

    if (!removed.empty()) {
        editor.files.recent.remove(removed);
        editor.files.recent.save();
        editor.startRecent.erase(
            std::remove_if(editor.startRecent.begin(), editor.startRecent.end(),
                           [&](const RecentInfo& info) { return info.path == removed; }),
            editor.startRecent.end());
    }
    if (!chosen.empty()) {
        editor.startOpen = false;
        requestAction(editor, canvas, window, PendingAction::OpenPath, chosen);
        ImGui::CloseCurrentPopup();
    }
}

} // namespace

void configureStartScreen(const std::string& title, const std::vector<uint8_t>& logoPng) {
    Branding& b = branding();
    b.title = title;
    b.logoPng = logoPng;
    b.tried = false;
    b.logo = nullptr;       // a texture belongs to a renderer; the old one is not reused
}

void openStartScreen(Editor& editor) {
    editor.startOpen = true;
    editor.startStale = true;
    editor.startTabs = tabCount(editor);
    editor.startPath = editor.doc.path();
}

void drawStartScreen(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.startOpen || editor.askingToRecover) {
        return;
    }
    // Something else changed the document under it -- a file dropped on the
    // window, say -- so the choice has been made.
    if (tabCount(editor) != editor.startTabs || editor.doc.path() != editor.startPath) {
        editor.startOpen = false;
        return;
    }
    if (editor.startStale) {
        editor.startRecent = describeRecent(editor.files.recent.entries());
        editor.startStale = false;
    }

    if (!ImGui::IsPopupOpen(kPopup)) {
        ImGui::OpenPopup(kPopup);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(std::min(900.f, viewport->WorkSize.x * 0.94f),
                      std::min(640.f, viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::BeginPopupModal(kPopup, nullptr, flags)) {
        return;
    }

    // The header: the logo, and the program's name beside it.
    const float logoSize = 56.f;
    if (SDL_Texture* logo = logoTexture(window)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(logo), at,
                                             ImVec2(at.x + logoSize, at.y + logoSize));
        ImGui::Dummy(ImVec2(logoSize, logoSize));
        ImGui::SameLine(0.f, 14.f);
    }
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.f, logoSize * 0.5f - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::TextColored(theme::palette().textBright, "%s", branding().title.c_str());
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 6.f));

    const float footer = 56.f;
    const float actionsWidth = 220.f;
    ImGui::BeginChild("startActions", ImVec2(actionsWidth, -footer), false);
    drawActions(editor, canvas, window);
    ImGui::EndChild();
    ImGui::SameLine(0.f, 24.f);
    ImGui::BeginChild("startRight", ImVec2(0.f, -footer), false);
    drawTemplates(editor, canvas, window);
    ImGui::Dummy(ImVec2(0.f, 10.f));
    ImGui::BeginChild("startRecent", ImVec2(0.f, 0.f), false);
    drawRecent(editor, canvas, window);
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (ImGui::Checkbox(tr("Show this screen at start"), &editor.prefs.showStart)) {
        saveSettings(editor);
    }
    const char* leave = tr("Start with a blank canvas");
    const float leaveWidth = ImGui::CalcTextSize(leave).x + 32.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - leaveWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button(leave, ImVec2(leaveWidth, 0.f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        editor.startOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace fast
