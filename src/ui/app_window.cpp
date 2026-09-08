// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// app_window.cpp — the window, the panels, and the input loop.
//
// This is the only file that knows what toolkit Fast uses. Everything it does
// goes through fast_core, which knows nothing about windows; if this file were
// deleted and rewritten against a different toolkit, the editor would still be
// here.

#include "app/paint.h"
#include "ui/canvas_view.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace ls;

enum class Tool { Pencil, Eraser };

// Everything the interface is holding on to. Deliberately small: the document
// is the state, and this is only what is needed to talk about it.
struct Editor {
    fast::Document                doc;
    SpriteId                      sprite;
    std::vector<fast::PaintLayer> layers;
    int                           activeLayer = 0;

    Tool  tool = Tool::Pencil;
    float color[4] = { 0.85f, 0.35f, 0.25f, 1.f };

    bool  stroking = false;
    bool  recolouring = false;
    int   renaming = -1;            // index of the layer being renamed, or -1
    char  renameBuffer[64] = {};
    Vec2i lastPixel { -1, -1 };
    std::string status = "ready";

    fast::PaintLayer* active() {
        if (activeLayer < 0 || activeLayer >= static_cast<int>(layers.size())) {
            return nullptr;
        }
        return &layers[static_cast<size_t>(activeLayer)];
    }
};

Color toColor(const float rgba[4]) {
    return { static_cast<uint8_t>(rgba[0] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[1] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[2] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[3] * 255.f + 0.5f) };
}

// Rebuilds the layer list from the document.
//
// The interface holds handles; undo, redo and open all change what exists. Undo
// restores ids exactly, so re-adopting after one gives back the same handles --
// but an undone "Add layer" leaves the panel holding a layer that is no longer
// there, and drawing on it would fail silently. Asking the document what it now
// contains is cheaper than tracking that by hand and cannot drift.
void resyncLayers(Editor& editor) {
    std::vector<fast::PaintLayer> found;
    SpriteId sprite;
    if (!fast::adoptPaintLayers(editor.doc, &sprite, &found)) {
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

bool newDocument(Editor& editor, uint32_t size) {
    if (!editor.doc.create("untitled", size, size)) {
        return false;
    }
    editor.layers.clear();
    editor.activeLayer = 0;
    editor.sprite = editor.doc.engine().createSprite(editor.doc.id()).value;

    fast::PaintLayer layer;
    if (!fast::createPaintLayer(editor.doc, editor.sprite, "layer 1",
                                toColor(editor.color), &layer)) {
        return false;
    }
    editor.layers.push_back(layer);
    editor.status = "new " + std::to_string(size) + "x" + std::to_string(size) + " document";
    return true;
}

// ------------------------------------------------------------------ panels --

void drawToolPanel(Editor& editor, fast::CanvasView& canvas) {
    ImGui::TextUnformatted("Tool");
    if (ImGui::RadioButton("Pencil", editor.tool == Tool::Pencil)) { editor.tool = Tool::Pencil; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Eraser", editor.tool == Tool::Eraser)) { editor.tool = Tool::Eraser; }

    ImGui::Separator();
    ImGui::TextUnformatted("Colour");

    // Changing the colour recolours what is already drawn, because the colour
    // lives on the fill rule rather than in the pixels. That is worth seeing
    // happen: it is the whole premise of the engine in one control.
    const bool changed = ImGui::ColorPicker4("##colour", editor.color,
                                            ImGuiColorEditFlags_NoSidePreview |
                                            ImGuiColorEditFlags_NoSmallPreview);

    // One history entry for a whole drag of the picker, not one per frame: the
    // bracket opens when the control is grabbed and closes when it is let go.
    // Without this a recolour was not undoable at all, and did not even mark the
    // document as modified.
    if (ImGui::IsItemActivated()) {
        editor.doc.beginAction("Recolour");
        editor.recolouring = true;
    }
    if (changed) {
        if (fast::PaintLayer* layer = editor.active()) {
            fast::setPaintColor(editor.doc, *layer, toColor(editor.color));
            canvas.invalidate();
            editor.status = "recoloured the layer without touching the drawing";
        }
    }
    if (editor.recolouring && ImGui::IsItemDeactivated()) {
        editor.doc.endAction();
        editor.recolouring = false;
    }

    ImGui::Separator();
    float zoom = canvas.zoom();
    if (ImGui::SliderFloat("Zoom", &zoom, 1.f, 32.f, "%.0fx")) {
        canvas.setZoom(zoom);
    }
    if (ImGui::Button("Reset view")) {
        canvas.resetView();
    }
}

void drawLayerPanel(Editor& editor, fast::CanvasView& canvas) {
    if (ImGui::Button("Add layer")) {
        fast::PaintLayer layer;
        const std::string name = "layer " + std::to_string(editor.layers.size() + 1);
        if (fast::createPaintLayer(editor.doc, editor.sprite, name,
                                   toColor(editor.color), &layer)) {
            editor.layers.push_back(layer);
            editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
            canvas.invalidate();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && editor.layers.size() > 1) {
        if (fast::PaintLayer* layer = editor.active()) {
            editor.doc.beginAction("Delete layer");
            editor.doc.engine().deleteLayer(layer->layer);
            editor.doc.endAction();
            resyncLayers(editor);
            canvas.invalidate();
        }
    }
    ImGui::Separator();

    // Topmost first, which is how a layer stack reads.
    for (int i = static_cast<int>(editor.layers.size()) - 1; i >= 0; --i) {
        ImGui::PushID(i);
        auto info = editor.doc.engine().getLayerInfo(editor.layers[static_cast<size_t>(i)].layer);
        const std::string name = info.ok() ? info.value.name : "?";

        bool visible = info.ok() ? info.value.visible : true;
        if (ImGui::Checkbox("##visible", &visible)) {
            editor.doc.beginAction("Toggle layer");
            editor.doc.engine().setLayerVisibility(
                editor.layers[static_cast<size_t>(i)].layer, visible);
            editor.doc.endAction();
            canvas.invalidate();
        }
        ImGui::SameLine();

        // Double-click to rename, which is what a layer name in a list means
        // everywhere else.
        if (editor.renaming == i) {
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputText("##rename", editor.renameBuffer,
                                 sizeof(editor.renameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                editor.doc.beginAction("Rename layer");
                editor.doc.engine().setLayerName(
                    editor.layers[static_cast<size_t>(i)].layer, editor.renameBuffer);
                editor.doc.endAction();
                editor.renaming = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editor.renaming = -1;
            }
        } else {
            if (ImGui::Selectable(name.c_str(), editor.activeLayer == i)) {
                editor.activeLayer = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renaming = i;
                std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                              "%s", name.c_str());
            }
        }
        ImGui::PopID();
    }
}

void drawMenuBar(Editor& editor, fast::CanvasView& canvas, bool& running) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New 16x16"))  { newDocument(editor, 16); canvas.invalidate(); }
        if (ImGui::MenuItem("New 32x32"))  { newDocument(editor, 32); canvas.invalidate(); }
        if (ImGui::MenuItem("New 64x64"))  { newDocument(editor, 64); canvas.invalidate(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::string error;
            const std::string path = editor.doc.path().empty()
                                   ? std::string("untitled") + fast::kFileExtension
                                   : editor.doc.path();
            editor.status = editor.doc.save(path, &error) ? "saved " + path
                                                          : "save failed: " + error;
        }
        if (ImGui::MenuItem("Open untitled.lsprite")) {
            std::string error;
            if (editor.doc.open(std::string("untitled") + fast::kFileExtension, &error)) {
                // Ids are minted fresh by the reader, so the interface asks the
                // document what it now contains rather than reusing handles.
                editor.activeLayer = 0;
                resyncLayers(editor);
                editor.status = "opened " + editor.doc.path() + ", " +
                                std::to_string(editor.layers.size()) + " layer(s)";
                canvas.invalidate();
            } else {
                editor.status = "open failed: " + error;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) { running = false; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + editor.doc.undoLabel();
        const std::string redo = "Redo " + editor.doc.redoLabel();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, editor.doc.canUndo())) {
            editor.doc.undo();
            resyncLayers(editor);
            canvas.invalidate();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, editor.doc.canRedo())) {
            editor.doc.redo();
            resyncLayers(editor);
            canvas.invalidate();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// ------------------------------------------------------------------- input --

void handleStroke(Editor& editor, fast::CanvasView& canvas, bool overCanvas, Vec2i pixel) {
    fast::PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }

    // A whole drag is one history entry, so the bracket opens on press and
    // closes on release rather than per sample.
    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor.doc.beginAction(editor.tool == Tool::Pencil ? "Pencil" : "Eraser");
        editor.stroking = true;
        editor.lastPixel = pixel;
    }

    if (editor.stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Interpolate: the mouse reports once a frame, not once a pixel.
        const std::vector<Vec2i> run =
            editor.lastPixel.x < 0 ? std::vector<Vec2i>{pixel}
                                   : fast::linePixels(editor.lastPixel, pixel);
        if (overCanvas) {
            if (editor.tool == Tool::Pencil) {
                fast::paintPixels(editor.doc, *layer, run);
            } else {
                fast::erasePixels(editor.doc, *layer, run);
            }
            editor.lastPixel = pixel;
            canvas.invalidate();
        }
    }

    if (editor.stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        editor.doc.endAction();
        editor.stroking = false;
        editor.lastPixel = { -1, -1 };
    }
}

void handleShortcuts(Editor& editor, fast::CanvasView& canvas) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) {
        return;
    }
    // Undoing halfway through a drag would step back over a history entry that
    // has not been committed yet, leaving the bracket open.
    if (editor.stroking || editor.recolouring) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift ? editor.doc.redo() : editor.doc.undo()) {
            resyncLayers(editor);
            canvas.invalidate();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && editor.doc.redo()) {
        resyncLayers(editor);
        canvas.invalidate();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string error;
        const std::string path = editor.doc.path().empty()
                               ? std::string("untitled") + fast::kFileExtension
                               : editor.doc.path();
        editor.status = editor.doc.save(path, &error) ? "saved " + path
                                                      : "save failed: " + error;
    }
}

} // namespace

// Running the interface without a person in front of it.
//
// A GUI is the part of a program that usually gets tested by someone looking at
// it. These flags make it testable instead: --frames runs a fixed number of
// frames and exits, --shot writes what was drawn to a file, and --demo-stroke
// puts something on the canvas through the same calls the pencil uses. With
// SDL's dummy video driver this runs on a machine with no display at all, which
// is what lets CI notice a UI that no longer starts.
struct Options {
    int         frames = 0;          // 0 = run until the user quits
    std::string screenshot;
    bool        demoStroke = false;
    bool        selfTest = false;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--shot" && i + 1 < argc) {
            options.screenshot = argv[++i];
        } else if (arg == "--demo-stroke") {
            options.demoStroke = true;
        } else if (arg == "--self-test") {
            options.selfTest = true;
        }
    }
    return options;
}

// An X and a filled block, drawn through exactly the calls the pencil makes, so
// a screenshot shows the real path rather than a special one.
void drawDemoContent(Editor& editor) {
    fast::PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    editor.doc.beginAction("Demo");
    fast::paintPixels(editor.doc, *layer, fast::linePixels({4, 4}, {27, 27}));
    fast::paintPixels(editor.doc, *layer, fast::linePixels({27, 4}, {4, 27}));
    for (int32_t y = 12; y < 20; ++y) {
        fast::paintPixels(editor.doc, *layer, fast::linePixels({12, y}, {19, y}));
    }
    editor.doc.endAction();
    editor.status = "demo content";
}

// The state management the interface does, driven without the interface.
//
// resyncLayers and the undo/open glue around it are the parts that were wrong:
// the panel held handles to layers that undo had removed, and an opened file
// could not be drawn on. None of that is reachable from a fast_core test,
// because it is the window's own bookkeeping -- so it is driven directly here
// rather than left to be found by clicking.
int runSelfTest() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* what) {
        if (!condition) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    };

    Editor editor;
    check(newDocument(editor, 16), "new document");
    check(editor.layers.size() == 1, "one layer to start");

    // Draw, as a stroke would.
    editor.doc.beginAction("Pencil");
    fast::paintPixels(editor.doc, *editor.active(), fast::linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // Add a layer, then undo it. The panel must not be left holding a layer that
    // no longer exists -- this was the bug.
    fast::PaintLayer added;
    check(fast::createPaintLayer(editor.doc, editor.sprite, "layer 2",
                                 Color{40, 80, 220, 255}, &added), "add layer");
    editor.layers.push_back(added);
    check(editor.layers.size() == 2, "two layers");

    check(editor.doc.undo(), "undo the added layer");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "panel drops the undone layer");
    check(editor.activeLayer == 0, "active index stays in range");
    check(editor.active() != nullptr, "still something to draw on");

    // Redo brings it back, and the panel picks it up again.
    check(editor.doc.canRedo(), "the undone layer can be redone");
    check(editor.doc.redo(), "redo");
    resyncLayers(editor);
    check(editor.layers.size() == 2, "the redone layer reappears in the panel");

    check(editor.doc.undo(), "undo it again");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "and goes away again");

    // Drawing after an undo discards the redo branch, which is what every editor
    // does: the history is a line, not a tree. The surviving layer must still be
    // drawable rather than a stale handle.
    editor.doc.beginAction("Pencil");
    check(fast::paintPixels(editor.doc, *editor.active(), {{7, 7}}), "draw after undo");
    editor.doc.endAction();
    check(!editor.doc.canRedo(), "a new action drops the redo branch");

    // Save, reopen, keep drawing: the loop that makes it an editor.
    std::string error;
    const std::string path = "ui_selftest.lsprite";
    check(editor.doc.save(path, &error), "save");
    check(editor.doc.open(path, &error), "open");
    resyncLayers(editor);
    check(!editor.layers.empty(), "reopened file has drawable layers");
    check(editor.active() != nullptr, "reopened file can be drawn on");

    editor.doc.beginAction("Pencil");
    check(fast::paintPixels(editor.doc, *editor.active(), {{11, 11}}), "draw after reopen");
    editor.doc.endAction();
    check(editor.doc.canUndo(), "the new stroke is undoable");

    // Opening repeatedly must not accumulate documents.
    check(editor.doc.open(path, &error), "open again");
    check(editor.doc.engine().documents().size() == 1, "one document held");

    if (failures == 0) {
        std::printf("ui_selftest: all checks passed\n");
        return 0;
    }
    std::printf("ui_selftest: %d check(s) failed\n", failures);
    return 1;
}

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.selfTest) {
        return runSelfTest();
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Sprit's'fast", 1280, 800,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Every window here is positioned explicitly each frame, so ImGui's saved
    // layout would never be read -- it would only drop an imgui.ini into
    // whatever directory the editor happened to be started from.
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Editor editor;
    fast::CanvasView canvas(renderer);
    if (!newDocument(editor, 32)) {
        std::printf("could not create the first document\n");
        return 1;
    }

    if (options.demoStroke) {
        drawDemoContent(editor);
        canvas.invalidate();
    }

    bool running = true;
    int frame = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        drawMenuBar(editor, canvas, running);
        handleShortcuts(editor, canvas);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float menuHeight = ImGui::GetFrameHeight();
        const float sidebar = 260.f;
        const float statusHeight = ImGui::GetFrameHeight() + 8.f;

        ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y});
        ImGui::SetNextWindowSize({sidebar, viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Tools", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
        drawToolPanel(editor, canvas);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - sidebar,
                                 viewport->WorkPos.y});
        ImGui::SetNextWindowSize({sidebar, viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Layers", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
        drawLayerPanel(editor, canvas);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x + sidebar, viewport->WorkPos.y});
        ImGui::SetNextWindowSize({viewport->WorkSize.x - sidebar * 2.f,
                                  viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Canvas", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
        Vec2i hovered { -1, -1 };
        const bool overCanvas = canvas.draw(editor.doc, editor.sprite, &hovered);
        handleStroke(editor, canvas, overCanvas, hovered);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x,
                                 viewport->WorkPos.y + viewport->WorkSize.y - statusHeight});
        ImGui::SetNextWindowSize({viewport->WorkSize.x, statusHeight});
        ImGui::Begin("Status", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        ImGui::Text("%s%s  |  %d,%d  |  compile %.2f ms  |  %.0f fps",
                    editor.doc.modified() ? "*" : "",
                    editor.status.c_str(),
                    hovered.x, hovered.y,
                    canvas.lastCompileMs(),
                    static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::End();
        (void)menuHeight;

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // Taken before the present, while the frame is still readable back off
        // the render target.
        if (!options.screenshot.empty() && options.frames > 0 &&
            frame == options.frames - 1) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                if (!SDL_SaveBMP(shot, options.screenshot.c_str())) {
                    std::printf("could not write %s: %s\n",
                                options.screenshot.c_str(), SDL_GetError());
                } else {
                    std::printf("wrote %s\n", options.screenshot.c_str());
                }
                SDL_DestroySurface(shot);
            } else {
                std::printf("could not read the frame back: %s\n", SDL_GetError());
            }
        }

        SDL_RenderPresent(renderer);

        if (options.frames > 0 && ++frame >= options.frames) {
            running = false;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
