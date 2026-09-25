// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/shape_tools.h"

#include "app/element.h"
#include "app/grid_snap.h"
#include "app/layers.h"
#include "app/palette.h"

#include <algorithm>
#include <cmath>

namespace fast {

namespace {

// How near, in screen pixels, a press must be to take a handle.
constexpr float kGrab = 7.f;

bool isPathTool(Tool tool) {
    return tool == Tool::Polygon || tool == Tool::Curve;
}

// The tools under which a shape's handles are live.
bool handlesLive(const Editor& editor) {
    switch (editor.tool) {
        case Tool::Move: case Tool::Rectangle: case Tool::Ellipse: case Tool::Line:
        case Tool::Polygon: case Tool::Curve:
            return !editor.placingPath && !editor.draggingShape;
        default:
            return false;
    }
}

// Where a point is placed: on the grid when snapping, otherwise the middle of
// the pixel pressed -- so a corner clicked on a pixel is that pixel.
ls::Vec2f pointHere(const Editor& editor, const CanvasView& canvas) {
    const ls::Vec2f at = canvas.pointerExact();
    if (editor.snapToGrid) {
        const ls::Vec2i p = nearestGridPoint(at, editor.snapGrid);
        return { static_cast<float>(p.x), static_cast<float>(p.y) };
    }
    return { std::floor(at.x) + 0.5f, std::floor(at.y) + 0.5f };
}

// Where a dragged handle lands, by the same rule each kind was drawn with:
// a box's corners and a line's ends on whole pixels, a path's points in the
// middle of one.
ls::Vec2f handleTarget(const Editor& editor, const CanvasView& canvas, ShapeKind kind,
                       bool control) {
    const ls::Vec2f at = canvas.pointerExact();
    if (control) {
        return at;                              // control points go anywhere
    }
    if (editor.snapToGrid) {
        const ls::Vec2i p = nearestGridPoint(at, editor.snapGrid);
        return { static_cast<float>(p.x), static_cast<float>(p.y) };
    }
    if (kind == ShapeKind::Polygon || kind == ShapeKind::Curve) {
        return { std::floor(at.x) + 0.5f, std::floor(at.y) + 0.5f };
    }
    if (kind == ShapeKind::Line) {
        return { std::floor(at.x), std::floor(at.y) };
    }
    return { std::round(at.x), std::round(at.y) };
}

bool near(ls::Vec2f a, ls::Vec2f b, float zoom) {
    const float dx = (a.x - b.x) * zoom;
    const float dy = (a.y - b.y) * zoom;
    return dx * dx + dy * dy <= kGrab * kGrab;
}

void cancelPath(Editor& editor) {
    editor.placingPath = false;
    editor.pullingHandle = false;
    editor.pathPoints.clear();
    editor.pathHandles.clear();
}

ImVec2 screen(ImVec2 origin, float zoom, ls::Vec2f p) {
    return ImVec2(origin.x + p.x * zoom, origin.y + p.y * zoom);
}

void drawAnchor(ImDrawList* draw, ImVec2 at, bool hot) {
    const float r = hot ? 5.f : 4.f;
    draw->AddRectFilled(ImVec2(at.x - r, at.y - r), ImVec2(at.x + r, at.y + r),
                        IM_COL32(0, 0, 0, 200));
    draw->AddRectFilled(ImVec2(at.x - r + 1.f, at.y - r + 1.f),
                        ImVec2(at.x + r - 1.f, at.y + r - 1.f),
                        hot ? IM_COL32(255, 180, 60, 255) : IM_COL32(255, 255, 255, 240));
}

void drawControl(ImDrawList* draw, ImVec2 at, ImVec2 anchor, bool hot) {
    draw->AddLine(anchor, at, IM_COL32(0, 0, 0, 160), 3.f);
    draw->AddLine(anchor, at, IM_COL32(255, 255, 255, 200), 1.f);
    draw->AddCircleFilled(at, hot ? 5.f : 4.f, IM_COL32(0, 0, 0, 200));
    draw->AddCircleFilled(at, hot ? 4.f : 3.f,
                          hot ? IM_COL32(255, 180, 60, 255) : IM_COL32(255, 255, 255, 240));
}

// A cubic's points, for a preview drawn with lines.
void drawCubic(ImDrawList* draw, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 colour,
               float width) {
    ImVec2 previous = a;
    for (int i = 1; i <= 24; ++i) {
        const float t = static_cast<float>(i) / 24.f;
        const float u = 1.f - t;
        const ImVec2 p(u * u * u * a.x + 3.f * u * u * t * b.x + 3.f * u * t * t * c.x +
                           t * t * t * d.x,
                       u * u * u * a.y + 3.f * u * u * t * b.y + 3.f * u * t * t * c.y +
                           t * t * t * d.y);
        draw->AddLine(previous, p, colour, width);
        previous = p;
    }
}

} // namespace

bool activeShape(Editor& editor, ShapeLayer* out) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr || !editor.activeElement.valid()) {
        return false;
    }
    for (const Element& element : elementsOf(editor.doc, layer->layer)) {
        if (element.fill == editor.activeElement && element.isGeometry()) {
            *out = shapeOfElement(layer->layer, element);
            return true;
        }
    }
    return false;
}

bool placeShape(Editor& editor, CanvasView& canvas, ShapeKind kind, const ShapeParams& params) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return false;
    }
    const Ink ink = foregroundInk(editor);
    ShapeLayer made;
    bool ok = false;
    if (!editor.shapesOnOwnLayer && !layerLocked(editor.doc, layer->layer)) {
        ok = addShapeElement(editor.doc, layer->layer, kind, params, ink, &made);
    } else if (!editor.shapesOnOwnLayer) {
        editor.say("This layer is locked -- unlock it, or draw shapes on their own layer");
        return false;
    } else {
        editor.doc.beginAction(shapeKindName(kind));
        ok = createShapeLayer(editor.doc, editor.sprite, kind, params, ink.colour, &made);
        if (ok && ink.usesSlot()) {
            setLayerRole(editor.doc, made.paint, ink.role);
        }
        editor.doc.endAction();
        if (ok) {
            resyncLayers(editor);
            selectLayer(editor, made.paint.layer);
        }
    }
    if (!ok) {
        editor.say(std::string("Could not place the ") + shapeKindName(kind));
        return false;
    }
    editor.activeElement = made.paint.fill;
    canvas.invalidate();
    return true;
}

bool finishPath(Editor& editor, CanvasView& canvas, bool closed) {
    const bool curve = editor.tool == Tool::Curve;
    ShapeParams params;
    params.outline = editor.shapeOutline && !curve;
    params.thickness = params.outline ? editor.shapeOutlineWidth : 1.f;
    if (curve) {
        if (editor.pathPoints.size() < 2) {
            editor.say("A curve needs two points at least");
            cancelPath(editor);
            return false;
        }
        params.points = curveThrough(editor.pathPoints, editor.pathHandles, closed);
        params.closed = closed;
    } else {
        if (editor.pathPoints.size() < 3) {
            editor.say("A polygon needs three corners at least");
            cancelPath(editor);
            return false;
        }
        params.points = editor.pathPoints;
    }
    const size_t count = editor.pathPoints.size();
    cancelPath(editor);
    if (!placeShape(editor, canvas, curve ? ShapeKind::Curve : ShapeKind::Polygon, params)) {
        return false;
    }
    editor.say(curve ? std::string(closed ? "A filled curve through " : "A curve through ") +
                           std::to_string(count) +
                           " points -- drag its points and handles to change it"
                     : "A polygon of " + std::to_string(count) +
                           " corners -- drag its corners to change it");
    return true;
}

bool handlePathInput(Editor& editor, CanvasView& canvas, bool overCanvas) {
    if (!isPathTool(editor.tool)) {
        if (editor.placingPath) {
            cancelPath(editor);         // a tool change lets the path go
        }
        return false;
    }
    const bool curve = editor.tool == Tool::Curve;
    const float zoom = canvas.zoom();

    // The handle of the anchor just placed, drawn out while the button is held.
    if (editor.pullingHandle) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !editor.pathPoints.empty()) {
            const ls::Vec2f anchor = editor.pathPoints.back();
            const ls::Vec2f at = canvas.pointerExact();
            ls::Vec2f pull { at.x - anchor.x, at.y - anchor.y };
            if (pull.x * pull.x + pull.y * pull.y < 0.25f) {
                pull = { 0.f, 0.f };    // a click, not a drag: a corner
            }
            editor.pathHandles.back() = pull;
        } else {
            editor.pullingHandle = false;
        }
        return true;
    }

    if (!overCanvas || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsKeyDown(ImGuiKey_Space)) {
        return editor.placingPath;
    }
    const ls::Vec2f here = pointHere(editor, canvas);
    if (editor.placingPath) {
        const bool onFirst = !editor.pathPoints.empty() &&
                             near(here, editor.pathPoints.front(), zoom) &&
                             editor.pathPoints.size() >= (curve ? 2u : 3u);
        if (onFirst) {
            finishPath(editor, canvas, true);
            return true;
        }
        // A double-click finishes: its first click already placed the last
        // point, so the second places nothing.
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            finishPath(editor, canvas, !curve);
            return true;
        }
        // A click on the point just placed adds nothing: two corners in one
        // place are one corner.
        if (!editor.pathPoints.empty() && near(here, editor.pathPoints.back(), zoom)) {
            return true;
        }
    } else {
        editor.placingPath = true;
        editor.pathPoints.clear();
        editor.pathHandles.clear();
    }
    editor.pathPoints.push_back(here);
    editor.pathHandles.push_back({ 0.f, 0.f });
    editor.pullingHandle = curve;
    return true;
}

bool handlePathKeys(Editor& editor, CanvasView& canvas) {
    if (!editor.placingPath) {
        return false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelPath(editor);
        editor.say("Let go");
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        finishPath(editor, canvas, editor.tool == Tool::Polygon);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        if (editor.pathPoints.size() > 1) {
            editor.pathPoints.pop_back();
            editor.pathHandles.pop_back();
        } else {
            cancelPath(editor);
        }
        return true;
    }
    return false;
}

bool handleShapeHandles(Editor& editor, CanvasView& canvas, bool overCanvas) {
    if (editor.draggingHandle >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const size_t index = static_cast<size_t>(editor.draggingHandle);
            ShapeParams params = editor.handleStart;
            moveShapeHandle(editor.handleShape.kind, params, index,
                            handleTarget(editor, canvas, editor.handleShape.kind,
                                         isControlHandle(editor.handleShape.kind, index)));
            updateShape(editor.doc, editor.handleShape, params);
            canvas.invalidate();
        } else {
            editor.draggingHandle = -1;
            editor.doc.endAction();
        }
        return true;
    }
    if (!handlesLive(editor) || !overCanvas || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsKeyDown(ImGuiKey_Space)) {
        return false;
    }
    ShapeLayer shape;
    ShapeParams params;
    if (!activeShape(editor, &shape) || !readShapeParams(editor.doc, shape, &params) ||
        layerLocked(editor.doc, shape.paint.layer)) {
        return false;
    }
    const std::vector<ls::Vec2f> handles = shapeHandles(shape.kind, params);
    const ls::Vec2f at = canvas.pointerExact();
    // Controls first: they sit on top, and an anchor under one is still
    // reachable by pulling the control away.
    int hit = -1;
    for (int pass = 0; pass < 2 && hit < 0; ++pass) {
        for (size_t i = 0; i < handles.size(); ++i) {
            if ((pass == 0) == isControlHandle(shape.kind, i) && near(at, handles[i], canvas.zoom())) {
                hit = static_cast<int>(i);
                break;
            }
        }
    }
    if (hit < 0) {
        return false;
    }
    editor.doc.beginAction(std::string("Edit ") + shapeKindName(shape.kind));
    editor.draggingHandle = hit;
    editor.handleShape = shape;
    editor.handleStart = params;
    return true;
}

void drawShapeOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                      float zoom) {
    const ls::Vec2f pointer = canvas.pointerExact();
    const ImU32 ink = ImGui::GetColorU32(ImVec4(editor.color[0], editor.color[1],
                                                editor.color[2], 1.f));
    if (editor.placingPath && !editor.pathPoints.empty()) {
        const std::vector<ls::Vec2f>& points = editor.pathPoints;
        const bool curve = editor.tool == Tool::Curve;
        const ls::Vec2f next = pointHere(editor, canvas);
        if (curve) {
            // What is placed so far, then the segment the next click would make.
            std::vector<ls::Vec2f> anchors = points;
            std::vector<ls::Vec2f> pulls = editor.pathHandles;
            if (!editor.pullingHandle) {
                anchors.push_back(next);
                pulls.push_back({ 0.f, 0.f });
            }
            const std::vector<ls::Vec2f> path = curveThrough(anchors, pulls, false);
            for (size_t i = 0; i + 3 < path.size(); i += 3) {
                const bool preview = !editor.pullingHandle && i + 3 == path.size() - 1;
                drawCubic(draw, screen(origin, zoom, path[i]), screen(origin, zoom, path[i + 1]),
                          screen(origin, zoom, path[i + 2]), screen(origin, zoom, path[i + 3]),
                          IM_COL32(0, 0, 0, 160), 3.f);
                drawCubic(draw, screen(origin, zoom, path[i]), screen(origin, zoom, path[i + 1]),
                          screen(origin, zoom, path[i + 2]), screen(origin, zoom, path[i + 3]),
                          preview ? IM_COL32(255, 255, 255, 150) : ink, 1.5f);
            }
            for (size_t i = 0; i < points.size(); ++i) {
                const ls::Vec2f pull = editor.pathHandles[i];
                if (pull.x != 0.f || pull.y != 0.f) {
                    const ImVec2 a = screen(origin, zoom, points[i]);
                    drawControl(draw, screen(origin, zoom, { points[i].x + pull.x, points[i].y + pull.y }), a, false);
                    drawControl(draw, screen(origin, zoom, { points[i].x - pull.x, points[i].y - pull.y }), a, false);
                }
            }
        } else {
            for (size_t i = 1; i < points.size(); ++i) {
                draw->AddLine(screen(origin, zoom, points[i - 1]), screen(origin, zoom, points[i]),
                              IM_COL32(0, 0, 0, 160), 3.f);
                draw->AddLine(screen(origin, zoom, points[i - 1]), screen(origin, zoom, points[i]),
                              ink, 1.5f);
            }
            draw->AddLine(screen(origin, zoom, points.back()), screen(origin, zoom, next),
                          IM_COL32(255, 255, 255, 150), 1.f);
            if (points.size() >= 2) {
                draw->AddLine(screen(origin, zoom, next), screen(origin, zoom, points.front()),
                              IM_COL32(255, 255, 255, 80), 1.f);
            }
        }
        for (size_t i = 0; i < points.size(); ++i) {
            const bool closes = i == 0 && points.size() >= (curve ? 2u : 3u) &&
                                near(pointer, points.front(), zoom);
            drawAnchor(draw, screen(origin, zoom, points[i]), closes);
        }
        return;
    }

    if (!handlesLive(editor) && editor.draggingHandle < 0) {
        return;
    }
    ShapeLayer shape;
    ShapeParams params;
    if (!activeShape(editor, &shape) || !readShapeParams(editor.doc, shape, &params)) {
        return;
    }
    const std::vector<ls::Vec2f> handles = shapeHandles(shape.kind, params);
    const auto hot = [&](size_t i) {
        return editor.draggingHandle == static_cast<int>(i) ||
               (editor.draggingHandle < 0 && near(pointer, handles[i], zoom));
    };
    // Controls with their lines, then the points the shape passes through.
    if (shape.kind == ShapeKind::Curve) {
        for (size_t i = 0; i < handles.size(); ++i) {
            if (!isControlHandle(shape.kind, i)) {
                continue;
            }
            const size_t anchor = i % 3 == 1 ? i - 1 : i + 1;
            if (anchor < params.points.size()) {
                drawControl(draw, screen(origin, zoom, handles[i]),
                            screen(origin, zoom, params.points[anchor]), hot(i));
            }
        }
    }
    for (size_t i = 0; i < handles.size(); ++i) {
        if (!isControlHandle(shape.kind, i)) {
            drawAnchor(draw, screen(origin, zoom, handles[i]), hot(i));
        }
    }
}

} // namespace fast
