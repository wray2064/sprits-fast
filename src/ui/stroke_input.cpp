// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// stroke_input.cpp -- what the mouse does on the canvas: strokes, fills, picking, dragging a reference.
// See app_internal.h for how the window's files divide.

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

// ------------------------------------------------------------------ input --

// Alt held turns a drag on the canvas into moving the selected reference
// rather than drawing. Alt because it is the one modifier no tool uses, and
// on the canvas because a reference is placed by eye against the artwork --
// the number fields in the panel are for when it has to be exact.
//
// Returns true when it took the drag, so the tools stand aside.
static bool handleReferenceDrag(Editor& editor, CanvasView& canvas, bool overCanvas,
                         ls::Vec2i pixel) {
    const bool alt = ImGui::GetIO().KeyAlt;
    if (!editor.draggingReference && (!alt || !overCanvas)) {
        return false;
    }
    Reference* active = activeReference(editor);
    if (active == nullptr || active->locked || !active->visible) {
        if (alt && overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            editor.say(active == nullptr ? "No reference to move"
                                         : "That reference is locked");
        }
        return false;
    }

    const ls::Vec2f here { static_cast<float>(pixel.x), static_cast<float>(pixel.y) };
    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor.draggingReference = true;
        editor.referenceGrabbed = { here.x - active->x, here.y - active->y };
        editor.doc.beginAction("Move reference");
        return true;
    }
    if (editor.draggingReference && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        active->x = here.x - editor.referenceGrabbed.x;
        active->y = here.y - editor.referenceGrabbed.y;
        updateReference(editor.doc, *active);
        canvas.invalidate();
        return true;
    }
    if (editor.draggingReference && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        editor.draggingReference = false;
        editor.doc.endAction();
        editor.say("Reference moved");
        return true;
    }
    return editor.draggingReference;
}

// What a pencil or a bucket paints into. Normally the ink -- the left
// button's, or the right one's -- which finds or makes that colour's element
// on the layer. But a dithered element picked in the element list is painted
// into as itself, so pixels drawn there take the dither rather than a flat
// colour: the one way to paint with a rule rather than a value.
bool beginPaint(Editor& editor, const PaintLayer& layer, bool back, InkStroke* out) {
    if (!back && editor.paintIntoElement && editor.activeElement.valid()) {
        for (const Element& element : elementsOf(editor.doc, layer.layer)) {
            if (element.fill != editor.activeElement || element.kind != ElementKind::Paint) {
                continue;
            }
            Ink solid;
            if (!inkOfElement(editor.doc, element.fill, &solid)) {
                PaintLayer target;
                target.layer = layer.layer;
                target.fill = element.fill;
                target.region = element.region;
                return beginElementStroke(editor.doc, target, out);
            }
        }
    }
    return beginInkStroke(editor.doc, layer.layer,
                          back ? backgroundInk(editor) : foregroundInk(editor), out);
}

// The palette the active frame draws with, in the order its swatches show --
// the ramp Shading steps along.
static std::vector<std::pair<ls::ColorRole, ls::Color>> paletteRamp(Editor& editor) {
    std::vector<std::pair<ls::ColorRole, ls::Color>> ramp;
    for (const PaletteEntry& entry :
         paletteEntries(editor.doc, paletteFor(editor.doc, editor.activeSprite()))) {
        ramp.push_back({ entry.role, entry.color });
    }
    return ramp;
}

// The last step of a mark that is a choice of pixels rather than a path --
// a lasso, a stamp: wrapped for tiled mode, mirrored for symmetry, kept
// inside the selection, and laid down through the ink mode as an area.
static void layDown(Editor& editor, CanvasView& canvas, std::vector<ls::Vec2i> run, bool tiled) {
    if (tiled) {
        for (ls::Vec2i& at : run) {
            at = canvas.wrap(at);
        }
    }
    run = mirrored(run, symmetryNow(editor));
    if (!editor.selection.empty()) {
        run.erase(std::remove_if(run.begin(), run.end(), [&](ls::Vec2i at) {
                      return !editor.selection.contains(at);
                  }),
                  run.end());
    }
    if (run.empty()) {
        return;
    }
    if (editor.tileTarget.layer.valid()) {
        strokeTiles(editor, run);
        canvas.invalidate();
        return;
    }
    if (editor.inkStroke.erasing()) {
        strokeInk(editor.doc, editor.inkStroke, run);
    } else {
        strokeInkMode(editor.doc, editor.inkModeState, editor.inkStroke, run);
    }
    canvas.invalidate();
}

// The last step of a pencil's or an eraser's path. `centres` are the pixels
// the brush is stamped on, in order; they are laid down as a path -- a mark
// that is drawn again wherever the layer is turned -- continuing the one the
// drag began. Wrapped for tiled mode and mirrored for symmetry, each mirror
// image a path of its own. Paint is kept to the selection and to what an ink
// mode allows by its run's clip (see keepStrokeToMode); an eraser's path is
// split where the selection cuts it. What goes into tiles is laid down as an
// area.
static void layDownPath(Editor& editor, CanvasView& canvas, std::vector<ls::Vec2i> centres,
                 const PenBrush& brush, bool tiled) {
    if (centres.empty()) {
        return;
    }
    if (tiled) {
        for (ls::Vec2i& at : centres) {
            at = canvas.wrap(at);
        }
    }
    // The stamps, for what is laid down as pixels.
    const auto stampsOf = [&brush](const std::vector<ls::Vec2i>& path) {
        std::vector<ls::Vec2i> out;
        for (const ls::Interval& run : markPixels(pathMark(path, brush)).intervals) {
            for (int32_t x = run.x0; x < run.x1; ++x) {
                out.push_back({ x, run.y });
            }
        }
        return out;
    };
    const bool erasing = editor.inkStroke.erasing() ||
                         (editor.tileTarget.layer.valid() && editor.tileErasing);
    const bool tiles = editor.tileTarget.layer.valid();
    const bool asPixels = erasing && !editor.selection.empty() && brush.size > 1;
    if (asPixels) {
        layDown(editor, canvas, stampsOf(centres), false);
        return;
    }
    if (!erasing && !tiles && editor.inkModeState.nothing) {
        return;                         // the mode allows nowhere
    }
    // Each mirror image, its centre placed so its stamp is the mirror of the
    // stamp: an even brush hangs right and down, so its mirror hangs from one
    // pixel further over.
    const Symmetry symmetry = symmetryNow(editor);
    const int before = (std::max(1, brush.size) - 1) / 2;
    const int shift = std::max(1, brush.size) - 2 * before;     // 1 odd, 2 even
    std::vector<std::vector<ls::Vec2i>> images { centres };
    if (symmetry.active()) {
        std::vector<ls::Vec2i> across, down, both;
        for (ls::Vec2i p : centres) {
            const ls::Vec2i flippedX { symmetry.axisX - shift - p.x, p.y };
            const ls::Vec2i flippedY { p.x, symmetry.axisY - shift - p.y };
            across.push_back(flippedX);
            down.push_back(flippedY);
            both.push_back({ flippedX.x, flippedY.y });
        }
        if (symmetry.across) { images.push_back(across); }
        if (symmetry.down)   { images.push_back(down); }
        if (symmetry.across && symmetry.down) { images.push_back(both); }
    }
    for (size_t image = 0; image < images.size(); ++image) {
        // Split where the path jumps -- a wrap in tiled mode -- or leaves the
        // selection: each part a path of its own.
        std::vector<ls::Vec2i> part;
        const auto flush = [&]() {
            if (part.empty()) {
                return;
            }
            if (tiles) {
                strokeTilesAlong(editor, part, brush, static_cast<int>(image));
            } else if (!erasing && editor.inkModeState.mode == InkMode::Shading) {
                InkModeState::Piece piece;
                piece.copy = static_cast<int>(image);
                piece.points = part;
                piece.brush = brush;
                shadeMark(editor.doc, editor.inkModeState, piece);
            } else {
                strokeAlong(editor.doc, editor.inkStroke, static_cast<int>(image), part, brush);
            }
            part.clear();
        };
        for (ls::Vec2i at : images[image]) {
            if ((erasing || tiles) && !editor.selection.empty() && !editor.selection.contains(at)) {
                flush();
                closePaths(editor.inkStroke);
                continue;
            }
            if (!part.empty() && std::max(std::abs(at.x - part.back().x),
                                          std::abs(at.y - part.back().y)) > 1) {
                flush();
            }
            part.push_back(at);
        }
        flush();
    }
    canvas.invalidate();
}

// A spray's dots, laid down as dots -- each where it fell, wherever the
// layer is turned.
static void layDownDots(Editor& editor, CanvasView& canvas, std::vector<ls::Vec2i> dots, bool tiled) {
    const bool tiles = editor.tileTarget.layer.valid();
    if ((tiles && editor.tileErasing) || (!tiles && editor.inkStroke.erasing())) {
        layDown(editor, canvas, std::move(dots), tiled);
        return;
    }
    if (!tiles && editor.inkModeState.nothing) {
        return;                         // the mode allows nowhere
    }
    if (tiled) {
        for (ls::Vec2i& at : dots) {
            at = canvas.wrap(at);
        }
    }
    dots = mirrored(dots, symmetryNow(editor));
    if (!editor.selection.empty()) {
        dots.erase(std::remove_if(dots.begin(), dots.end(), [&](ls::Vec2i at) {
                       return !editor.selection.contains(at);
                   }),
                   dots.end());
    }
    if (dots.empty()) {
        return;
    }
    if (tiles) {
        strokeTileDots(editor, dots);
    } else if (editor.inkModeState.mode == InkMode::Shading) {
        InkModeState::Piece piece;
        piece.kind = InkModeState::Piece::Kind::Dots;
        piece.points = dots;
        shadeMark(editor.doc, editor.inkModeState, piece);
    } else {
        sprayDots(editor.doc, editor.inkStroke, dots);
    }
    canvas.invalidate();
}

// A custom brush colour's shape, round the pixel it is stamped on, as a
// stroke's tip -- and its mirror images, the copies symmetry paints: as
// drawn, across, down, both.
std::array<std::shared_ptr<const ls::AreaDesc>, 4> tipImages(const std::vector<ls::Vec2i>& offsets) {
    ls::IntervalSet set;
    for (ls::Vec2i p : offsets) {
        set.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    const ls::AreaDesc drawn = ls::geom::traceArea(ls::geom::normalize(set));
    std::array<std::shared_ptr<const ls::AreaDesc>, 4> out;
    for (int image = 0; image < 4; ++image) {
        ls::AreaDesc tip = drawn;
        for (auto& contour : tip.contours) {
            for (ls::Vec2f& q : contour) {
                if ((image & 1) != 0) { q.x = 1.f - q.x; }
                if ((image & 2) != 0) { q.y = 1.f - q.y; }
            }
        }
        out[static_cast<size_t>(image)] = std::make_shared<const ls::AreaDesc>(std::move(tip));
    }
    return out;
}

// Whether the pencil is stamping a custom brush rather than its own.
static bool usingCustomBrush(const Editor& editor) {
    return editor.tool == Tool::Pencil && editor.customBrushOn && !editor.customBrush.empty() &&
           !editor.tileTarget.layer.valid();
}

// A custom brush's colour, laid down the way layDown lays a stroke: wrapped,
// mirrored, kept inside the selection -- through its own ink stroke.
static void layDownWith(Editor& editor, CanvasView& canvas, const InkStroke& stroke,
                 std::vector<ls::Vec2i> run, bool tiled) {
    if (tiled) {
        for (ls::Vec2i& at : run) {
            at = canvas.wrap(at);
        }
    }
    run = mirrored(run, symmetryNow(editor));
    if (!editor.selection.empty()) {
        run.erase(std::remove_if(run.begin(), run.end(), [&](ls::Vec2i at) {
                      return !editor.selection.contains(at);
                  }),
                  run.end());
    }
    if (!run.empty()) {
        strokeInk(editor.doc, stroke, run);
        canvas.invalidate();
    }
}

// The ink under the cursor: the slot a pixel was painted through when the
// drawing knows it, and otherwise the colour on screen -- matched to a slot
// if one is exactly that colour, so picking a palette colour off a shape or
// a dither still paints through the palette.
static bool pickInk(Editor& editor, CanvasView& canvas, ls::Vec2i pixel, Ink* out) {
    if (inkAt(editor.doc, editor.sprite, pixel, out)) {
        return true;
    }
    // Where the artwork draws nothing, a reference shown there gives its own
    // colour -- the image's pixel, not the faded one on screen.
    const ls::Color* drawn = canvas.colorAt(pixel);
    ls::Color fromReference;
    const bool bare = drawn == nullptr || drawn->a == 0;
    if (bare && !referenceColourAt(editor.doc, pixel, &fromReference)) {
        return false;
    }
    const ls::Color* seen = bare ? &fromReference : drawn;
    out->colour = *seen;
    out->role = ls::kColorRoleNone;
    for (const PaletteEntry& entry :
         paletteEntries(editor.doc, paletteFor(editor.doc, editor.sprite))) {
        if (entry.color.r == seen->r && entry.color.g == seen->g &&
            entry.color.b == seen->b && entry.color.a == seen->a) {
            out->role = entry.role;
            break;
        }
    }
    return true;
}

void handleStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel) {
    // Alt belongs to the selection tools -- it subtracts -- so with one in
    // hand it does not also grab a reference.
    if ((editor.draggingReference || !isSelectionTool(editor.tool)) &&
        handleReferenceDrag(editor, canvas, overCanvas, pixel)) {
        return;
    }
    // A puppet being drawn on or posed: its joints, sockets and rings come
    // first, and while posing nothing draws.
    if (handlePuppetInput(editor, canvas, overCanvas)) {
        return;
    }
    // A shape's handles come before the tool: a press on one edits the
    // shape whatever the tool would have done there.
    if (handleTransformGizmo(editor, canvas, overCanvas)) {
        return;
    }
    if (handleFreeScale(editor, canvas, overCanvas)) {
        return;
    }
    if (handleShapeHandles(editor, canvas, overCanvas)) {
        return;
    }
    if (handleSliceInput(editor, canvas, overCanvas)) {
        return;
    }
    if (handleSelectionInput(editor, canvas, overCanvas, pixel)) {
        return;
    }
    if (handlePathInput(editor, canvas, overCanvas)) {
        return;
    }
    // The hand and the zoom tool are the canvas's own business.
    if (editor.tool == Tool::Hand || editor.tool == Tool::Zoom) {
        return;
    }
    if (handleTilemapStroke(editor, canvas, overCanvas, pixel)) {
        return;
    }
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    // A locked layer: the picker still reads, nothing writes. Said once per
    // press rather than per frame, or the status line would flicker.
    if (editor.tool != Tool::Picker && activeLayerLocked(editor) && overCanvas &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        editor.say("This layer is locked -- unlock it in the Layers panel");
        return;
    }
    if (editor.tool != Tool::Picker && activeLayerLocked(editor)) {
        return;
    }

    if (editor.tool == Tool::Picker) {
        const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (overCanvas && (left || right)) {
            Ink picked;
            if (!pickInk(editor, canvas, pixel, &picked)) {
                editor.say("Nothing to pick there");
                return;
            }
            if (left) { setForegroundInk(editor, picked); }
            else      { setBackgroundInk(editor, picked); }
            refreshInks(editor);
            editor.tool = editor.toolBeforePicker;
            editor.say(picked.usesSlot()
                ? "Picked slot " + std::to_string(picked.role) +
                  " -- painting with it follows the palette"
                : std::string("Picked the colour under the cursor"));
        }
        return;
    }

    // Shapes are created on press and driven while the mouse moves, so what is
    // on screen during the drag is the real object rather than a preview that
    // then has to be reproduced exactly.
    if (editor.tool == Tool::Rectangle || editor.tool == Tool::Ellipse ||
        editor.tool == Tool::Line) {
        const ShapeKind kind = editor.tool == Tool::Rectangle ? ShapeKind::Rectangle
                             : editor.tool == Tool::Ellipse   ? ShapeKind::Ellipse
                                                              : ShapeKind::Line;
        ls::Vec2f here { static_cast<float>(pixel.x), static_cast<float>(pixel.y) };
        if (editor.snapToGrid) {
            const ls::Vec2i point = nearestGridPoint(canvas.pointerExact(), editor.snapGrid);
            here = { static_cast<float>(point.x), static_cast<float>(point.y) };
        }
        // A box's corners are corners, but a line's ends are pixels: the
        // middle of the one pressed, or of the one a grid point is the top
        // left of. A point on a corner would be four pixels at once, and
        // which of them it draws would change with every turn and flip.
        if (kind == ShapeKind::Line) {
            here = linePoint(here);
        }

        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ShapeParams params;
            params.from = here;
            params.to = { here.x + 1.f, here.y + 1.f };
            params.cornerRadius = editor.shapeCorner;
            params.outline = editor.shapeOutline && kind != ShapeKind::Line;
            params.thickness = params.outline ? editor.shapeOutlineWidth : 1.f;
            // Onto the active layer as one more element, unless asked for a
            // layer per shape. Either way the panel then shows the thing
            // under the cursor.
            const bool ontoLayer = !editor.shapesOnOwnLayer && !activeLayerLocked(editor);
            bool made = false;
            const Ink ink = foregroundInk(editor);
            if (ontoLayer) {
                made = addShapeElement(editor.doc, layer->layer, kind, params, ink,
                                       &editor.pendingShape);
            } else if (activeLayerLocked(editor) && !editor.shapesOnOwnLayer) {
                editor.say("This layer is locked -- unlock it, or draw shapes on their own layer");
            } else {
                // One history entry for the layer and its slot.
                editor.doc.beginAction(shapeKindName(kind));
                made = createShapeLayer(editor.doc, editor.sprite, kind, params,
                                        ink.colour, &editor.pendingShape);
                if (made && ink.usesSlot()) {
                    setLayerRole(editor.doc, editor.pendingShape.paint, ink.role);
                }
                editor.doc.endAction();
            }
            if (made) {
                editor.draggingShape = true;
                editor.shapeAnchor = here;
                if (ontoLayer) {
                    editor.activeElement = editor.pendingShape.paint.fill;
                } else {
                    selectLayer(editor, editor.pendingShape.paint.layer);
                    editor.activeElement = editor.pendingShape.paint.fill;
                }
                canvas.invalidate();
            }
        }

        if (editor.draggingShape && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ShapeParams params;
            const ls::Vec2f anchor = editor.shapeAnchor;
            ls::Vec2f to = here;
            if (ImGui::GetIO().KeyShift) {
                const float dx = here.x - anchor.x;
                const float dy = here.y - anchor.y;
                if (kind == ShapeKind::Line) {
                    // Fifteen-degree steps: horizontal, vertical, the
                    // diagonals, and the slopes pixel art actually uses
                    // between them.
                    const float step = 3.14159265f / 12.f;
                    const float angle = std::round(std::atan2(dy, dx) / step) * step;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    to = { anchor.x + std::round(std::cos(angle) * length),
                           anchor.y + std::round(std::sin(angle) * length) };
                } else {
                    const float side = std::max(std::fabs(dx), std::fabs(dy));
                    to = { anchor.x + (dx < 0.f ? -side : side),
                           anchor.y + (dy < 0.f ? -side : side) };
                }
            }
            if (kind == ShapeKind::Line || editor.snapToGrid) {
                // A line's two points are the drag itself; grid points are
                // corners already.
                params.from = anchor;
                params.to = to;
            } else {
                // The box holds both end pixels -- the one pressed on and the
                // one under the pointer -- as a marquee does, whichever way
                // the drag went.
                params.from = { std::min(anchor.x, to.x), std::min(anchor.y, to.y) };
                params.to = { std::max(anchor.x, to.x) + 1.f, std::max(anchor.y, to.y) + 1.f };
            }
            params.cornerRadius = editor.shapeCorner;
            updateShape(editor.doc, editor.pendingShape, params);
            canvas.invalidate();
        }

        if (editor.draggingShape && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            editor.draggingShape = false;
            editor.say(std::string(shapeKindName(kind)) +
                       " drawn, and still editable");
        }
        return;
    }

    if (editor.tool == Tool::Bucket) {
        const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (overCanvas && (left || right)) {
            editor.doc.beginAction("Fill");
            InkStroke stroke;
            bool filled = beginPaint(editor, *layer, right && !left, &stroke);
            if (filled) {
                // With symmetry on, each mirror of the click fills as well --
                // in the order given, so a fill that already covered a mirror
                // leaves nothing for it to do.
                bool any = false;
                for (ls::Vec2i seed : mirrored({ pixel }, symmetryNow(editor))) {
                    any = bucketFill(editor.doc, editor.sprite, stroke, seed, editor.bucket,
                                     editor.selection.empty() ? nullptr
                                                              : &editor.selection.mask) || any;
                }
                filled = any;
            }
            if (filled) {
                // The fill is an element of its own; a run made only to say
                // what colour it is goes again.
                pruneEmptyInks(editor.doc, layer->layer);
                editor.doc.endAction();
                resyncLayers(editor);
            } else {
                editor.doc.abandonAction();
            }
            canvas.invalidate();
            editor.say(filled ? "Filled" : "Nothing to fill there");
        }
        return;
    }

    // Text: a click says where; the words are typed in the window that opens.
    if (editor.tool == Tool::Text) {
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space) && !editor.textOpen) {
            editor.textOpen = true;
            editor.textAt = pixel;
        }
        return;
    }

    // Gradient: the press makes the element over the area -- the selection,
    // or what a fill would find -- and the drag drives its axis, so what is
    // on screen during the drag is the gradient itself. All of it in the
    // layer's own pixels, so on a turned layer it lands where it was drawn.
    if (editor.tool == Tool::Gradient) {
        const ls::Vec2f pointer { static_cast<float>(canvas.pointerPixel().x) + 0.5f,
                                  static_cast<float>(canvas.pointerPixel().y) + 0.5f };
        ls::Vec2f here = pointer;
        if (!listTransforms(editor.doc, layer->layer).empty()) {
            auto back = layerTransform(editor.doc, layer->layer).inverse();
            if (back.ok()) {
                here = back.value.transformPoint(pointer);
            }
        }
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space)) {
            const bool finds = editor.selection.empty();
            const ls::IntervalSet area = finds
                ? bucketAreaOnLayer(editor.doc, editor.sprite, layer->layer, pixel, editor.bucket)
                : canvasAreaOnLayer(editor.doc, layer->layer, editor.selection.mask);
            DitherSettings settings;
            settings.pattern = editor.dither.pattern;
            settings.modulation = ls::DitherModulation::Linear;
            const Ink front = foregroundInk(editor);
            const Ink back = backgroundInk(editor);
            settings.from = front.colour;
            settings.fromRole = front.role;
            settings.to = back.colour;
            settings.toRole = back.role;
            settings.gradientStart = here;
            settings.gradientEnd = { here.x + 1.f, here.y };
            editor.doc.beginAction("Gradient");
            if (!area.empty() && addGradientElement(editor.doc, layer->layer, area, finds, settings,
                                                    &editor.gradientElement)) {
                editor.drawingGradient = true;
                editor.gradientSettings = settings;
                editor.gradientFrom = pointer;
                editor.gradientTo = pointer;
            } else {
                editor.doc.abandonAction();
                editor.say("Nothing to lay a gradient over there");
            }
        }
        if (editor.drawingGradient && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (here.x != editor.gradientSettings.gradientEnd.x ||
                here.y != editor.gradientSettings.gradientEnd.y) {
                editor.gradientSettings.gradientEnd = here;
                editor.gradientTo = pointer;
                applyDitherSettings(editor.doc, editor.gradientElement,
                                    editor.gradientSettings);
            }
        }
        if (editor.drawingGradient && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor.drawingGradient = false;
            pruneEmptyInks(editor.doc, layer->layer, editor.gradientElement.fill);
            editor.doc.endAction();
            // The panel then shows the gradient, ready to be adjusted.
            editor.activeElement = editor.gradientElement.fill;
            editor.paintIntoElement = false;
            resyncLayers(editor);
            editor.say("A gradient that stays one -- adjust it in the Element panel");
        }
        return;
    }

    // Contour: the outline is drawn as the pointer goes, and on release what it
    // encloses -- outline included -- is painted in one go.
    if (editor.tool == Tool::Contour) {
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space)) {
            editor.drawingContour = true;
            editor.contourPoints.assign(1, pixel);
        }
        if (editor.drawingContour && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ls::Vec2i at = canvas.pointerPixel();
            const ls::Vec2i last = editor.contourPoints.back();
            if (at.x != last.x || at.y != last.y) {
                editor.contourPoints.push_back(at);
            }
        }
        if (editor.drawingContour && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor.drawingContour = false;
            auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
            // In the layer's own pixels: the contour is drawn over the canvas,
            // and a turned layer's drawing is not where the canvas shows it.
            const std::vector<ls::Vec2i> inside = pixelsOf(canvasAreaOnLayer(
                editor.doc, layer->layer,
                clipToCanvas(lassoMask(editor.contourPoints),
                             size.ok() ? static_cast<uint32_t>(size.value.x) : 0u,
                             size.ok() ? static_cast<uint32_t>(size.value.y) : 0u)));
            editor.contourPoints.clear();
            editor.doc.beginAction("Contour");
            editor.strokeWithBack = false;
            if (!inside.empty() && beginPaint(editor, *layer, false, &editor.inkStroke) &&
                beginInkMode(editor.doc, layer->layer, InkMode::Simple, Ink{}, {},
                             &editor.inkModeState)) {
                layDown(editor, canvas, inside, false);
                pruneEmptyInks(editor.doc, layer->layer, editor.inkStroke.target.fill);
                editor.doc.endAction();
                resyncLayers(editor);
            } else {
                editor.doc.abandonAction();
            }
            editor.inkStroke = InkStroke{};
        }
        return;
    }

    // In tiled mode the stroke follows the pointer as it really moved, off
    // the edge and all, and each pixel is brought back onto the canvas after
    // the path is drawn -- wrapping first would draw a line straight across
    // the canvas every time the pointer crossed a seam.
    const bool tiled = canvas.tiledMode() != TiledMode::None;
    if (tiled && overCanvas) {
        pixel = canvas.pointerPixel();
    }

    // A layer shown rotated is still drawn on straight. The pencil writes into
    // the region and the transform acts on it afterwards, so the point under
    // the cursor has to be carried back into the layer's own space.
    if (overCanvas) {
        ls::Vec2f mapped;
        if (mapCanvasPointToLayer(editor.doc, layer->layer,
                                  {static_cast<float>(pixel.x),
                                   static_cast<float>(pixel.y)}, &mapped)) {
            pixel = { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
                      static_cast<int32_t>(std::floor(mapped.y + 0.5f)) };
        } else {
            editor.say("This transform cannot be drawn through");
            overCanvas = false;
        }
    }

    // A whole drag is one history entry, so the bracket opens on press and
    // closes on release rather than per sample. Which element gains the
    // pixels, and which lose them, is settled here once: the ink's own
    // element, made at the top of the layer the first time a colour is used.
    const bool pressedLeft = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool pressedRight = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (overCanvas && !editor.stroking && (pressedLeft || pressedRight) &&
        !ImGui::IsKeyDown(ImGuiKey_Space)) {
        const bool back = pressedRight && !pressedLeft;
        const bool erasing = editor.tool == Tool::Eraser;
        editor.doc.beginAction(editor.tool == Tool::Pencil ? "Pencil"
                               : editor.tool == Tool::Spray ? "Spray" : "Eraser");
        // Replace turns the other button's colour into this one's.
        const Ink other = back ? foregroundInk(editor) : backgroundInk(editor);
        // On a tilemap layer the stroke goes into the tiles under it.
        TilemapLayer tilemap;
        const bool intoTiles = readTilemapLayer(editor.doc, layer->layer, &tilemap);
        if (intoTiles) {
            editor.tileTarget = tilemap;
            editor.tileStrokes.clear();
            editor.tileErasing = erasing;
            editor.tileInk = back ? backgroundInk(editor) : foregroundInk(editor);
            editor.inkStroke = InkStroke{};
            editor.inkModeState = InkModeState{};
        }
        // Paint is kept inside the selection, and to what the ink mode
        // allows, by a clip on its run: the stroke stays a path.
        const ls::IntervalSet selected = editor.selection.empty() || erasing
            ? ls::IntervalSet{}
            : canvasAreaOnLayer(editor.doc, layer->layer, editor.selection.mask);
        const bool ready = intoTiles ||
            ((erasing ? beginEraseStroke(editor.doc, layer->layer, &editor.inkStroke)
                      : beginPaint(editor, *layer, back, &editor.inkStroke)) &&
             beginInkMode(editor.doc, layer->layer, erasing ? InkMode::Simple : editor.inkMode,
                          other, paletteRamp(editor), &editor.inkModeState,
                          editor.inkStroke.target.fill, &selected) &&
             keepStrokeToMode(editor.doc, editor.inkModeState, editor.inkStroke));
        editor.inkModeState.forward = !back;
        if (!ready) {
            editor.doc.abandonAction();
            editor.say("Nothing to draw on here");
            return;
        }
        editor.stroking = true;
        editor.strokeWithBack = back;
        editor.stabiliser.reset(canvas.pointerExact());
        editor.brushStrokes.clear();
        if (usingCustomBrush(editor)) {
            // A stroke per colour of the brush, all made first so that each
            // one's list of the layer's other colours includes the rest.
            std::vector<ls::OperationId> targets;
            for (const PixelClip::Piece& piece : editor.customBrush.pieces) {
                Ink ink = editor.customBrushOwnColours ? piece.ink
                        : (back ? backgroundInk(editor) : foregroundInk(editor));
                InkStroke made;
                if (beginInkStroke(editor.doc, layer->layer, ink, &made)) {
                    targets.push_back(made.target.fill);
                    editor.brushStrokes.push_back(made);
                }
            }
            for (InkStroke& made : editor.brushStrokes) {
                beginElementStroke(editor.doc, made.target, &made);
                keepStrokeToMode(editor.doc, editor.inkModeState, made);
            }
            editor.brushTips.clear();
            for (const std::vector<ls::Vec2i>& shape : stampOf(editor.customBrush, { 0, 0 })) {
                editor.brushTips.push_back(tipImages(shape));
            }
        }
        // Shift+click: a straight line from where the last stroke ended.
        editor.lastPixel = ImGui::GetIO().KeyShift && editor.lastStrokeEnd.x >= 0
                               ? editor.lastStrokeEnd : pixel;
        editor.pixelPerfect.reset();
    }

    const ImGuiMouseButton button = editor.strokeWithBack ? ImGuiMouseButton_Right
                                                          : ImGuiMouseButton_Left;
    // With the stabiliser on, the stroke follows the point on the string
    // rather than the pointer itself -- only for the pencil and eraser, and
    // only where the layer is not transformed, since the point is in canvas
    // pixels.
    if (editor.stroking && editor.brush.stabiliser > 0 && editor.tool != Tool::Spray &&
        ImGui::IsMouseDown(button) && listTransforms(editor.doc, layer->layer).empty()) {
        const ls::Vec2f held = editor.stabiliser.follow(
            canvas.pointerExact(), static_cast<float>(editor.brush.stabiliser));
        pixel = { static_cast<int32_t>(std::floor(held.x)),
                  static_cast<int32_t>(std::floor(held.y)) };
        overCanvas = true;
    }
    if (editor.stroking && ImGui::IsMouseDown(button) && overCanvas) {
        // Interpolate: the mouse reports once a frame, not once a pixel. The
        // path is then laid down through the brush -- stamped at its size, or
        // at one pixel through the pixel-perfect filter, which holds each
        // point until the next says whether it was the corner of an L.
        const int size = editor.pen.down ? pressuredSize(editor.brush, editor.pen.pressure)
                                         : editor.brush.size;
        const bool perfect = size == 1 && editor.brush.pixelPerfect &&
                             editor.tool == Tool::Pencil;
        PenBrush brush;
        brush.size = size;
        brush.round = editor.brush.round;
        brush.pixelPerfect = perfect;
        std::vector<ls::Vec2i> run;
        if (usingCustomBrush(editor) && !tiled && !editor.inkModeState.nothing) {
            // Each colour of the brush a path whose tip is that colour's
            // shape, stamped at every pixel it passes: a stroke that turns
            // with the drawing, carrying on from the last piece.
            const std::vector<ls::Vec2i> path =
                editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{ pixel }
                                       : linePixels(editor.lastPixel, pixel);
            const Symmetry symmetry = symmetryNow(editor);
            for (size_t k = 0; k < editor.brushStrokes.size() && k < editor.brushTips.size(); ++k) {
                for (int image = 0; image < 4; ++image) {
                    const bool across = (image & 1) != 0;
                    const bool down = (image & 2) != 0;
                    if ((across && !symmetry.across) || (down && !symmetry.down)) {
                        continue;
                    }
                    std::vector<ls::Vec2i> centres = path;
                    for (ls::Vec2i& p : centres) {
                        if (across) { p.x = symmetry.axisX - 1 - p.x; }
                        if (down)   { p.y = symmetry.axisY - 1 - p.y; }
                    }
                    PenBrush tipped;
                    tipped.tip = editor.brushTips[k][static_cast<size_t>(image)];
                    strokeAlong(editor.doc, editor.brushStrokes[k], image, centres, tipped);
                }
            }
            canvas.invalidate();
            editor.lastPixel = pixel;
        } else if (usingCustomBrush(editor)) {
            // Tiled, the stamps wrap round the edges pixel by pixel.
            const std::vector<ls::Vec2i> path =
                editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{ pixel }
                                       : linePixels(editor.lastPixel, pixel);
            for (size_t n = 0; n < path.size(); ++n) {
                if (n == 0 && editor.lastPixel.x >= 0 && path.size() > 1) {
                    continue;              // stamped already, at the end of the last run
                }
                const std::vector<std::vector<ls::Vec2i>> stamp =
                    stampOf(editor.customBrush, path[n]);
                for (size_t k = 0; k < stamp.size() && k < editor.brushStrokes.size(); ++k) {
                    layDownWith(editor, canvas, editor.brushStrokes[k], stamp[k], tiled);
                }
            }
            editor.lastPixel = pixel;
        } else if (editor.tool == Tool::Spray) {
            // A burst every frame the button is held, moving or not, as a can
            // keeps spraying where it is pointed.
            layDownDots(editor, canvas,
                        sprayPixels(pixel, editor.sprayRadius, editor.sprayDensity,
                                    ++editor.sprayBursts),
                        tiled);
        } else if (perfect) {
            const std::vector<ls::Vec2i> path =
                editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{pixel}
                                       : linePixels(editor.lastPixel, pixel);
            for (ls::Vec2i point : path) {
                for (ls::Vec2i ready : editor.pixelPerfect.push(point)) {
                    run.push_back(ready);
                }
            }
        } else {
            // The centres the brush is stamped on; the mark keeps the brush.
            run = editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{ pixel }
                                         : linePixels(editor.lastPixel, pixel);
        }
        if (!usingCustomBrush(editor) && editor.tool != Tool::Spray) {
            layDownPath(editor, canvas, std::move(run), brush, tiled);
        }
        editor.lastPixel = pixel;
    }

    if (editor.stroking && ImGui::IsMouseReleased(button)) {
        // The filter's last point, held until now.
        std::vector<ls::Vec2i> rest = editor.pixelPerfect.finish();
        if (!rest.empty() && editor.tool == Tool::Pencil && !usingCustomBrush(editor)) {
            PenBrush brush;
            brush.pixelPerfect = true;
            layDownPath(editor, canvas, std::move(rest), brush, tiled);
        }
        editor.lastStrokeEnd = editor.lastPixel;
        const bool intoTiles = editor.tileTarget.layer.valid();
        if (intoTiles) {
            endTileStroke(editor);
        }
        // A colour painted out entirely, or erased away, leaves the element
        // list rather than lingering as an element that draws nothing.
        int pruned = intoTiles ? 0 : pruneEmptyInks(editor.doc, editor.inkStroke.layer,
                                                    editor.inkStroke.target.fill);
        if (!editor.brushStrokes.empty()) {
            pruned += pruneEmptyInks(editor.doc, editor.inkStroke.layer);
            editor.brushStrokes.clear();
            editor.brushTips.clear();
        }
        // Shading lays colours down through strokes of its own; a slot it
        // stepped every pixel out of goes the same way.
        if (!editor.inkModeState.strokes.empty()) {
            pruned += pruneEmptyInks(editor.doc, editor.inkStroke.layer);
        }
        editor.doc.endAction();
        editor.stroking = false;
        editor.lastPixel = { -1, -1 };
        if (pruned > 0 || !editor.inkStroke.erasing()) {
            resyncLayers(editor);
        }
        editor.inkStroke = InkStroke{};
        editor.inkModeState = InkModeState{};
    }
}


// The symmetry axes, as thin dashed lines across the canvas, so it is never a
// surprise that a stroke is being mirrored.
void drawSymmetryAxes(Editor& editor, const CanvasView& canvas, ImDrawList* draw,
                      ImVec2 origin, float zoom) {
    if (!editor.symmetryAcross && !editor.symmetryDown) {
        return;
    }
    const Symmetry symmetry = symmetryNow(editor);
    const float width = static_cast<float>(canvas.compiledWidth()) * zoom;
    const float height = static_cast<float>(canvas.compiledHeight()) * zoom;
    const ImU32 colour = IM_COL32(255, 170, 60, 170);
    const float dash = 6.f;
    if (symmetry.across) {
        const float x = origin.x + static_cast<float>(symmetry.axisX) * 0.5f * zoom;
        for (float y = 0.f; y < height; y += dash * 2.f) {
            draw->AddLine(ImVec2(x, origin.y + y),
                          ImVec2(x, origin.y + std::min(y + dash, height)), colour, 1.f);
        }
    }
    if (symmetry.down) {
        const float y = origin.y + static_cast<float>(symmetry.axisY) * 0.5f * zoom;
        for (float x = 0.f; x < width; x += dash * 2.f) {
            draw->AddLine(ImVec2(origin.x + x, y),
                          ImVec2(origin.x + std::min(x + dash, width), y), colour, 1.f);
        }
    }
}


} // namespace fast
