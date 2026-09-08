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
    if (ImGui::ColorPicker4("##colour", editor.color,
                            ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_NoSmallPreview)) {
        if (fast::PaintLayer* layer = editor.active()) {
            fast::setPaintColor(editor.doc, *layer, toColor(editor.color));
            canvas.invalidate();
            editor.status = "recoloured the layer without touching the drawing";
        }
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
        if (ImGui::Selectable(name.c_str(), editor.activeLayer == i)) {
            editor.activeLayer = i;
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
                auto info = editor.doc.engine().getDocumentInfo(editor.doc.id());
                if (info.ok() && !info.value.sprites.empty()) {
                    // Ids are minted fresh by the reader, so the interface has
                    // to ask the document what it now contains.
                    editor.sprite = info.value.sprites.front();
                    editor.layers.clear();
                    editor.activeLayer = 0;
                    editor.status = "opened; drawing on reopened layers is not wired up yet";
                }
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
            canvas.invalidate();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, editor.doc.canRedo())) {
            editor.doc.redo();
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
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift ? editor.doc.redo() : editor.doc.undo()) {
            canvas.invalidate();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && editor.doc.redo()) {
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

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);

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
