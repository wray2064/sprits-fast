// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/layout.h"

#include "app/file_io.h"
#include "ui/theme.h"

#include <imgui_internal.h>

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
    out->append("\n");
}

// The default arrangement: the tool's options over the palette on the left,
// the layers, their elements, the selected one's properties and the
// transform on the right, the timeline under the canvas, and the canvas in
// the middle. References float, opened from the Window menu.
void buildDefault(ImGuiID dockspace, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);

    const float side = theme::metrics().sidebarWidth;
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,
                                                     side / size.x, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right,
                                                      side / (size.x - side), nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,
                                                       170.f / size.y, nullptr, &centre);
    ImGuiID palette = 0;
    const ImGuiID tool = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.52f, nullptr, &palette);
    ImGuiID below = 0;
    const ImGuiID layers = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.24f, nullptr, &below);
    ImGuiID rest = 0;
    const ImGuiID elements = ImGui::DockBuilderSplitNode(below, ImGuiDir_Up, 0.17f, nullptr, &rest);
    ImGuiID transform = 0;
    const ImGuiID properties = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up, 0.74f, nullptr,
                                                           &transform);

    ImGui::DockBuilderDockWindow(panel::kTool, tool);
    ImGui::DockBuilderDockWindow(panel::kPalette, palette);
    ImGui::DockBuilderDockWindow(panel::kLayers, layers);
    ImGui::DockBuilderDockWindow(panel::kElements, elements);
    ImGui::DockBuilderDockWindow(panel::kProperties, properties);
    ImGui::DockBuilderDockWindow(panel::kTransform, transform);
    ImGui::DockBuilderDockWindow(panel::kTimeline, bottom);
    ImGui::DockBuilderDockWindow(panel::kCanvas, centre);
    ImGui::DockBuilderFinish(dockspace);
}

} // namespace

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
        buildDefault(dockspace, size);
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

} // namespace fast
