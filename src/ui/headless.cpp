// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// headless.cpp — running the window with nobody there: the command line, a figure to draw
// on, and a screenshot written out. See app_internal.h.

#include "ui/app_internal.h"

#include "app/batch.h"
#include "app/import_aseprite.h"
#include "app/palette_tools.h"
#include "app/pixel_font.h"
#include "app/guides.h"
#include "app/slices.h"
#include "app/tracks.h"
#include "app/import_image.h"
#include "app/animation.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/grid_snap.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/shape.h"
#include "app/sheet.h"
#include "app/transform.h"
#include "app/ui_state.h"
#include "ui/app_extension.h"
#include "ui/keys.h"
#include "ui/layout.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/shape_tools.h"
#include "ui/appearance.h"
#include "ui/help.h"
#include "ui/new_document.h"
#include "ui/start_screen.h"
#include "ui/widgets.h"
#include "ui/rulers.h"
#include "ui/slice_tool.h"
#include "ui/tabs.h"
#include "ui/tile_tools.h"
#include "ui/ui_script.h"
#include "ui/selection_tools.h"
#include "ui/editor.h"
#include "ui/panels.h"
#include "ui/theme.h"
#include "ui/timeline_drawer.h"
#include "ui/user_guide.h"
#include "ui/transform_gizmo.h"
#include "ui/puppet_panel.h"
#include "ui/reference_section.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include "fast_icon_256.h"
#include "fast_icon_32.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>


namespace fast {

// A frame written as a PNG when the name says so, and as a BMP otherwise.
bool saveShot(SDL_Surface* surface, const std::string& path) {
    return hasExtension(path, ".png") ? SDL_SavePNG(surface, path.c_str())
                                      : SDL_SaveBMP(surface, path.c_str());
}

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
        } else if (arg == "--sheet" && i + 1 < argc) {
            // Writes a sheet of the demo and exits with the rest of the run.
            // The interesting path is compiling every frame and composing them,
            // and it is worth CI walking it rather than only the unit tests.
            options.sheetPath = argv[++i];
        } else if (arg == "--autosave" && i + 1 < argc) {
            options.autosaveSeconds = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--library") {
            options.library = true;
        } else if (arg == "--preferences") {
            options.preferences = true;
        } else if (arg == "--zoom" && i + 1 < argc) {
            options.zoom = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--language" && i + 1 < argc) {
            options.language = argv[++i];
        } else if (arg == "--rotsprite") {
            options.rotsprite = true;
        } else if (arg == "--slices") {
            options.slices = true;
        } else if (arg == "--shadow") {
            options.shadow = true;
        } else if (arg == "--tween") {
            options.tween = true;
        } else if (arg == "--script" && i + 1 < argc) {
            options.script = argv[++i];
        } else if (arg == "--stay") {
            options.stay = true;
        } else if (arg == "--expect-drawn" && i + 1 < argc) {
            options.expectDrawn = std::atoi(argv[++i]);
        } else if (arg == "--expect-at-most" && i + 1 < argc) {
            options.expectAtMost = std::atoi(argv[++i]);
        } else if (arg == "--reference" && i + 1 < argc) {
            options.reference = argv[++i];
        } else if (arg == "--rectangle" && i + 1 < argc) {
            const char* text = argv[++i];
            for (float& value : options.rectangle) {
                char* end = nullptr;
                value = std::strtof(text, &end);
                text = (*end == ',') ? end + 1 : end;
            }
        } else if (arg == "--drag" && i + 1 < argc) {
            const char* text = argv[++i];
            for (float& value : options.drag) {
                char* end = nullptr;
                value = std::strtof(text, &end);
                text = (*end == ',') ? end + 1 : end;
            }
        } else if (arg == "--adjust") {
            options.adjust = true;
        } else if (arg == "--tabs") {
            options.tabs = true;
        } else if (arg == "--start") {
            options.start = true;
        } else if (arg == "--kit") {
            options.kit = true;
        } else if (arg == "--paths") {
            options.paths = true;
        } else if (arg == "--layer-properties") {
            options.layerProperties = true;
        } else if (arg == "--system-copy") {
            options.systemCopy = true;
        } else if (arg == "--system-paste") {
            options.systemPaste = true;
        } else if (arg == "--preferences-keys") {
            options.preferences = true;
            options.preferencesKeys = true;
        } else if (arg == "--play") {
            options.play = true;
        } else if (arg == "--show-sheet-panel") {
            // Opens the export window so a headless capture can show it. Only
            // useful with --frames and --shot.
            options.showSheetPanel = true;
        } else if (arg == "--select" && i + 1 < argc) {
            // A rectangle selected at start, so a headless capture can show
            // the marching ants and the tool options that go with them.
            options.select = argv[++i];
        } else if (arg == "--tool" && i + 1 < argc) {
            options.tool = argv[++i];
        } else if (arg == "--light") {
            options.light = true;
        } else if (arg == "--isometric") {
            options.isometric = true;
        } else if (arg == "--tiled") {
            options.tiled = true;
        } else if (arg == "--symmetry") {
            options.symmetry = true;
        } else if (arg == "--expect-idle") {
            // Fails the run if the last frame compiled anything. Nothing is
            // changing by then, so a compile means something asked for a
            // picture it already had -- which is the whole performance design
            // quietly coming undone, and is invisible in a screenshot.
            options.expectIdle = true;
        } else if (arg == "--self-test") {
            options.selfTest = true;
        } else if (!arg.empty() && arg[0] != '-') {
            // A bare argument is a file to open, which is how a file manager
            // hands one over when someone double-clicks it.
            options.openPath = arg;
        }
    }
    return options;
}

void drawDemoContent(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    editor.doc.beginAction("Demo");
    paintPixels(editor.doc, *layer, linePixels({4, 4}, {27, 27}));
    paintPixels(editor.doc, *layer, linePixels({27, 4}, {4, 27}));
    for (int32_t y = 12; y < 20; ++y) {
        paintPixels(editor.doc, *layer, linePixels({12, y}, {19, y}));
    }
    editor.doc.endAction();

    // Dithered and turned, because a flat square proves nothing about an engine
    // whose point is that neither of those costs anything.
    DitherSettings dither;
    dither.pattern = ls::DitherPatternKind::Bayer4;
    dither.from = ls::Color{40, 50, 110, 255};
    dither.to = ls::Color{240, 170, 90, 255};
    dither.modulation = ls::DitherModulation::Linear;
    dither.gradientStart = {6.f, 6.f};
    dither.gradientEnd = {26.f, 26.f};
    dither.anchor = ls::PatternAnchor::Local;
    // The two ends follow palette slots rather than holding literal colours,
    // so the dither recolours with the palette like everything else. That is
    // the promise the palette makes, and until ramps could name a role it was
    // false for exactly the kind of layer pixel art is mostly made of.
    ensurePalette(editor.doc, editor.sprite);
    dither.fromRole = addPaletteEntry(editor.doc, editor.sprite, dither.from);
    dither.toRole = addPaletteEntry(editor.doc, editor.sprite, dither.to);
    setPaletteLabel(editor.doc, dither.fromRole, "shade");
    setPaletteLabel(editor.doc, dither.toRole, "light");
    setLayerDithered(editor.doc, *layer, dither);
    editor.dither = dither;

    // Two shapes on the figure's own layer, so the Shape panel has an element
    // list to show and the belt is one layer, not three.
    {
        ShapeParams belt;
        belt.from = { 10.f, 22.f };
        belt.to = { 22.f, 24.f };
        ShapeLayer made;
        addShapeElement(editor.doc, layer->layer, ShapeKind::Rectangle, belt, &made);
        ShapeParams strap;
        strap.from = { 12.f, 20.f };
        strap.to = { 20.f, 26.f };
        addShapeElement(editor.doc, layer->layer, ShapeKind::Line, strap, &made);
    }

    editor.doc.beginAction("Demo rotate");
    addRotate(editor.doc, layer->layer, 24.f, {16.f, 16.f});
    editor.doc.endAction();
    // The demo shows the preview over a colour rather than the chequer, since
    // checking a sprite against a background it will really be seen on is the
    // reason the panel exists.
    editor.preview.transparent = false;
    editor.preview.color[0] = 0.36f;
    editor.preview.color[1] = 0.55f;
    editor.preview.color[2] = 0.78f;
    editor.preview.scale = 2;

    // A second layer, and one line round both of them.
    //
    // This is the outline worth showing, because it is the one a conventional
    // editor cannot draw at all: the line belongs to the figure rather than to
    // a layer, so there is no seam where the two parts meet, and it follows
    // whichever part moves.
    // A copy, not the pointer: pushing onto editor.layers below can reallocate
    // the vector `layer` points into, and everything after that would be
    // writing through a dangling pointer.
    const PaintLayer first = *layer;
    layer = nullptr;

    PaintLayer second;
    if (createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                         ls::Color{120, 200, 255, 255}, &second)) {
        editor.doc.beginAction("Demo second part");
        paintPixels(editor.doc, second, linePixels({18, 10}, {26, 10}));
        paintPixels(editor.doc, second, linePixels({18, 11}, {26, 11}));
        paintPixels(editor.doc, second, linePixels({18, 12}, {26, 12}));
        // And a second colour on the same layer, through a palette slot: a
        // layer holds as many colours as it is painted with.
        Ink light;
        light.colour = dither.to;
        light.role = dither.toRole;
        InkStroke stroke;
        if (beginInkStroke(editor.doc, second.layer, light, &stroke)) {
            strokeInk(editor.doc, stroke, linePixels({19, 11}, {25, 11}));
        }
        editor.doc.endAction();
        editor.layers.push_back(second);

        // On the first layer rather than the second, to make the point: the
        // line belongs to the figure, not to the layer it happens to live on.
        OutlineSettings figure;
        figure.scope = OutlineScope::Sprite;
        figure.colour = ls::Color{12, 14, 20, 255};
        editor.doc.beginAction("Demo outline");
        setOutline(editor.doc, first, figure);
        editor.doc.endAction();
    }

    // Six frames of the same drawing at different angles.
    //
    // This is the demo worth having, because in any editor that resamples it
    // would be six degraded copies -- each frame stamped from the last, softer
    // than the one before. Here every frame owns the same authored pixels and
    // one rotation parameter, so frame six is exactly as sharp as frame one and
    // the file is not six times larger.
    resyncFrames(editor);
    for (int i = 1; i < 6; ++i) {
        const int at = duplicateFrame(editor.doc, i - 1);
        if (at < 0) {
            break;
        }
        resyncFrames(editor);
        selectFrame(editor, at);
        PaintLayer* frameLayer = editor.active();
        if (frameLayer == nullptr) {
            continue;
        }
        const std::vector<TransformEntry> stack =
            listTransforms(editor.doc, frameLayer->layer);
        if (!stack.empty()) {
            editor.doc.beginAction("Demo angle");
            setRotateAngle(editor.doc, stack.front().id,
                           24.f + static_cast<float>(i) * 12.f);
            editor.doc.endAction();
        }
        setFrameDuration(editor.doc, at, 80 + i * 10);
    }
    resyncFrames(editor);

    // A cycle that is not simply "every frame", because that is the case the
    // step row exists for: it leaves a frame out and plays another twice, which
    // no row of checkboxes over the strip could say.
    const int frameCount = static_cast<int>(editor.frames.size());
    if (addCycle(editor.doc, "swing", frameCount) == 0) {
        resyncFrames(editor);
        // every frame -> 0 1 2 3 4 2
        removeCycleStep(editor.doc, 0, 5, frameCount);
        resyncFrames(editor);
        addCycleStep(editor.doc, 0, 4, 2, frameCount);
        resyncFrames(editor);
        setCycleLoop(editor.doc, 0, LoopMode::PingPong, frameCount);
        resyncFrames(editor);
        selectCycle(editor, 0);
    }

    // The stack has something to show: the highlight at Screen, and a group
    // of the two layers so the panel has a fold to draw.
    {
        const std::vector<ls::LayerId> order = layerOrder(editor.doc, editor.sprite);
        if (order.size() >= 2) {
            setLayerBlend(editor.doc, order.back(), ls::BlendMode::Screen);
            setLayerOpacity(editor.doc, order.back(), 0.8f);
            groupLayers(editor.doc, { order[0], order[1] }, "figure");
        }
    }

    // A reference to draw from. Generated rather than read from disk so the
    // demo needs no files beside it, but it goes in through exactly the call
    // an imported photograph does.
    {
        ls::RasterBuffer picture = ls::makeRaster(24, 24);
        for (uint32_t y = 0; y < picture.height; ++y) {
            for (uint32_t x = 0; x < picture.width; ++x) {
                uint8_t* pixel = picture.row(y) + static_cast<size_t>(x) * 4u;
                const bool ring = (x + y) % 7 < 2;
                pixel[0] = static_cast<uint8_t>(90 + x * 6);
                pixel[1] = static_cast<uint8_t>(70 + y * 5);
                pixel[2] = 160;
                pixel[3] = ring ? 200 : 60;
            }
        }
        std::vector<uint8_t> png;
        std::string error;
        Reference made;
        if (encodeImageAsPng(picture, &png, &error) &&
            addReference(editor.doc, "study.png", png, &made, &error)) {
            made.opacity = 0.4f;
            made.behind = true;
            updateReference(editor.doc, made);
            editor.activeReference = made.id;
        }
    }

    // A second palette, the same as the first with the dither's two slots
    // turned to night, and a flash frame bound to a third. The quick row has
    // something to swap between, and the strip shows one frame sitting it out.
    {
        const ls::PaletteId day = documentPalette(editor.doc);
        renamePalette(editor.doc, day, "day");
        const ls::PaletteId night = addPalette(editor.doc, "night", day);
        if (night.valid()) {
            setPaletteEntry(editor.doc, night, dither.fromRole, ls::Color{20, 22, 48, 255});
            setPaletteEntry(editor.doc, night, dither.toRole, ls::Color{120, 130, 210, 255});
        }
        const ls::PaletteId flash = addPalette(editor.doc, "flash", day);
        const std::vector<Frame> frames = readFrames(editor.doc);
        if (flash.valid() && frames.size() > 3) {
            setPaletteEntry(editor.doc, flash, dither.fromRole, ls::Color{255, 255, 255, 255});
            setPaletteEntry(editor.doc, flash, dither.toRole, ls::Color{255, 255, 255, 255});
            bindFrame(editor.doc, frames[3].sprite, flash);
        }
    }

    selectFrame(editor, 0);
    editor.timeline.visible = true;
    editor.timeline.pinned = true;      // a capture shows the strip docked
    editor.timeline.onion = true;

    editor.say("Demo content");
}

} // namespace fast
