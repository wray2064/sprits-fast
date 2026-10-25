// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// app_window.cpp — the window: layout, input, and the file commands.
//
// The panels themselves live in panels.cpp and the look in theme.cpp, so what
// is left here is the shape of the window and what happens when keys and mouse
// buttons are pressed.
//
// This is still the only part of Fast that knows what toolkit is in use.
// Everything it does goes through fast_core, which knows nothing about windows.

#include "app/batch.h"
#include "app/guides.h"
#include "app/tracks.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/transform.h"
#include "ui/app_internal.h"
#include "ui/layout.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/shape_tools.h"
#include "ui/appearance.h"
#include "ui/help.h"
#include "ui/start_screen.h"
#include "ui/widgets.h"
#include "ui/rulers.h"
#include "ui/program.h"
#include "ui/slice_tool.h"
#include "ui/tabs.h"
#include "ui/tile_tools.h"
#include "ui/ui_script.h"
#include "ui/selection_tools.h"
#include "ui/panels.h"
#include "ui/theme.h"
#include "ui/timeline_drawer.h"
#include "ui/transform_gizmo.h"
#include "ui/puppet_panel.h"
#include "ui/reference_section.h"

#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fast_icon_256.h"
#include "fast_icon_32.h"

using namespace fast;

namespace {

// ----------------------------------------------------------------- layout --

void drawWindow(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const theme::Metrics& m = theme::metrics();

    // The status bar is set in the caption size, and the rail is as wide as a
    // tool button and its padding: both follow the font, the density and the
    // interface size rather than a number.
    theme::pushText(theme::TextStyle::Caption);
    const float statusHeight = ImGui::GetFrameHeight() + theme::px(8.f);
    theme::popText();
    const float toolbarWidth = theme::toolRailWidth();
    const float bodyHeight = viewport->WorkSize.y - statusHeight;
    const float left = viewport->WorkPos.x;
    const float top = viewport->WorkPos.y;

    constexpr ImGuiWindowFlags kPanel =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoDocking;

    // The toolbar: a narrow strip, no title, so the tools read as a group
    // rather than as the contents of a panel.
    ImGui::SetNextWindowPos({left, top});
    ImGui::SetNextWindowSize({toolbarWidth, bodyHeight});
    // Tools sit close together: they are one group, and the default spacing
    // makes four buttons read as four unrelated things.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(m.itemSpacing * 0.66f, m.itemSpacing * 0.66f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().windowBackground);
    ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar);
    drawToolbar(editor);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Everything else docks: the panels round the canvas, each one moved,
    // tabbed, floated, resized or closed as the person likes (see layout.h).
    drawDockSpace(editor, { left + toolbarWidth, top },
                  { viewport->WorkSize.x - toolbarWidth, bodyHeight });
    const auto title = [](const char* name, const char* id) {
        return std::string(tr(name)) + id;
    };
    if (editor.panels.tool) {
        if (beginPanel(title("Tool", panel::kTool).c_str(), &editor.panels.tool)) {
            drawToolPanel(editor);
        }
        ImGui::End();
    }
    if (editor.panels.palette) {
        if (beginPanel(title("Palette", panel::kPalette).c_str(), &editor.panels.palette)) {
            drawPalettePanel(editor, canvas, window);
        }
        ImGui::End();
    }
    if (editor.panels.layers) {
        if (beginPanel(title("Layers", panel::kLayers).c_str(), &editor.panels.layers)) {
            drawLayerPanel(editor, canvas);
        }
        ImGui::End();
    }
    if (editor.panels.elements) {
        if (beginPanel(title("Elements", panel::kElements).c_str(), &editor.panels.elements)) {
            drawElementsPanel(editor, canvas);
        }
        ImGui::End();
    }
    if (editor.panels.properties) {
        if (beginPanel(title("Properties", panel::kProperties).c_str(),
                       &editor.panels.properties)) {
            drawPropertiesPanel(editor, canvas);
        }
        ImGui::End();
    }
    if (editor.panels.transform) {
        if (beginPanel(title("Transform", panel::kTransform).c_str(), &editor.panels.transform)) {
            drawTransformPanel(editor, canvas);
        }
        ImGui::End();
    }
    if (editor.panels.puppet) {
        if (beginPanel(title("Puppet", panel::kPuppet).c_str(), &editor.panels.puppet)) {
            drawPuppetPanel(editor, canvas);
        }
        ImGui::End();
    }
    // References float over everything, opened when wanted: a picture to
    // draw from is looked at beside the canvas, not kept in a column.
    if (editor.panels.references) {
        ImGui::SetNextWindowPos({ left + viewport->WorkSize.x - m.sidebarWidth * 2.3f,
                                  top + bodyHeight * 0.18f }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ m.sidebarWidth * 1.2f, bodyHeight * 0.4f },
                                 ImGuiCond_FirstUseEver);
        if (beginPanel(title("References", panel::kReferences).c_str(),
                       &editor.panels.references)) {
            drawReferencePanel(editor, canvas, window);
        }
        ImGui::End();
    }
    drawReferenceSectionWindow(editor, canvas);

    // The canvas: the middle of the dock space, which nothing covers, with no
    // tab of its own and no way to drag it out.
    ImGuiWindowClass canvasClass;
    canvasClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar |
                                           ImGuiDockNodeFlags_NoUndocking;
    ImGui::SetNextWindowClass(&canvasClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().canvasBackground);
    ImGui::Begin(panel::kCanvas, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawTabBar(editor, canvas, window);
    ls::Vec2i hovered { -1, -1 };
    // While playing, the canvas shows the frame the clock says rather than the
    // frame being edited. Selection does not move with it -- stopping is what
    // changes which frame the tools act on.
    canvas.setHoverSize(editor.tool == Tool::Spray ? editor.sprayRadius * 2 + 1
                        : (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser)
                            ? (editor.pen.down ? pressuredSize(editor.brush, editor.pen.pressure)
                                               : editor.brush.size)
                            : 1);
    canvas.setPanWithPrimary(editor.tool == Tool::Hand);
    canvas.setZoomOnClick(editor.tool == Tool::Zoom);
    const int showing = frameToShow(editor, SDL_GetTicks());
    // A puppet shown in the Puppet panel's Draw part or Pose takes the canvas.
    settlePuppetView(editor, canvas);
    const ls::SpriteId onScreen =
        editor.puppet.mode == Editor::PuppetView::Mode::DrawPart
            ? editor.activeSprite()
            : (showing >= 0 && showing < static_cast<int>(editor.frames.size()))
                  ? editor.frames[static_cast<size_t>(showing)].sprite
                  : editor.sprite;
    const bool overCanvas = canvas.draw(
        editor.doc, onScreen, &hovered,
        [&editor, &canvas](ImDrawList* draw, ImVec2 origin, float zoom) {
            drawReferences(editor, canvas, draw, origin, zoom, true);
            drawOnionSkin(editor, canvas, draw, origin, zoom);
        },
        [&editor, &canvas](ImDrawList* draw, ImVec2 origin, float zoom) {
            drawReferences(editor, canvas, draw, origin, zoom, false);
            drawSelectionOverlay(editor, draw, origin, zoom);
            drawSymmetryAxes(editor, canvas, draw, origin, zoom);
            drawShapeOverlay(editor, canvas, draw, origin, zoom);
            drawFreeScaleOverlay(editor, canvas, draw, origin, zoom);
            drawTransformGizmo(editor, canvas, draw, origin, zoom);
            drawPuppetOverlay(editor, canvas, draw, origin, zoom);
            drawTilemapOverlay(editor, canvas, draw, origin, zoom);
            drawSliceOverlay(editor, canvas, draw, origin, zoom);
            drawGuides(editor, canvas, draw, origin, zoom);
            if (editor.stroking && editor.brush.stabiliser > 0) {
                const ls::Vec2f a = editor.stabiliser.at();
                const ls::Vec2f b = canvas.pointerExact();
                draw->AddLine(ImVec2(origin.x + a.x * zoom, origin.y + a.y * zoom),
                              ImVec2(origin.x + b.x * zoom, origin.y + b.y * zoom),
                              IM_COL32(255, 255, 255, 120), 1.f);
            }
            if (editor.drawingGradient) {
                const ls::Vec2f a = editor.gradientFrom;
                const ls::Vec2f b = editor.gradientTo;
                const ImVec2 from(origin.x + a.x * zoom, origin.y + a.y * zoom);
                const ImVec2 to(origin.x + b.x * zoom, origin.y + b.y * zoom);
                draw->AddLine(from, to, IM_COL32(0, 0, 0, 200), 3.f);
                draw->AddLine(from, to, IM_COL32(255, 255, 255, 230), 1.f);
                draw->AddCircleFilled(from, 4.f, IM_COL32(255, 255, 255, 230));
                draw->AddCircle(to, 4.f, IM_COL32(255, 255, 255, 230));
            }
            if (editor.drawingContour && editor.contourPoints.size() > 1) {
                const ImU32 ink = ImGui::GetColorU32(ImVec4(editor.color[0], editor.color[1],
                                                            editor.color[2], 1.f));
                const auto centre = [&](ls::Vec2i p) {
                    return ImVec2(origin.x + (static_cast<float>(p.x) + 0.5f) * zoom,
                                  origin.y + (static_cast<float>(p.y) + 0.5f) * zoom);
                };
                for (size_t i = 1; i < editor.contourPoints.size(); ++i) {
                    draw->AddLine(centre(editor.contourPoints[i - 1]),
                                  centre(editor.contourPoints[i]), ink, 2.f);
                }
                draw->AddLine(centre(editor.contourPoints.back()),
                              centre(editor.contourPoints.front()), ink, 1.f);
            }
        });
    drawRulers(editor, canvas);
    editor.hovered = hovered;
    // Where the canvas is, for a Timeline that opens over it.
    const ImVec2 canvasPos = ImGui::GetWindowPos();
    const ImVec2 canvasSize = ImGui::GetWindowSize();

    // The preview goes on top of the canvas and takes its clicks first, so a
    // stroke is not started by someone reaching for a backdrop swatch.
    drawPreviewOverlay(editor, canvas);
    // A press on the canvas makes the window's own move ID the active one for
    // the whole drag -- ImGui does that even for a window that cannot move --
    // so only an item other than the window itself being held means the
    // pointer belongs to something else. Counting the window's own ID stopped
    // every drag after its first frame: a click painted, a drag did not.
    const bool itemHeld = ImGui::IsAnyItemActive() &&
                          ImGui::GetActiveID() != ImGui::GetCurrentWindow()->MoveId;
    const bool overPreview = ImGui::IsAnyItemHovered() || itemHeld;

    // Drawing is refused while playing rather than silently landing on a frame
    // the person is not looking at.
    handleStroke(editor, canvas,
                 overCanvas && !overPreview && !editor.timeline.playing, hovered);
    // For the next frame's question: was a click there on the artwork, or
    // somewhere that should drop a float first?
    editor.canvasHovered = overCanvas && !overPreview;
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // The timeline is one of two things (ui/timeline_drawer.h). Pinned, it docks
    // under the canvas and takes its height out of it rather than covering it:
    // an animator wants to see the frame and the strip at once. Unpinned, it is
    // a drawer over the bottom of the canvas that opens when wanted and tucks
    // away, so a person drawing has the whole window.
    if (editor.timeline.visible) {
        const auto strip = [&editor, &canvas]() { drawTimelinePanel(editor, canvas); };
        if (editor.timeline.pinned) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
            if (beginPanel(title("Timeline", panel::kTimeline).c_str(), &editor.timeline.visible)) {
                drawTimelineHeader(editor, false);
                strip();
            }
            ImGui::End();
            ImGui::PopStyleVar();
        } else {
            drawTimelineDrawer(editor, canvasPos, canvasSize, strip);
        }
    }

    ImGui::SetNextWindowPos({left, top + bodyHeight});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, statusHeight});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().windowBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.panelPadding, 4.f));
    theme::pushText(theme::TextStyle::Caption);
    ImGui::Begin("##status", nullptr,
                 kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    drawStatusBar(editor, canvas);
    ImGui::End();
    theme::popText();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    drawSheetPanel(editor, window);
    drawAnimationPanel(editor, window);
    drawCanvasSizePanel(editor, canvas);
    drawSpriteSizePanel(editor, canvas);
    drawModifyPanel(editor);
    drawTilemapDialog(editor, canvas);
    drawAdjustPanel(editor, canvas);
    drawTextPanel(editor, canvas);
    drawHistoryPanel(editor, canvas);
    drawPreferencesPanel(editor, canvas, window);
    drawLayerPropertiesPanel(editor, canvas);
    drawSlicesPanel(editor, canvas);
    drawSheetImportPanel(editor, canvas, window);
    drawLibraryPanel(editor, canvas, window);
    drawRecoveryPrompt(editor, canvas);
    drawStartScreen(editor, canvas, window);
    drawHelpWindows(editor, window);
    widgets::drawGallery(&editor.kitOpen);
    drawSaveLayoutDialog(editor);
    drawUnsavedPrompt(editor, canvas, window);
}

} // namespace

namespace {

// The program's icon on its window: the title bar, the task switcher, and
// wherever a platform has no icon in the executable, the taskbar or dock.
// Each size is decoded from a PNG compiled into the program; a small one is
// offered beside the large so the title bar need not shrink 256 to 16.
void setWindowIcon(SDL_Window* window) {
    std::vector<std::vector<uint8_t>> pngs;
    pngs.emplace_back(kFastIcon256, kFastIcon256 + kFastIcon256_size);
    pngs.emplace_back(kFastIcon32, kFastIcon32 + kFastIcon32_size);
    std::vector<ls::RasterBuffer> images(pngs.size());
    SDL_Surface* icon = nullptr;
    for (size_t i = 0; i < pngs.size(); ++i) {
        ls::RasterBuffer& image = images[i];
        std::string why;
        if (!decodeImage(pngs[i], &image, &why) || image.empty()) {
            continue;
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            static_cast<int>(image.width), static_cast<int>(image.height),
            SDL_PIXELFORMAT_RGBA32, image.pixels.data(), static_cast<int>(image.stride));
        if (surface == nullptr) {
            continue;
        }
        if (icon == nullptr) {
            icon = surface;
        } else {
            SDL_AddSurfaceAlternateImage(icon, surface);
            SDL_DestroySurface(surface);   // the alternate list holds its own reference
        }
    }
    if (icon != nullptr) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}

} // namespace

int fast::runApp(int argc, char** argv) {
    // --export makes this a batch run: open, write, exit, with no window --
    // what a build script wants. See app/batch.h for the options.
    {
        const std::vector<std::string> args(argv + 1, argv + argc);
        BatchJob job;
        std::string error;
        if (parseBatch(args, &job, &error)) {
            std::string message;
            const int code = runBatch(job, &message);
            std::printf("%s\n", message.c_str());
            return code;
        }
        if (!error.empty()) {
            std::printf("%s\n", error.c_str());
            return 2;
        }
    }

    const Options options = parseOptions(argc, argv);
    if (options.selfTest) {
        return runSelfTest();
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(program::kTitle, 1440, 900,
                                          SDL_WINDOW_RESIZABLE |
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    setWindowIcon(window);
    configureProgram(std::vector<uint8_t>(kFastIcon256, kFastIcon256 + kFastIcon256_size));

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
    watchInterfaceErrors();

    // Every window is positioned explicitly each frame, so ImGui's saved layout
    // would never be read -- it would only drop an imgui.ini into whatever
    // directory the editor happened to be started from.
    ImGui::GetIO().IniFilename = nullptr;
    // Panels dock, tab and float (see layout.h). A panel is dragged out of a
    // dock by its tab, and a window's own title bar docks it again.
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    theme::loadFonts(SDL_GetWindowDisplayScale(window));
    theme::apply();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Editor editor;
    editor.files.dialog.init();
    // A run that is scripted or captured keeps to the defaults, so what it
    // captures or checks is the same on every machine, whatever keys and files the
    // person there has -- and leaves their lists as they are.
    const bool headless = options.selfTest || options.frames > 0 || !options.script.empty();
    if (headless) {
        editor.files.recent.keepInMemory();
    }
    editor.files.recent.load();
    if (!headless) {
        applyStoredLimits();
    }
    // Where the panels were left, kept between sessions -- except for a run
    // that is scripted or captured, which starts from the default so what it
    // checks does not depend on how the panels were last arranged.
    const bool interactive = options.frames == 0 && options.script.empty();
    registerPanelSettings(editor);
    if (interactive) {
        loadLayout();
    }

    CanvasView canvas(renderer);
    if (!newDocument(editor, 32)) {
        std::printf("could not create the first document\n");
        return 1;
    }
    canvas.requestFit();
    initTabs(editor);
    attachTracks(editor);
    if (!options.reference.empty()) {
        // A picture to draw from, imported as the window opens: a scripted
        // run's reference, or a sheet to start from.
        std::string error;
        if (!importReferenceToPick(editor, canvas, options.reference, &error)) {
            std::printf("--reference %s: %s\n", options.reference.c_str(), error.c_str());
        }
    }
    if (!options.openPath.empty()) {
        openPath(editor, canvas, options.openPath);
    }
    if (options.demoStroke) {
        drawDemoContent(editor);
        canvas.invalidate();
    }
    // Whatever built the document -- new, opened, or the demo -- the panel
    // and the canvas read the references from it here rather than each
    // caller remembering to.
    resyncReferences(editor, canvas);
    editor.sheetPanelOpen = options.showSheetPanel;
    if (!options.select.empty()) {
        // x,y,w,h -- four numbers and three commas, or nothing is selected.
        int values[4] = {};
        const char* at = options.select.c_str();
        int read = 0;
        for (; read < 4; ++read) {
            char* end = nullptr;
            values[read] = static_cast<int>(std::strtol(at, &end, 10));
            if (end == at) {
                break;
            }
            at = (*end == ',') ? end + 1 : end;
        }
        if (read == 4 && values[2] > 0 && values[3] > 0) {
            editor.selection.mask = rectangleMask(
                { values[0], values[1] },
                { values[0] + values[2] - 1, values[1] + values[3] - 1 });
        }
    }
    if (options.light) {
        editor.prefs.lightTheme = true;
        editor.prefs.themeId = "paper";
        theme::setLight(true);
        canvasDefaultsFor(true, &editor.prefs);
        applyViewPreferences(editor.prefs, canvas);
    }
    if (options.isometric) {
        canvas.tileGrid().visible = true;
        canvas.tileGrid().isometric = true;
        canvas.tileGrid().width = 16;
        canvas.tileGrid().height = 8;
    }
    if (options.tiled) {
        canvas.setTiledMode(TiledMode::Both);
        canvas.tileGrid().visible = true;
        canvas.tileGrid().width = 8;
        canvas.tileGrid().height = 8;
    }
    editor.symmetryAcross = editor.symmetryAcross || options.symmetry;
    editor.symmetryDown = editor.symmetryDown || options.symmetry;
    if (!options.tool.empty()) {
        toolFromName(options.tool, &editor.tool);
    }
    // The safety net, before anything can be drawn and lost. Autosave is
    // simply off when there is nowhere to write, rather than writing
    // somewhere the person would not think to look.
    if (!headless) {
        loadSettings(editor);
        editor.settingsLoaded = true;
    }
    editor.systemClipboard = !headless;
    initSystemClipboard(window);
    editor.preferencesOpen = options.preferences;
    editor.preferencesShowKeys = options.preferencesKeys;
    if (!editor.recovery.begin()) {
        editor.autosaveOn = false;
    } else {
        editor.autosaveOn = editor.prefs.autosaveOn;
        editor.recovery.setIntervalSeconds(editor.prefs.autosaveSeconds);
    }
    // The theme the person chose: a built-in, or one of their own files.
    loadAppearance(editor);
    theme::setDensity(static_cast<theme::Density>(editor.prefs.density));
    theme::setScale(static_cast<float>(editor.prefs.uiScale) / 100.f);
    applyViewPreferences(editor.prefs, canvas);
    editor.doc.setHistoryLimit(editor.prefs.historyLimit);
    if (options.autosaveSeconds > 0) {
        editor.recovery.setIntervalSeconds(options.autosaveSeconds);
    }
    // The person's own unfinished work is theirs to be asked about, in a
    // session of theirs: a scripted or captured run leaves it where it is for
    // them -- and does not stop at a dialog that depends on what they left.
    if (!headless) {
        editor.recovered = findRecoveredWork();
    }
    editor.askingToRecover = !editor.recovered.empty();

    // A person's start greets them with the start screen, unless they asked
    // for a file or turned it off; a scripted run sees it only when asked.
    if (options.start || (interactive && editor.prefs.showStart && options.openPath.empty())) {
        openStartScreen(editor);
    }
    editor.kitOpen = options.kit;

    if (options.library) {
        editor.libraryOpen = true;
        editor.libraryStale = true;
    }
    editor.libraryFolders.inMemory = headless;
    editor.libraryFolders.load();
    // Nothing chosen yet: the folder this document is in is the obvious
    // project, and costs the person no decision.
    if (editor.libraryFolders.project.empty() && !editor.doc.path().empty()) {
        editor.libraryFolders.project = directoryOf(editor.doc.path());
    }
    if (!options.language.empty()) {
        applyLanguage(options.language);
    }
    if (options.rotsprite && editor.active() != nullptr) {
        // A small figure drawn twice: the left turned by coverage, the right
        // by RotSprite, both 30 degrees.
        const char* rows[] = {
            "...XXXX...",
            "..XooooX..",
            ".XooXXooX.",
            ".XoX..XoX.",
            ".XoX..XoX.",
            ".XooXXooX.",
            "..XooooX..",
            "...XXXX...",
        };
        for (int copy = 0; copy < 2; ++copy) {
            PaintLayer made;
            editor.doc.beginAction("Figure");
            createPaintLayer(editor.doc, editor.sprite, copy == 0 ? "coverage" : "rotsprite",
                             ls::Color{ 30, 30, 40, 255 }, &made);
            const int ox = copy == 0 ? 3 : 18;
            std::vector<ls::Vec2i> dark;
            std::vector<ls::Vec2i> light;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 10; ++x) {
                    const char c = rows[y][x];
                    if (c == 'X') { dark.push_back({ ox + x, 12 + y }); }
                    if (c == 'o') { light.push_back({ ox + x, 12 + y }); }
                }
            }
            paintPixels(editor.doc, made, dark);
            Ink amber;
            amber.colour = { 240, 170, 60, 255 };
            InkStroke stroke;
            if (beginInkStroke(editor.doc, made.layer, amber, &stroke)) {
                strokeInk(editor.doc, stroke, light);
            }
            addRotate(editor.doc, made.layer, 30.f,
                      { static_cast<float>(ox) + 5.f, 16.f },
                      copy == 0 ? ls::SamplingPolicy::Coverage : ls::SamplingPolicy::RotSprite);
            editor.doc.endAction();
        }
        resyncLayers(editor);
    }
    if (options.rectangle[0] >= 0.f && editor.active() != nullptr) {
        ShapeParams params;
        params.from = { options.rectangle[0], options.rectangle[1] };
        params.to = { options.rectangle[2], options.rectangle[3] };
        ShapeLayer made;
        editor.doc.beginAction("Rectangle");
        addShapeElement(editor.doc, editor.active()->layer, ShapeKind::Rectangle, params,
                        foregroundInk(editor), &made);
        editor.doc.endAction();
        resyncLayers(editor);
    }
    if (options.tween && editor.active() != nullptr) {
        PaintLayer made;
        editor.doc.beginAction("Figure");
        createPaintLayer(editor.doc, editor.sprite, "arrow", ls::Color{ 240, 170, 60, 255 }, &made);
        std::vector<ls::Vec2i> body;
        for (int y = 12; y < 18; ++y) {
            for (int x = 3; x < 7; ++x) { body.push_back({ x, y }); }
        }
        for (int x = 7; x < 11; ++x) { body.push_back({ x, 14 }); body.push_back({ x, 15 }); }
        paintPixels(editor.doc, made, body);
        editor.doc.endAction();
        for (int i = 0; i < 4; ++i) {
            duplicateFrame(editor.doc, i);
        }
        resyncFrames(editor);
        const std::string key = trackKey(editor.doc, made.layer);
        const ls::LayerId from = layerOfTrack(editor.doc, editor.frames.front().sprite, key);
        const ls::LayerId to = layerOfTrack(editor.doc, editor.frames.back().sprite, key);
        editor.doc.beginAction("Keys");
        addRotate(editor.doc, from, 0.f, { 6.f, 15.f }, ls::SamplingPolicy::RotSprite);
        addOffset(editor.doc, from, { 0.f, 0.f });
        addRotate(editor.doc, to, 180.f, { 6.f, 15.f }, ls::SamplingPolicy::RotSprite);
        addOffset(editor.doc, to, { 18.f, 0.f });
        editor.doc.endAction();
        std::vector<ls::SpriteId> run;
        for (const Frame& f : editor.frames) { run.push_back(f.sprite); }
        editor.doc.beginAction("Tween");
        tweenTransforms(editor.doc, run, key, TweenEasing::EaseInOut, nullptr);
        editor.doc.endAction();
        resyncFrames(editor);
        editor.timeline.rangeAnchor = 0;
        selectFrame(editor, 2);
        editor.timeline.visible = true;
        editor.timeline.pinned = true;  // a capture shows the strip docked
        editor.timeline.onion = true;
        resyncLayers(editor);
    }
    if (options.slices) {
        Slice panel;
        panel.name = "panel";
        panel.bounds = { { 3, 3 }, { 15, 12 } };
        panel.nine = true;
        panel.centre = { { 3, 3 }, { 9, 6 } };
        Slice hit;
        hit.name = "hitbox";
        hit.bounds = { { 12, 14 }, { 22, 30 } };
        hit.hasPivot = true;
        hit.pivot = { 5, 16 };
        hit.colour = { 230, 90, 60, 255 };
        writeSlices(editor.doc, { panel, hit });
        writeGuides(editor.doc, { { true, 16 }, { false, 20 } });
        editor.tool = Tool::Slice;
        editor.activeSlice = 0;
        editor.slicesOpen = true;
    }
    if (options.shadow && editor.active() != nullptr) {
        ShadowSettings settings;
        settings.scope = OutlineScope::Sprite;
        settings.dx = 2;
        settings.dy = 2;
        settings.opacity = 0.6f;
        editor.doc.beginAction("Add shadow");
        setShadow(editor.doc, *editor.active(), settings);
        editor.doc.endAction();
    }
    if (options.adjust && editor.active() != nullptr) {
        editor.adjustDialog = Editor::AdjustDialog{};
        editor.adjustDialog.open = true;
        editor.adjustDialog.scope = 2;
        editor.adjustDialog.slots = true;
        editor.adjustDialog.adjust.hue = 150.f;
        editor.adjustDialog.adjust.contrast = 0.2f;
        editor.doc.beginAction("Adjust colours");
    }
    if (options.tabs) {
        // Two more, the second of them changed, and the middle one showing.
        for (uint32_t size : { 24u, 48u }) {
            openTab(editor, canvas);
            newDocument(editor, size);
            if (size == 48u) {
                editor.doc.beginAction("Pencil");
                paintPixels(editor.doc, *editor.active(), linePixels({ 4, 4 }, { 40, 30 }));
                editor.doc.endAction();
            }
        }
        switchToTab(editor, canvas, 1);
    }
    if (options.paths && editor.active() != nullptr) {
        editor.tool = Tool::Polygon;
        editor.placingPath = true;
        editor.pathPoints = { { 3.5f, 20.5f }, { 12.5f, 6.5f }, { 22.5f, 12.5f }, { 16.5f, 27.5f } };
        editor.pathHandles.assign(4, { 0.f, 0.f });
        finishPath(editor, canvas, true);
        editor.tool = Tool::Curve;
        editor.placingPath = true;
        editor.pathPoints = { { 4.5f, 4.5f }, { 16.5f, 16.5f }, { 28.5f, 4.5f } };
        editor.pathHandles = { { 6.f, 0.f }, { 6.f, 6.f }, { 0.f, 6.f } };
        editor.color[0] = 0.1f;
        editor.color[1] = 0.1f;
        editor.color[2] = 0.15f;
        finishPath(editor, canvas, false);
        editor.tool = Tool::Move;
    }
    if (options.layerProperties && editor.active() != nullptr) {
        // Tagged and annotated, so a capture shows the stripe and the notes.
        const ls::Color orange{ 232, 146, 52, 255 };
        setLayerTag(editor.doc, editor.active()->layer, &orange);
        setLayerNotes(editor.doc, editor.active()->layer, "Outline pass still to do.");
        editor.propertiesLayer = editor.active()->layer;
    }
    if (options.play) {
        editor.timeline.playing = true;
        editor.timeline.startedAtMs = SDL_GetTicks();
    }
    if (options.systemCopy || options.systemPaste) {
        editor.systemClipboard = true;
        // Paste first, so both together are a round trip: what came in goes
        // back out through the selection the paste leaves.
        if (options.systemPaste) {
            pasteCommand(editor, canvas, false);
            if (editor.floating.active()) {
                settleFloating(editor);
            }
            resyncLayers(editor);
            std::printf("clipboard: %s\n", editor.status.c_str());
        }
        if (options.systemCopy) {
            copyCommand(editor);
            std::printf("clipboard: %s\n", editor.status.c_str());
        }
    }
    if (!options.sheetPath.empty()) {
        std::string sheetError;
        SheetSettings settings;
        settings.scale = 2;
        if (exportSheetToPng(editor.doc, editor.frames, sheetSteps(editor),
                             editor.cycles, options.sheetPath, settings,
                             &sheetError)) {
            std::printf("sheet: wrote %s, %d cells\n", options.sheetPath.c_str(),
                        static_cast<int>(sheetSteps(editor).size()));
        } else {
            std::printf("sheet: %s\n", sheetError.c_str());
            return 1;
        }
    }

    UiScript script;
    if (!options.script.empty()) {
        suppressNativeDialogs(true);        // a script answers them; nobody is there to
        std::string scriptError;
        if (!script.load(options.script, &scriptError)) {
            std::printf("script: %s\n", scriptError.c_str());
            return 1;
        }
    }

    bool running = true;
    bool idleBroken = false;
    int frame = 0;
    // An editor that is not being touched has nothing new to draw, and drawing
    // it sixty times a second anyway keeps a core busy and a laptop warm for
    // nothing. So the window draws at full rate while anything is happening --
    // input, a drag, text being typed, an animation playing -- for a moment
    // after, to let the interface settle (a menu that opened, a tooltip's delay);
    // and otherwise waits for an event for up to a tenth of a second, which is
    // frequent enough for what runs on a clock (hover delays, a message
    // expiring, autosave). A run driven by a script, or by a flag that counts
    // frames, is never idle: what it checks must not depend on timing.
    constexpr uint64_t kSettleMs = 600;
    constexpr uint32_t kIdleFrameMs = 100;
    uint64_t lastActivityMs = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        bool gotEvent = false;
        {
            const ImGuiIO& io = ImGui::GetIO();
            const bool busy = !interactive || editor.timeline.playing || ImGui::IsAnyMouseDown() ||
                              io.WantTextInput || editor.quitRequested ||
                              !editor.runCommand.empty() ||
                              SDL_GetTicks() < editor.awakeUntilMs ||
                              SDL_GetTicks() - lastActivityMs < kSettleMs;
            gotEvent = busy ? SDL_PollEvent(&event) : SDL_WaitEventTimeout(&event, kIdleFrameMs);
        }
        for (; gotEvent; gotEvent = SDL_PollEvent(&event)) {
            lastActivityMs = SDL_GetTicks();
            // While a script drives the window, only the script's input
            // counts: the desktop's own mouse, keys and focus changes -- a
            // person working in another window -- would otherwise land in
            // the middle of a run, and the same script would pass or fail
            // depending on where the pointer happened to be.
            const bool fromThePerson =
                (event.type >= SDL_EVENT_KEY_DOWN && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) ||
                event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                event.type == SDL_EVENT_WINDOW_MOUSE_ENTER ||
                event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE;
            if (script.active() && fromThePerson) {
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                // The close button asks the same question the menu does rather
                // than throwing the work away.
                requestAction(editor, canvas, window, PendingAction::Quit);
            }
            // Another program put something on the clipboard: the next paste
            // looks there first. (Windows reads the clipboard's own counter.)
            if (event.type == SDL_EVENT_CLIPBOARD_UPDATE) {
                noteClipboardEvent(event.clipboard.owner);
            }
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                requestAction(editor, canvas, window, PendingAction::OpenPath,
                              event.drop.data);
            }

            // A stylus. SDL sends mouse events for it too, which is what
            // draws; these carry what the mouse path cannot -- pressure, and
            // which end is touching. The eraser end erases for as long as it
            // is down, then hands the tool back.
            if (event.type == SDL_EVENT_PEN_PROXIMITY_IN) {
                editor.pen.seen = true;
            }
            if (event.type == SDL_EVENT_PEN_AXIS &&
                event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
                editor.pen.seen = true;
                editor.pen.pressure = event.paxis.value;
            }
            if (event.type == SDL_EVENT_PEN_DOWN || event.type == SDL_EVENT_PEN_UP) {
                editor.pen.seen = true;
                editor.pen.down = event.ptouch.down;
                editor.pen.eraser = event.ptouch.eraser;
                if (event.ptouch.down && event.ptouch.eraser && !editor.eraserTipHeld &&
                    (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser)) {
                    editor.toolBeforeEraserTip = editor.tool;
                    editor.tool = Tool::Eraser;
                    editor.eraserTipHeld = true;
                } else if (!event.ptouch.down && editor.eraserTipHeld) {
                    editor.tool = editor.toolBeforeEraserTip;
                    editor.eraserTipHeld = false;
                }
            }
        }

        processDialogResult(editor, canvas, window);
        if (editor.quitRequested) {
            running = false;
        }

        // The title carries the file name and whether there is unsaved work.
        // Only set when it changes: this runs every frame.
        const std::string title = windowTitle(editor.doc);
        if (title != editor.lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            editor.lastTitle = title;
        }

        // Autosave. Once a frame is far more often than it writes: the
        // session decides, and it refuses while a drag is open so a copy is
        // never taken of a half-committed stroke.
        if (editor.autosaveOn) {
            const uint64_t seconds = SDL_GetTicks() / 1000ull;
            if (editor.recovery.tick(editor.doc, seconds, editor.busy())) {
                editor.say("Recovery copy written");
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        script.feed(editor, canvas);
        if (options.drag[0] >= 0.f) {
            // Settle, press, move in steps, release -- after the backend has
            // had its say, so these are the last word on the pointer.
            constexpr int kPress = 5;
            constexpr int kSteps = 12;
            ImGuiIO& io = ImGui::GetIO();
            // The middle of a canvas pixel, where the view last drew it.
            const auto at = [&](float t) {
                const ImVec2 origin = canvas.artworkOrigin();
                const float x = options.drag[0] + (options.drag[2] - options.drag[0]) * t;
                const float y = options.drag[1] + (options.drag[3] - options.drag[1]) * t;
                return ImVec2(origin.x + (x + 0.5f) * canvas.zoom(),
                              origin.y + (y + 0.5f) * canvas.zoom());
            };
            if (frame == kPress - 1) {
                io.AddMousePosEvent(at(0.f).x, at(0.f).y);
            } else if (frame == kPress) {
                io.AddMousePosEvent(at(0.f).x, at(0.f).y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            } else if (frame > kPress && frame <= kPress + kSteps) {
                const ImVec2 p = at(static_cast<float>(frame - kPress) / static_cast<float>(kSteps));
                io.AddMousePosEvent(p.x, p.y);
            } else if (frame == kPress + kSteps + 1) {
                io.AddMousePosEvent(at(1.f).x, at(1.f).y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
        }
        // A saved layout chosen last frame goes in now, between frames.
        applyPendingLayout(editor);
        ImGui::NewFrame();
        canvas.frames().beginFrame();
        // Frames come and go through several doors -- the strip's delete, an
        // undo of a duplicate, a redo of a delete -- and each would have to
        // remember to drop the texture. Checked here instead: more textures
        // than frames means some belong to sprites that no longer exist.
        if (canvas.frames().heldTextures() > editor.frames.size()) {
            auto info = editor.doc.engine().getDocumentInfo(editor.doc.id());
            if (info.ok()) {
                canvas.frames().retainOnly(info.value.sprites);
            }
        }

        // A click anywhere off the artwork -- a panel, a menu, the strip --
        // drops a float before whatever was clicked acts, so no other action
        // ever lands inside the move's history entry.
        if (editor.floating.active() && !editor.draggingFloat && !editor.canvasHovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            settleFloating(editor);
        }
        drawMenuBar(editor, canvas, window);
        handleShortcuts(editor, canvas, window);
        {
            const TileGrid& tiles = canvas.tileGrid();
            editor.snapGrid = { tiles.width, tiles.height, tiles.offsetX, tiles.offsetY, {}, {} };
            if (editor.guidesShown) {
                for (const Guide& guide : readGuides(editor.doc)) {
                    (guide.vertical ? editor.snapGrid.linesX : editor.snapGrid.linesY)
                        .push_back(guide.at);
                }
            }
        }
        drawWindow(editor, canvas, window);

        ImGui::Render();
        if (interactive) {
            saveLayoutWhenChanged();
        }
        const ImVec4 bg = theme::palette().windowBackground;
        SDL_SetRenderDrawColor(renderer,
                               static_cast<Uint8>(bg.x * 255),
                               static_cast<Uint8>(bg.y * 255),
                               static_cast<Uint8>(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // Taken before the present, while the frame is still readable back off
        // the render target.
        if (!options.screenshot.empty() && options.frames > 0 &&
            frame == options.frames - 1) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                if (!saveShot(shot, options.screenshot)) {
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

        if (const std::string asked = script.takeShot(); !asked.empty()) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                if (!saveShot(shot, asked)) {
                    std::printf("could not write %s: %s\n", asked.c_str(), SDL_GetError());
                }
                SDL_DestroySurface(shot);
            }
        }

        SDL_RenderPresent(renderer);
        if (script.finished() && !options.stay) {
            idleBroken = idleBroken || script.failures() > 0;
            running = false;
        }

        // The fit happens on the first frame; a zoom asked for comes after it.
        if (frame == 0 && options.zoom > 0.f) {
            canvas.setZoom(options.zoom);
        }
        if (options.frames > 0 && ++frame >= options.frames) {
            // What the last settled frame cost. Nothing changed for the whole
            // run after the demo was built, so this must be zero -- if it is
            // not, something is compiling a picture it already has, and this
            // is where CI notices.
            std::printf("frames: %zu textures held, %d compile(s) in the last "
                        "frame, %.2f ms for the last one\n",
                        canvas.frames().heldTextures(),
                        canvas.frames().compilesThisFrame(),
                        canvas.frames().lastCompileMs());
            // Whether the safety net is actually armed. A person cannot see
            // autosave working, and neither can CI, so it says so.
            std::printf("autosave: %s, every %us, copy %s\n",
                        editor.autosaveOn ? (editor.recovery.active() ? "on" : "no folder")
                                          : "off",
                        editor.recovery.intervalSeconds(),
                        editor.recovery.haveCopy() ? "written" : "not yet due");
            if (options.expectDrawn >= 0) {
                // What the drag left: every pixel of the frame with any alpha.
                ls::RasterBuffer picture;
                int drawn = 0;
                if (compileForExport(editor.doc, editor.sprite, 1, &picture, nullptr)) {
                    for (size_t i = 3; i < picture.pixels.size(); i += 4) {
                        drawn += picture.pixels[i] != 0 ? 1 : 0;
                    }
                }
                std::printf("drawn: %d pixel(s)\n", drawn);
                if (options.expectAtMost >= 0 && drawn > options.expectAtMost) {
                    std::printf("FAIL expected at most %d drawn pixels -- the eraser should "
                                "have taken some\n", options.expectAtMost);
                    idleBroken = true;
                }
                if (drawn < options.expectDrawn) {
                    std::printf("FAIL expected at least %d drawn pixels -- a drag across "
                                "the canvas should paint all the way\n", options.expectDrawn);
                    idleBroken = true;
                }
            }
            if (options.expectIdle && canvas.frames().compilesThisFrame() != 0) {
                std::printf("FAIL a settled editor compiled %d time(s); every "
                            "frame on screen should already have a texture\n",
                            canvas.frames().compilesThisFrame());
                idleBroken = true;
            }
            running = false;
        }
    }

    // A script that ended the program (File > Quit) still says what it checked.
    if (script.active()) {
        script.finishEarly();
        idleBroken = idleBroken || script.failures() > 0;
    }

    // Closing normally is the proof that nothing was lost, so the copies go --
    // every tab's. Anything that stops the program without reaching this line
    // -- a crash, a kill, the power -- leaves them, which is the signal wanted.
    clearAllRecovery(editor);

    editor.files.dialog.destroy();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return idleBroken ? 1 : 0;
}
