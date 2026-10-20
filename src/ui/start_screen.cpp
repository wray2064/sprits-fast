// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/start_screen.h"

#include "app/file_io.h"
#include "app/i18n.h"
#include "app/image_io.h"
#include "app/library.h"
#include "app/templates.h"
#include "app/tips.h"
#include "ui/new_document.h"
#include "ui/reference_cache.h"
#include "ui/start_art.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/thumbnails.h"
#include "ui/user_guide.h"
#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

namespace fast {

namespace {

constexpr const char* kPopup = "Start";

// The program's own name, links and picture, as configureProgram was given
// them; the logo becomes a texture the first time it is wanted, because only
// then is a renderer to hand. A logo that will not decode is simply not drawn.
struct Branding {
    ProgramInfo          info;
    std::vector<uint8_t> logoPng;
    SDL_Texture*         logo = nullptr;
    bool                 tried = false;
};

Branding& branding() {
    static Branding b;
    return b;
}

// The start screen asks the New window's questions in a panel of its own, over
// the very same answers (Editor::files.newDocument): a template card fills
// them in, the fields change them, and Create makes the document from them.
void createFromAnswers(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    editor.startOpen = false;
    requestAction(editor, canvas, window, PendingAction::NewDocument);
    ImGui::CloseCurrentPopup();
}

bool sameAsTemplate(const FileState::NewDocument& spec, const NewTemplate& t) {
    return spec.width == t.width && spec.height == t.height &&
           spec.background == t.background && spec.preset == t.preset;
}

// Every size on this screen is drawn at the person's interface size: the
// pixel counts below are for 100%, and k() is what turns them into the ones used.
float k() {
    return theme::scale();
}

// --- what every card is made of --------------------------------------------------------

ImU32 u32(const ImVec4& colour) {
    return ImGui::GetColorU32(colour);
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// A rounded card, a step lighter than the surface it is on, with a hairline; lit when
// the pointer is over it and ringed in the accent when it is the one chosen.
void drawCardFrame(ImDrawList* draw, ImVec2 a, ImVec2 b, bool hovered, bool selected) {
    const theme::Palette& c = theme::palette();
    const float rounding = 6.f * k();
    const ImVec4 rest = mix(c.panelBackground, c.text, 0.04f);
    const ImVec4 fill = selected ? c.accentDim : hovered ? mix(c.panelBackground, c.text, 0.10f) : rest;
    draw->AddRectFilled(a, b, u32(fill), rounding);
    draw->AddRect(a, b, u32(selected ? c.accent : hovered ? c.textDim : c.border), rounding);
}

// `text` cut to fit `width`, with an ellipsis, never inside a UTF-8 character.
std::string fitted(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return text;
    }
    std::string cut = text;
    while (!cut.empty() && ImGui::CalcTextSize((cut + "...").c_str()).x > width) {
        size_t i = cut.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(cut[i]) & 0xC0) == 0x80) {
            --i;
        }
        cut.resize(i);
    }
    return cut + "...";
}

// Text wrapped at `width`, drawn at `at`; the height it took.
float wrappedText(ImDrawList* draw, ImVec2 at, float width, ImU32 colour, const char* text) {
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, colour, text, nullptr, width);
    return ImGui::CalcTextSize(text, nullptr, false, width).y;
}

// One big thing to press: an icon, what it does, and a line about it. `id` is its
// English name, which a script clicks it by.
bool actionCard(const std::string& id, theme::Icon icon, const char* title, const char* subtitle,
                float width, float height) {
    const theme::Palette& c = theme::palette();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton((id + "##start").c_str(), ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawCardFrame(draw, at, ImVec2(at.x + width, at.y + height), hovered, false);

    const float pad = 14.f * k();
    const float iconSize = 26.f * k();
    theme::drawIcon(draw, icon, ImVec2(at.x + pad, at.y + (height - iconSize) * 0.5f), iconSize,
                    u32(hovered ? c.accent : mix(c.accent, c.textDim, 0.35f)));
    const float textX = at.x + pad + iconSize + 12.f * k();
    const float textWidth = at.x + width - pad - textX;

    theme::pushText(theme::TextStyle::Strong);
    const float titleHeight = ImGui::GetTextLineHeight();
    theme::popText();
    theme::pushText(theme::TextStyle::Caption);
    const float captionHeight = ImGui::CalcTextSize(tr(subtitle), nullptr, false, textWidth).y;
    theme::popText();
    const float block = titleHeight + 2.f * k() + captionHeight;
    float y = at.y + (height - block) * 0.5f;

    theme::pushText(theme::TextStyle::Strong);
    draw->AddText(ImVec2(textX, y), u32(c.textBright), fitted(tr(title), textWidth).c_str());
    theme::popText();
    y += titleHeight + 2.f * k();
    theme::pushText(theme::TextStyle::Caption);
    wrappedText(draw, ImVec2(textX, y), textWidth, u32(c.textDim), tr(subtitle));
    theme::popText();
    return clicked;
}

// --- the header ---------------------------------------------------------------------------

void drawHeader(SDL_Window* window) {
    const theme::Palette& c = theme::palette();
    const ProgramInfo& info = branding().info;
    const float logoSize = 56.f * k();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (SDL_Texture* logo = programLogo(window)) {
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(logo), top,
                                             ImVec2(top.x + logoSize, top.y + logoSize));
    }
    ImGui::Dummy(ImVec2(logoSize, logoSize));
    ImGui::SameLine(0.f, 16.f * k());

    ImGui::BeginGroup();
    theme::pushText(theme::TextStyle::Title);
    const float titleHeight = ImGui::GetTextLineHeight();
    theme::popText();
    theme::pushText(theme::TextStyle::Label);
    const float tagHeight = ImGui::GetTextLineHeight();
    theme::popText();
    ImGui::Dummy(ImVec2(0.f, (logoSize - titleHeight - tagHeight - 4.f * k()) * 0.5f));
    theme::pushText(theme::TextStyle::Title);
    ImGui::TextColored(c.textBright, "%s", info.title.c_str());
    theme::popText();
    if (!info.version.empty()) {
        ImGui::SameLine(0.f, 10.f * k());
        widgets::badge(info.version.c_str(), widgets::Tone::Accent);
    }
    theme::pushText(theme::TextStyle::Label);
    ImGui::TextColored(c.textDim, "%s", tr("A sprite editor where every stroke stays a stroke."));
    theme::popText();
    ImGui::EndGroup();
}

// --- the left column: where to begin ------------------------------------------------------

void drawActions(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    theme::sectionHeader("Start");
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 60.f * k();
    const float gap = 6.f * k();

    if (actionCard("Open...", theme::Icon::FolderOpen, "Open...",
                   "A sprite, a picture or an Aseprite file", width, height)) {
        editor.startOpen = false;
        requestAction(editor, canvas, window, PendingAction::OpenDialog);
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.f, gap - ImGui::GetStyle().ItemSpacing.y));
    if (actionCard("Import sprite sheet...", theme::Icon::Sheet, "Import sprite sheet...",
                   "Cut a sheet of frames into an animation", width, height)) {
        editor.startOpen = false;
        showImportSheetDialog(editor.files, window, editor.doc);
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.f, gap - ImGui::GetStyle().ItemSpacing.y));
    if (actionCard("Library...", theme::Icon::Images, "Library...",
                   "Browse your sprites and reference images", width, height)) {
        editor.startOpen = false;
        editor.libraryOpen = true;
        editor.libraryStale = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.f, gap - ImGui::GetStyle().ItemSpacing.y));
    if (actionCard("User guide", theme::Icon::Book, "User guide",
                   "Learn the tools. F1 opens it anywhere", width, height)) {
        editor.startOpen = false;
        openUserGuide(editor);
        ImGui::CloseCurrentPopup();
    }
}

// --- the middle: a new canvas, and what was being worked on --------------------------

// One preset: its name and size, and the canvas it makes in its proportions (each fitted
// to the card, not to one another) on the chequer the canvas will show, with a picture of
// what the size is for. `chosen` is set by a press, `doubled` by a double press.
void drawCard(const Editor& editor, const NewTemplate& t, const ImVec2& size, bool selected,
              bool* chosen, bool* doubled) {
    const theme::Palette& c = theme::palette();
    const std::string label = templateLabel(t);
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label.c_str(), size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *chosen = true;
        *doubled = true;
    } else if (pressed) {
        *chosen = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawCardFrame(draw, corner, ImVec2(corner.x + size.x, corner.y + size.y), hovered, selected);

    // The name on the left, the size on the right.
    const float pad = 10.f * k();
    theme::pushText(theme::TextStyle::Strong);
    draw->AddText(ImVec2(corner.x + pad, corner.y + 8.f * k()), u32(c.textBright), tr(t.name.c_str()));
    const float nameHeight = ImGui::GetTextLineHeight();
    theme::popText();
    const std::string dims = std::to_string(t.width) + " x " + std::to_string(t.height);
    theme::pushText(theme::TextStyle::Strong);
    const float nameWidth = ImGui::CalcTextSize(tr(t.name.c_str())).x;
    theme::popText();
    theme::pushText(theme::TextStyle::Caption);
    const float dimsWidth = ImGui::CalcTextSize(dims.c_str()).x;
    // Where the card is too narrow for both (a large interface size), the name wins:
    // the size is also in the form beside the cards.
    if (pad + nameWidth + 8.f * k() + dimsWidth + pad <= size.x) {
        draw->AddText(ImVec2(corner.x + size.x - pad - dimsWidth,
                             corner.y + 8.f * k() + nameHeight - ImGui::GetTextLineHeight() - 1.f),
                      u32(c.textDim), dims.c_str());
    }
    theme::popText();

    const float labelHeight = nameHeight + 8.f * k() + 6.f * k();
    const ImVec2 box(size.x - 2.f * pad, size.y - labelHeight - pad);
    const float scale = std::min(box.x / static_cast<float>(t.width),
                                 box.y / static_cast<float>(t.height));
    const ImVec2 shape(static_cast<float>(t.width) * scale, static_cast<float>(t.height) * scale);
    const ImVec2 from(corner.x + (size.x - shape.x) * 0.5f,
                      corner.y + labelHeight + (box.y - shape.y) * 0.5f);
    const ImVec2 to(from.x + shape.x, from.y + shape.y);

    const auto rgb = [](uint32_t colour) {
        return IM_COL32((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF, 255);
    };
    const ImU32 light = rgb(editor.prefs.checkerLight);
    const ImU32 dark = rgb(editor.prefs.checkerDark);
    draw->AddRectFilled(from, to, dark);
    const float square = std::max(4.f, std::min(shape.x, shape.y) / 8.f);
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
    drawPresetArt(draw, t.name, from, to);
    draw->AddRect(from, to, u32(c.textDim));
}

void drawTemplates(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    theme::sectionHeader("Presets");

    const float gap = 10.f * k();
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = 3;
    const float width = std::floor((available - gap * static_cast<float>(perRow - 1)) /
                                   static_cast<float>(perRow));
    const ImVec2 card(width, 124.f * k());
    int column = 0;
    const NewTemplate* chosen = nullptr;
    bool doubled = false;
    for (const NewTemplate& t : newTemplates()) {
        if (column > 0) {
            ImGui::SameLine(0.f, gap);
        }
        ImGui::PushID(t.name.c_str());
        bool picked = false;
        drawCard(editor, t, card, sameAsTemplate(editor.files.newDocument, t), &picked, &doubled);
        ImGui::PopID();
        if (picked) {
            chosen = &t;
        }
        column = (column + 1) % perRow;
    }
    if (chosen != nullptr) {
        // A card fills in the details beside it, as in Photoshop; a double
        // click goes on to make the document.
        FileState::NewDocument& spec = editor.files.newDocument;
        spec.width = chosen->width;
        spec.height = chosen->height;
        spec.background = chosen->background;
        spec.preset = chosen->preset;
        if (doubled) {
            createFromAnswers(editor, canvas, window);
        }
    }
}

// What a person can do with nothing recent to go back to: three ways into the guide, so
// the first screen of a first run says where to begin instead of saying "nothing here".
void drawGettingStarted(Editor& editor) {
    const float gap = 10.f * k();
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("%s", tr("Nothing opened yet. A good place to begin:"));
    ImGui::Dummy(ImVec2(0.f, 4.f * k()));
    struct Way { const char* id; theme::Icon icon; const char* title; const char* about; const char* topic; };
    const Way ways[] = {
        { "Your first sprite", theme::Icon::Pencil, "Your first sprite",
          "Draw, save and export in a few minutes", "getting-started" },
        { "Make it move", theme::Icon::Move, "Make it move",
          "Frames, cycles and the timeline", "animation" },
        { "Learn the keys", theme::Icon::Keyboard, "Learn the keys",
          "Every shortcut, and how to change it", "shortcuts" },
    };
    const float width = std::floor((available - gap * 2.f) / 3.f);
    for (size_t i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.f, gap);
        }
        if (actionCard(ways[i].id, ways[i].icon, ways[i].title, ways[i].about, width, 70.f * k())) {
            editor.startOpen = false;
            openUserGuide(editor, ways[i].topic);
            ImGui::CloseCurrentPopup();
        }
    }
}

void drawRecent(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    theme::sectionHeader("Recent");
    if (editor.startRecent.empty()) {
        drawGettingStarted(editor);
        return;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const theme::Palette& c = theme::palette();
    std::string chosen;
    std::string removed;
    // A few thumbnails are decoded a frame, so a list of ten fills in over the
    // next few frames instead of stalling the one that opened the screen.
    int budget = 3;
    const float gap = 10.f * k();
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((available + gap) / (230.f * k() + gap)));
    const float width = std::floor((available - gap * static_cast<float>(perRow - 1)) /
                                   static_cast<float>(perRow));
    // A picture on the left, and on the right what and where and when: wide cards, so two
    // rows of them fit under the presets without hiding anything.
    const float pad = 8.f * k();
    const float thumb = 56.f * k();
    theme::pushText(theme::TextStyle::Label);
    const float lineHeight = ImGui::GetTextLineHeight();
    theme::popText();
    const float height = thumb + 2.f * pad;

    for (size_t i = 0; i < editor.startRecent.size(); ++i) {
        const RecentInfo& info = editor.startRecent[i];
        if (i % static_cast<size_t>(perRow) != 0) {
            ImGui::SameLine(0.f, gap);
        }
        ImGui::PushID(static_cast<int>(i));

        std::string label = fileName(info.path);
        if (!info.exists) {
            label += " (missing)";
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!info.exists);
        const bool pressed = ImGui::InvisibleButton((label + "##recent").c_str(), ImVec2(width, height));
        ImGui::EndDisabled();
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        if (pressed) {
            chosen = info.path;
        }
        if (ImGui::BeginPopupContextItem("recentRow")) {
            if (ImGui::MenuItem(tr("Remove from this list"))) {
                removed = info.path;
            }
            ImGui::EndPopup();
        }
        if (hovered) {
            ImGui::SetTooltip("%s", info.path.c_str());
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        drawCardFrame(draw, at, ImVec2(at.x + width, at.y + height), hovered && info.exists, false);
        paintDocumentThumbnail(canvas, info.path, ImVec2(at.x + pad, at.y + pad), thumb,
                               info.exists, &budget);
        theme::pushText(theme::TextStyle::Label);
        const float textX = at.x + pad + thumb + 10.f * k();
        const float textWidth = at.x + width - pad - textX;
        const float block = lineHeight * 3.f + 2.f * k();
        float y = at.y + (height - block) * 0.5f;
        draw->AddText(ImVec2(textX, y), u32(info.exists ? c.textBright : c.textDim),
                      fitted(label, textWidth).c_str());
        y += lineHeight + 2.f * k();
        const std::string age = ageText(info.modifiedSeconds, now);
        draw->AddText(ImVec2(textX, y), u32(c.text), fitted(age.empty() ? "-" : age, textWidth).c_str());
        y += lineHeight;
        draw->AddText(ImVec2(textX, y), u32(c.textDim),
                      fitted(directoryOf(info.path), textWidth).c_str());
        theme::popText();
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

// --- the right: the details of the document about to be made -----------------------------

// Every question File > New asks, answered here, with the presets only a quick way to
// fill them in. True when the answers make a document (Create is enabled by it).
bool drawNewDocumentFieldsCard(Editor& editor) {
    theme::sectionHeader("New document");
    return drawNewDocumentFields(editor.files.newDocument);
}

// --- the foot: one thing worth knowing ---------------------------------------------------------

void drawTip(Editor& editor) {
    const theme::Palette& c = theme::palette();
    const float pad = 14.f * k();
    const float iconSize = 22.f * k();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float buttonWidth = widgets::buttonWidth("Next tip");
    const float textX = at.x + pad + iconSize + 12.f * k();
    const float textWidth = at.x + width - pad - buttonWidth - 12.f * k() - textX;
    const std::string tip = tr(tipAt(editor.startTip).c_str());

    theme::pushText(theme::TextStyle::Caption);
    const float captionHeight = ImGui::GetTextLineHeight();
    theme::popText();
    const float tipHeight = ImGui::CalcTextSize(tip.c_str(), nullptr, false, textWidth).y;
    const float height = std::max(48.f * k(), pad + captionHeight + 2.f * k() + tipHeight + pad);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawCardFrame(draw, at, ImVec2(at.x + width, at.y + height), false, false);
    theme::drawIcon(draw, theme::Icon::Bulb, ImVec2(at.x + pad, at.y + (height - iconSize) * 0.5f),
                    iconSize, u32(c.accent));
    theme::pushText(theme::TextStyle::Caption);
    draw->AddText(ImVec2(textX, at.y + pad), u32(c.textDim), tr("Did you know"));
    theme::popText();
    wrappedText(draw, ImVec2(textX, at.y + pad + captionHeight + 2.f * k()), textWidth,
                u32(c.text), tip.c_str());

    ImGui::SetCursorScreenPos(ImVec2(at.x + width - pad - buttonWidth,
                                     at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    if (widgets::button("Next tip")) {
        ++editor.startTip;
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void configureProgram(const AppExtension& extension, const std::vector<uint8_t>& logoPng) {
    Branding& b = branding();
    b.info.title = extension.title;
#ifdef FAST_VERSION
    b.info.version = extension.version.empty() ? std::string(FAST_VERSION) : extension.version;
#else
    b.info.version = extension.version;
#endif
    b.info.homepage = extension.homepage;
    b.info.issues = extension.issues;
    b.info.licence = extension.licence;
    b.logoPng = logoPng;
    b.tried = false;
    b.logo = nullptr;       // a texture belongs to a renderer; the old one is not reused
}

const ProgramInfo& programInfo() {
    return branding().info;
}

SDL_Texture* programLogo(SDL_Window* window) {
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

void openStartScreen(Editor& editor) {
    // The tip it opens on, and the next one kept for the next time.
    editor.startTip = editor.prefs.tipIndex;
    editor.prefs.tipIndex = (editor.prefs.tipIndex + 1) % 1000000;
    saveSettings(editor);
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
    const ImVec2 size(std::min(1180.f * k(), viewport->WorkSize.x * 0.94f),
                      std::min(720.f * k(), viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::BeginPopupModal(kPopup, nullptr, flags)) {
        return;
    }

    drawHeader(window);
    ImGui::Dummy(ImVec2(0.f, 6.f * k()));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 8.f * k()));

    // What is under the three columns: the tip, and the way out.
    const float footer = 120.f * k();
    const float actionsWidth = 250.f * k();
    const float gutter = 24.f * k();
    ImGui::BeginChild("startActions", ImVec2(actionsWidth, -footer), false);
    drawActions(editor, canvas, window);
    ImGui::EndChild();
    ImGui::SameLine(0.f, gutter);
    const float detailsWidth = 290.f * k();
    ImGui::BeginChild("startMiddle", ImVec2(-(detailsWidth + gutter), -footer), false);
    drawTemplates(editor, canvas, window);
    ImGui::Dummy(ImVec2(0.f, 8.f * k()));
    ImGui::BeginChild("startRecent", ImVec2(0.f, 0.f), false);
    drawRecent(editor, canvas, window);
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine(0.f, gutter);
    // The details sit on a card of their own: it is the one place on the screen with
    // fields, and they should read as one thing. Create is under the card, outside what
    // scrolls, so that at a large interface size on a small screen the button that makes
    // the document is never the part below the edge.
    const float createHeight = 38.f * k();
    ImGui::BeginChild("startDetailsColumn", ImVec2(0.f, -footer), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          mix(theme::palette().panelBackground, theme::palette().text, 0.04f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f * k());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f * k(), 8.f * k()));
    ImGui::BeginChild("startDetails", ImVec2(0.f, -(createHeight + 8.f * k())),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    const bool fits = drawNewDocumentFieldsCard(editor);
    ImGui::EndChild();
    ImGui::BeginDisabled(!fits);
    if (widgets::button("Create", widgets::ButtonStyle::Primary, ImVec2(-1.f, createHeight))) {
        createFromAnswers(editor, canvas, window);
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.f, 4.f * k()));
    drawTip(editor);
    ImGui::Dummy(ImVec2(0.f, 2.f * k()));

    if (ImGui::Checkbox(tr("Show this screen at start"), &editor.prefs.showStart)) {
        saveSettings(editor);
    }
    const char* leave = tr("Start with a blank canvas");
    const float leaveWidth = ImGui::CalcTextSize(leave).x + 32.f * k();
    ImGui::SameLine(ImGui::GetWindowWidth() - leaveWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button(leave, ImVec2(leaveWidth, 0.f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        editor.startOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace fast
