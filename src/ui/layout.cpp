// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/layout.h"

#include "app/file_io.h"
#include "app/theme_tokens.h"
#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/widgets.h"

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
void buildPreset(ImGuiID dockspace, ImVec2 size, LayoutPreset preset) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);

    const float side = theme::metrics().sidebarWidth;
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,
                                                     side / size.x, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right,
                                                      side / (size.x - side), nullptr, &centre);
    const float timelineHeight = preset == LayoutPreset::Animation ? 0.36f * size.y : 214.f;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,
                                                       timelineHeight / size.y, nullptr, &centre);
    ImGuiID palette = 0;
    const ImGuiID tool = ImGui::DockBuilderSplitNode(
        left, ImGuiDir_Up, preset == LayoutPreset::Compact ? 0.42f : 0.40f, nullptr, &palette);

    ImGui::DockBuilderDockWindow(panel::kTool, tool);
    ImGui::DockBuilderDockWindow(panel::kPalette, palette);
    ImGui::DockBuilderDockWindow(panel::kTimeline, bottom);
    ImGui::DockBuilderDockWindow(panel::kCanvas, centre);

    if (preset == LayoutPreset::Drawing) {
        ImGuiID below = 0;
        const ImGuiID layers = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.30f, nullptr, &below);
        ImGuiID rest = 0;
        const ImGuiID elements = ImGui::DockBuilderSplitNode(below, ImGuiDir_Up, 0.16f, nullptr, &rest);
        ImGuiID transform = 0;
        const ImGuiID properties = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up, 0.70f, nullptr,
                                                               &transform);
        ImGui::DockBuilderDockWindow(panel::kLayers, layers);
        ImGui::DockBuilderDockWindow(panel::kElements, elements);
        ImGui::DockBuilderDockWindow(panel::kProperties, properties);
        ImGui::DockBuilderDockWindow(panel::kTransform, transform);
        ImGui::DockBuilderDockWindow(panel::kPuppet, properties);
    } else {
        // Layers over one group of tabs: the layers always in view, and one of
        // the rest at a time in the space below.
        ImGuiID tabs = 0;
        const ImGuiID layers = ImGui::DockBuilderSplitNode(
            right, ImGuiDir_Up, preset == LayoutPreset::Animation ? 0.50f : 0.34f, nullptr, &tabs);
        ImGui::DockBuilderDockWindow(panel::kLayers, layers);
        ImGui::DockBuilderDockWindow(panel::kElements, tabs);
        ImGui::DockBuilderDockWindow(panel::kProperties, tabs);
        ImGui::DockBuilderDockWindow(panel::kTransform, tabs);
        ImGui::DockBuilderDockWindow(panel::kPuppet, tabs);
    }
    ImGui::DockBuilderFinish(dockspace);
}

// Whether a named file is one of the person's saved layouts, by its id: the
// same plain ids a theme has, because it is a file name.
bool plainId(const std::string& id) {
    if (id.empty() || id.size() > 48) {
        return false;
    }
    for (char ch : id) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
            return false;
        }
    }
    return true;
}

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
    // Which panels are open goes with the arrangement: all of them, except that
    // Compact puts the timeline away.
    editor.panels.tool = editor.panels.palette = editor.panels.layers = true;
    editor.panels.elements = editor.panels.properties = editor.panels.transform = true;
    editor.panels.puppet = true;
    editor.timeline.visible = preset != LayoutPreset::Compact;
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
        if (!plainId(id)) {
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
    if (folder.empty() || !plainId(id)) {
        return false;
    }
    const std::string path = joinPath(folder, id + ".ini");
    return fileExists(path) && deleteFile(path);
}

void useSavedLayout(Editor& editor, const std::string& folder, const std::string& id) {
    if (folder.empty() || !plainId(id)) {
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
        buildPreset(dockspace, size, static_cast<LayoutPreset>(editor.panels.preset));
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
