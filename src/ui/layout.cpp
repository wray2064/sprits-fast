// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/layout.h"

#include "app/file_io.h"
#include "app/theme_tokens.h"
#include "app/i18n.h"
#include "app/text_util.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace fast {

namespace {

Editor* gEditor = nullptr;

std::string layoutPath() {
    const std::string directory = preferencesDirectory();
    return directory.empty() ? std::string() : directory + "/layout.ini";
}

// The open flags, by name, for the layout file.
struct Flag {
    const char* name;
    bool Editor::Panels::*field;
};
constexpr Flag kFlags[] = {
    { "tool", &Editor::Panels::tool },
    { "palette", &Editor::Panels::palette },
    { "layers", &Editor::Panels::layers },
    { "elements", &Editor::Panels::elements },
    { "properties", &Editor::Panels::properties },
    { "transform", &Editor::Panels::transform },
    { "puppet", &Editor::Panels::puppet },
    { "references", &Editor::Panels::references },
};

void* readOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*) {
    return gEditor;
}

void readLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    Editor* editor = static_cast<Editor*>(entry);
    if (editor == nullptr) {
        return;
    }
    const char* equals = std::strchr(line, '=');
    if (equals == nullptr) {
        return;
    }
    const std::string name(line, equals);
    const bool on = equals[1] == '1';
    for (const Flag& flag : kFlags) {
        if (name == flag.name) {
            editor->panels.*flag.field = on;
        }
    }
    if (name == "timeline") {
        editor->timeline.visible = on;
        // A layout written before the drawer existed had the Timeline docked when
        // it was shown, so that is what it was: pinned, unless it says otherwise
        // (timelinePinned follows in a layout written since).
        editor->timeline.pinned = on;
    }
    if (name == "timelinePinned") {
        editor->timeline.pinned = on;
    }
}

void writeAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    if (gEditor == nullptr) {
        return;
    }
    out->appendf("[%s][Open]\n", handler->TypeName);
    for (const Flag& flag : kFlags) {
        out->appendf("%s=%d\n", flag.name, gEditor->panels.*flag.field ? 1 : 0);
    }
    out->appendf("timeline=%d\n", gEditor->timeline.visible ? 1 : 0);
    out->appendf("timelinePinned=%d\n", gEditor->timeline.pinned ? 1 : 0);
    out->append("\n");
}

// The arrangements the program knows how to build.
//
// Drawing: the tool's options over the palette on the left; the layers, their
// elements, the selected one's properties and the transform on the right; the
// timeline under the canvas; the canvas in the middle. References float,
// opened from the Window menu.
//
// Animation: the canvas and the timeline get the room. The layers sit alone on
// the right over one tabbed group of everything else, and the timeline is
// taller.
//
// Compact: for a small screen. Every panel but the tool and the palette is a
// tab of one group on the right, and the timeline is put away until it is
// asked for.
//
// A stage (app_extension.h) adds its own panels to the node of the region they name; a
// stage with no base preset is built by buildStageDock instead.
using RegionNodes = ImGuiID[5];   // by DockRegion

void dockStagePanels(const Stage& stage, const RegionNodes nodes) {
    for (const StagePanel& entry : stage.panels) {
        const ImGuiID node = nodes[static_cast<int>(entry.region)];
        if (node != 0 && !entry.id.empty()) {
            ImGui::DockBuilderDockWindow(entry.id.c_str(), node);
        }
    }
}

// The dock of a stage that is exactly the panels it lists: a column each side (split
// in two when it has lower panels), a strip under the canvas, the canvas in the middle.
void buildStageDock(ImGuiID dockspace, ImVec2 size, const Stage& stage) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);
    bool has[5] = {};
    for (const StagePanel& entry : stage.panels) {
        has[static_cast<int>(entry.region)] = true;
    }
    const float k = theme::scale();
    const float sideAt100 = theme::metrics().sidebarWidth / k;
    const float side = std::min(theme::metrics().sidebarWidth * stage.sideWidth,
                                std::max(sideAt100 * stage.sideWidth, size.x * 0.30f));
    ImGuiID centre = dockspace;
    ImGuiID left = 0;
    ImGuiID right = 0;
    const bool anyLeft = has[0] || has[1];
    const bool anyRight = has[2] || has[3];
    if (anyLeft) {
        left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, side / size.x, nullptr, &centre);
    }
    if (anyRight) {
        const float rest = anyLeft ? size.x - side : size.x;
        right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, side / rest, nullptr, &centre);
    }
    ImGuiID bottom = 0;
    if (has[4]) {
        bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, nullptr, &centre);
    }
    RegionNodes nodes = {};
    const auto column = [&](ImGuiID whole, bool lower, ImGuiID* upperOut, ImGuiID* lowerOut) {
        if (whole == 0) {
            return;
        }
        if (lower) {
            ImGuiID under = 0;
            *upperOut = ImGui::DockBuilderSplitNode(whole, ImGuiDir_Up, 0.55f, nullptr, &under);
            *lowerOut = under;
        } else {
            *upperOut = whole;
        }
    };
    column(left, has[1], &nodes[0], &nodes[1]);
    column(right, has[3], &nodes[2], &nodes[3]);
    nodes[4] = bottom;
    ImGui::DockBuilderDockWindow(panel::kCanvas, centre);
    dockStagePanels(stage, nodes);
    ImGui::DockBuilderFinish(dockspace);
}

void buildPreset(ImGuiID dockspace, ImVec2 size, LayoutPreset preset, const Stage* stage = nullptr) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);
    RegionNodes nodes = {};

    // Every measure here is for 100% and grows with the interface size, so a
    // panel is as roomy at 150% as it is at 100% -- up to what the window can
    // spare: a dock takes at most 30% of the width (and never less than it has at
    // 100%), the Timeline at most half the height, so the canvas keeps the rest.
    const float k = theme::scale();
    const float sideAt100 = theme::metrics().sidebarWidth / k;
    const float widen = stage != nullptr ? stage->sideWidth : 1.f;
    const float side = std::min(theme::metrics().sidebarWidth * widen,
                                std::max(sideAt100 * widen, size.x * 0.30f));
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,
                                                     side / size.x, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right,
                                                      side / (size.x - side), nullptr, &centre);
    // The height the Timeline has when it is pinned: its rows and the frame
    // strip under them, with the header row that holds Unpin.
    // A larger interface leaves the strip narrower in its own pixels, so its
    // controls wrap onto more rows, and it needs more than the interface size
    // alone gives it.
    const float strip = 246.f * k * (1.f + 0.6f * std::max(0.f, k - 1.f));
    const float wanted = preset != LayoutPreset::Animation ? strip
                         : k > 1.f                          ? std::max(0.36f * size.y, strip)
                                                            : 0.36f * size.y;
    const float timelineHeight = std::min(wanted, std::max(246.f, 0.5f * size.y));
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,
                                                       timelineHeight / size.y, nullptr, &centre);
    // The height a window has, in logical pixels, falls as the interface grows
    // (900 pixels are 600 of them at 150%), while a panel's rows do not get any
    // shorter: so the panels that need the room -- the Tool's options, the
    // Layers list -- take a larger share of a column as the size goes up, from
    // what they have at 100% to what is given here for 150%.
    const float t = std::clamp((k - 1.f) / 0.5f, 0.f, 1.f);
    const auto grow = [t](float at100, float at150) { return at100 + (at150 - at100) * t; };

    ImGuiID palette = 0;
    const ImGuiID tool = ImGui::DockBuilderSplitNode(
        left, ImGuiDir_Up,
        preset == LayoutPreset::Compact ? grow(0.42f, 0.48f) : grow(0.40f, 0.46f), nullptr,
        &palette);

    ImGui::DockBuilderDockWindow(panel::kTool, tool);
    ImGui::DockBuilderDockWindow(panel::kPalette, palette);
    ImGui::DockBuilderDockWindow(panel::kTimeline, bottom);
    ImGui::DockBuilderDockWindow(panel::kCanvas, centre);

    if (preset == LayoutPreset::Drawing) {
        ImGuiID below = 0;
        const ImGuiID layers = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, grow(0.30f, 0.38f),
                                                           nullptr, &below);
        ImGuiID rest = 0;
        const ImGuiID elements = ImGui::DockBuilderSplitNode(below, ImGuiDir_Up,
                                                             grow(0.16f, 0.19f), nullptr, &rest);
        ImGuiID transform = 0;
        const ImGuiID properties = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up,
                                                               grow(0.70f, 0.60f), nullptr,
                                                               &transform);
        ImGui::DockBuilderDockWindow(panel::kLayers, layers);
        ImGui::DockBuilderDockWindow(panel::kElements, elements);
        ImGui::DockBuilderDockWindow(panel::kProperties, properties);
        ImGui::DockBuilderDockWindow(panel::kTransform, transform);
        ImGui::DockBuilderDockWindow(panel::kPuppet, properties);
        nodes[2] = properties;
        nodes[3] = transform;
    } else {
        // Layers over one group of tabs: the layers always in view, and one of
        // the rest at a time in the space below.
        ImGuiID tabs = 0;
        const ImGuiID layers = ImGui::DockBuilderSplitNode(
            right, ImGuiDir_Up, preset == LayoutPreset::Animation ? 0.50f : grow(0.34f, 0.42f),
            nullptr, &tabs);
        ImGui::DockBuilderDockWindow(panel::kLayers, layers);
        ImGui::DockBuilderDockWindow(panel::kElements, tabs);
        ImGui::DockBuilderDockWindow(panel::kProperties, tabs);
        ImGui::DockBuilderDockWindow(panel::kTransform, tabs);
        ImGui::DockBuilderDockWindow(panel::kPuppet, tabs);
        nodes[2] = nodes[3] = tabs;
    }
    nodes[0] = tool;
    nodes[1] = palette;
    nodes[4] = bottom;
    if (stage != nullptr) {
        dockStagePanels(*stage, nodes);
    }
    ImGui::DockBuilderFinish(dockspace);
}

// Whether a named file is one of the person's saved layouts, by its id: the
// same plain ids a theme has, because it is a file name.
constexpr size_t kMaxSavedLayouts = 32;
constexpr size_t kMaxLayoutBytes = 64 * 1024;

} // namespace

const char* presetName(LayoutPreset preset) {
    switch (preset) {
        case LayoutPreset::Drawing: return "Drawing";
        case LayoutPreset::Animation: return "Animation";
        case LayoutPreset::Compact: return "Compact";
    }
    return "";
}

void useLayoutPreset(Editor& editor, LayoutPreset preset) {
    editor.panels.preset = static_cast<int>(preset);
    editor.panels.resetLayout = true;
    // Which panels are open goes with the arrangement: all of them. The Timeline
    // is pinned in Animation, where the strip is what the work is about, and a
    // drawer in the others, where the canvas is.
    editor.panels.tool = editor.panels.palette = editor.panels.layers = true;
    editor.panels.elements = editor.panels.properties = editor.panels.transform = true;
    editor.panels.puppet = true;
    editor.timeline.visible = true;
    editor.timeline.pinned = preset == LayoutPreset::Animation;
    editor.timeline.drawerOpen = false;
    editor.timeline.drawerHot = false;
    ImGui::MarkIniSettingsDirty();
}

void useStage(Editor& editor, int index) {
    if (index < 0 || index >= static_cast<int>(editor.stages.size())) {
        return;
    }
    const size_t count = editor.stages.size();
    editor.stageLayouts.resize(count);
    const bool first = editor.stage < 0;
    if (!first && editor.stage == index) {
        return;
    }
    if (!first && editor.stage < static_cast<int>(count)) {
        // What the person made of this stage is kept for when they come back.
        size_t size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&size);
        editor.stageLayouts[static_cast<size_t>(editor.stage)].assign(ini, size);
    }
    editor.stage = index;
    const std::string& kept = editor.stageLayouts[static_cast<size_t>(index)];
    if (!kept.empty()) {
        editor.pendingLayout = kept;
        return;
    }
    const Stage& stage = editor.stages[static_cast<size_t>(index)];
    if (stage.base >= 0) {
        useLayoutPreset(editor, static_cast<LayoutPreset>(std::min(stage.base, 2)));
        return;
    }
    // Exactly the panels it lists: the Fast ones it does not name are closed.
    const auto listed = [&stage](const char* id) {
        for (const StagePanel& entry : stage.panels) {
            if (entry.id == id) {
                return true;
            }
        }
        return false;
    };
    editor.panels.tool = listed(panel::kTool);
    editor.panels.palette = listed(panel::kPalette);
    editor.panels.layers = listed(panel::kLayers);
    editor.panels.elements = listed(panel::kElements);
    editor.panels.properties = listed(panel::kProperties);
    editor.panels.transform = listed(panel::kTransform);
    editor.panels.puppet = listed(panel::kPuppet);
    editor.panels.references = listed(panel::kReferences);
    editor.timeline.visible = listed(panel::kTimeline);
    editor.timeline.pinned = editor.timeline.visible;
    editor.timeline.drawerOpen = false;
    editor.panels.resetLayout = true;
    ImGui::MarkIniSettingsDirty();
}

std::string layoutsFolderFor(const Editor& editor) {
    if (!editor.settingsLoaded) {
        return "ui_selftest_layouts";
    }
    const std::string directory = preferencesDirectory();
    return directory.empty() ? std::string() : directory + "/layouts";
}

std::vector<SavedLayout> savedLayouts(const std::string& folder) {
    std::vector<SavedLayout> layouts;
    if (folder.empty()) {
        return layouts;
    }
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (layouts.size() >= kMaxSavedLayouts) {
            break;
        }
        if (entry.directory || !hasExtension(entry.name, ".ini") || entry.bytes > kMaxLayoutBytes) {
            continue;
        }
        const std::string id = fileStem(entry.name);
        if (!isPlainId(id)) {
            continue;
        }
        // The name is the first line, as a comment ImGui skips: "; My layout".
        std::vector<uint8_t> bytes;
        std::string error;
        std::string name = id;
        if (readFile(entry.path, bytes, &error) && bytes.size() <= kMaxLayoutBytes &&
            bytes.size() > 2 && bytes[0] == ';') {
            std::string first;
            for (size_t i = 1; i < bytes.size() && bytes[i] != '\n' && first.size() < 48; ++i) {
                if (bytes[i] != '\r') {
                    first += static_cast<char>(bytes[i]);
                }
            }
            while (!first.empty() && first.front() == ' ') {
                first.erase(first.begin());
            }
            if (!first.empty()) {
                name = first;
            }
        }
        layouts.push_back({ id, name });
    }
    return layouts;
}

bool saveLayoutAs(const std::string& folder, const std::string& name, std::string* savedId,
                  std::string* error) {
    if (folder.empty()) {
        if (error != nullptr) {
            *error = "there is no settings folder to keep it in";
        }
        return false;
    }
    std::string cleaned;
    for (char ch : name) {
        if (static_cast<unsigned char>(ch) >= 0x20 && ch != '\n' && ch != '\r') {
            cleaned += ch;
        }
    }
    if (cleaned.size() > 48) {
        cleaned.resize(48);
    }
    if (cleaned.empty()) {
        cleaned = "My layout";
    }
    const std::string id = themeIdFromName(cleaned);
    if (!createDirectory(folder)) {
        if (error != nullptr) {
            *error = "could not make the layouts folder";
        }
        return false;
    }
    size_t size = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&size);
    std::string text = "; " + cleaned + "\n";
    text.append(ini, size);
    if (text.size() > kMaxLayoutBytes) {
        if (error != nullptr) {
            *error = "the layout is too large to keep";
        }
        return false;
    }
    if (!writeFileAtomic(joinPath(folder, id + ".ini"),
                         std::vector<uint8_t>(text.begin(), text.end()), error)) {
        return false;
    }
    if (savedId != nullptr) {
        *savedId = id;
    }
    return true;
}

bool deleteSavedLayout(const std::string& folder, const std::string& id) {
    if (folder.empty() || !isPlainId(id)) {
        return false;
    }
    const std::string path = joinPath(folder, id + ".ini");
    return fileExists(path) && deleteFile(path);
}

void useSavedLayout(Editor& editor, const std::string& folder, const std::string& id) {
    if (folder.empty() || !isPlainId(id)) {
        return;
    }
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(joinPath(folder, id + ".ini"), bytes, &error) || bytes.size() > kMaxLayoutBytes) {
        return;
    }
    editor.pendingLayout.assign(bytes.begin(), bytes.end());
}

void applyPendingLayout(Editor& editor) {
    if (editor.pendingLayout.empty()) {
        return;
    }
    // Before the frame begins: ImGui reads a layout between frames, not in one.
    ImGui::LoadIniSettingsFromMemory(editor.pendingLayout.c_str(), editor.pendingLayout.size());
    editor.pendingLayout.clear();
    ImGui::MarkIniSettingsDirty();
}

void drawSaveLayoutDialog(Editor& editor) {
    if (!widgets::beginDialog("Save layout", &editor.saveLayoutOpen, widgets::DialogSize::Small)) {
        return;
    }
    ImGui::TextWrapped("%s", tr("Keeps where every panel is, and which are open, under a name "
                                "you can come back to from Window > Layouts."));
    ImGui::Dummy(ImVec2(0.f, 6.f));
    widgets::field("name");
    ImGui::InputTextWithHint("##layoutname", tr("a name for it"), editor.layoutName,
                             sizeof(editor.layoutName));
    if (!editor.layoutMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
        ImGui::TextWrapped("%s", editor.layoutMessage.c_str());
        ImGui::PopStyleColor();
    }
    const int choice = widgets::dialogButtons("Save", "Cancel");
    if (choice == 1) {
        std::string error;
        if (saveLayoutAs(layoutsFolderFor(editor), editor.layoutName, nullptr, &error)) {
            editor.saveLayoutOpen = false;
            editor.say(std::string("Saved the layout as ") +
                       (editor.layoutName[0] != '\0' ? editor.layoutName : "My layout"));
            ImGui::CloseCurrentPopup();
        } else {
            editor.layoutMessage = "Could not save it: " + error;
        }
    } else if (choice == 2) {
        editor.saveLayoutOpen = false;
        ImGui::CloseCurrentPopup();
    }
    widgets::endDialog();
}

void registerPanelSettings(Editor& editor) {
    gEditor = &editor;
    ImGuiSettingsHandler handler;
    handler.TypeName = "FastPanels";
    handler.TypeHash = ImHashStr("FastPanels");
    handler.ReadOpenFn = readOpen;
    handler.ReadLineFn = readLine;
    handler.WriteAllFn = writeAll;
    ImGui::AddSettingsHandler(&handler);
}

void loadLayout() {
    const std::string path = layoutPath();
    if (path.empty()) {
        return;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const std::string ini = text.str();
    ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
}

void saveLayoutWhenChanged() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantSaveIniSettings) {
        return;
    }
    io.WantSaveIniSettings = false;
    const std::string path = layoutPath();
    if (path.empty()) {
        return;
    }
    size_t size = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&size);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(ini, static_cast<std::streamsize>(size));
    }
}

void drawDockSpace(Editor& editor, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##dockhost", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    const ImGuiID dockspace = ImGui::GetID("##fastdock");
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
    if (editor.panels.resetLayout || node == nullptr) {
        editor.panels.resetLayout = false;
        const bool staged = editor.stage >= 0 && editor.stage < static_cast<int>(editor.stages.size());
        const Stage* stage = staged ? &editor.stages[static_cast<size_t>(editor.stage)] : nullptr;
        if (stage != nullptr && stage->base < 0) {
            buildStageDock(dockspace, size, *stage);
        } else {
            buildPreset(dockspace, size, static_cast<LayoutPreset>(editor.panels.preset), stage);
        }
        editor.panels.builtAtScale = theme::scale();
        ImGui::MarkIniSettingsDirty();
    }
    // Panels dock round the canvas, never over it.
    // One close button a panel, on its tab: none for the whole dock beside
    // it, and no menu arrow. (Flags given to the dock space pass to every
    // node in it.)
    ImGui::DockSpace(dockspace, ImVec2(0.f, 0.f),
                     ImGuiDockNodeFlags_NoDockingOverCentralNode |
                         ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
    ImGui::End();
}

ImGuiWindowFlags panelFlags() {
    return ImGuiWindowFlags_NoCollapse;
}

bool beginPanel(const char* title, bool* open) {
    // The scrollbar is asked for only by a panel that did overflow last frame. A
    // window that may always scroll sideways settles into showing its bar for
    // good once a vertical bar has come and gone beside it -- the bar's height
    // pushes the content into a vertical bar, whose width pushes the content
    // wider than the window, and ImGui forces the bar on to stop the flicker --
    // so a panel with nothing too wide for it would carry a bar with nothing to
    // scroll. Overflow is measured whether the bar is allowed or not.
    const ImGuiWindow* before = ImGui::FindWindowByName(title);
    const bool overflowed = before != nullptr && before->ScrollMax.x > 0.f;
    const bool shown = ImGui::Begin(
        title, open, panelFlags() | (overflowed ? ImGuiWindowFlags_HorizontalScrollbar : 0));
    if (shown) {
        // Begin gives the work rect the width of last frame's widest content when
        // the window may scroll sideways (imgui.cpp, work_rect_size_x); put it back
        // to the visible width. Content wider than that still counts, and still
        // scrolls: it is the cursor's furthest reach that sizes the scroll.
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float visible = window->Size.x - window->WindowPadding.x * 2.f -
                              (window->DecoOuterSizeX1 + window->DecoOuterSizeX2);
        window->WorkRect.Max.x = std::min(window->WorkRect.Max.x,
                                          window->WorkRect.Min.x + visible);
    }
    return shown;
}

} // namespace fast
