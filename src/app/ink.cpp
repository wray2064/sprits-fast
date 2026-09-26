// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/ink.h"
#include "app/shape.h"

#include "app/element.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>

namespace fast {

bool operator==(const Ink& a, const Ink& b) {
    // Two inks through the same slot are the same ink whatever colour each
    // remembers as its fallback: the slot is what they paint.
    if (a.role != b.role) {
        return false;
    }
    if (a.usesSlot()) {
        return true;
    }
    return a.colour.r == b.colour.r && a.colour.g == b.colour.g &&
           a.colour.b == b.colour.b && a.colour.a == b.colour.a;
}

namespace {

bool isSolid(Document& doc, ls::OperationId op) {
    auto info = doc.engine().getOperationInfo(op);
    return info.ok() && info.value.type == "FillSolidOp";
}

// A run: a freehand element whose region is made of strokes.
bool isRun(Document& doc, const Element& element) {
    return element.kind == ElementKind::Paint && element.region.valid() &&
           readStrokes(doc, element.region, nullptr, nullptr);
}

// A new run on top of the layer, coloured by `fill` -- a solid colour or a
// copy of another element's rule -- over strokes with nothing in them yet.
bool addRun(Document& doc, ls::LayerId layer, ls::Operation fill, PaintLayer* out) {
    ls::LSContext& engine = doc.engine();
    const ls::RegionId region = createFreehandRegion(doc);
    if (!region.valid()) {
        return false;
    }
    if (auto* solid = std::get_if<ls::FillSolidOp>(&fill)) {
        solid->targetRegion = region;
    } else if (auto* dither = std::get_if<ls::FillDitherOp>(&fill)) {
        dither->targetRegion = region;
    } else {
        deleteRegionAndShapes(doc, region);
        return false;
    }
    auto op = engine.addOperation(layer, fill);
    if (op.fail()) {
        deleteRegionAndShapes(doc, region);
        return false;
    }
    // New paint is drawn before any transform, outline or shadow, so they see it.
    keepEffectsLast(doc, layer);
    out->layer = layer;
    out->region = region;
    out->fill = op.value;
    return true;
}

// What a path of centres draws, the brush stamped along it.
ls::IntervalSet stamped(const std::vector<ls::Vec2i>& centres, const PenBrush& brush) {
    return markPixels(pathMark(centres, brush));
}

std::vector<ls::Vec2i> listOf(const ls::IntervalSet& set) {
    std::vector<ls::Vec2i> out;
    for (const ls::Interval& run : set.intervals) {
        for (int32_t x = run.x0; x < run.x1; ++x) {
            out.push_back({ x, run.y });
        }
    }
    return out;
}

bool sameBrush(const PenBrush& a, const PenBrush& b) {
    return a.size == b.size && a.round == b.round && a.pixelPerfect == b.pixelPerfect;
}

// A line or a curve drawn as a path has no region to keep an erase: what the
// eraser takes from those goes into an erase of the layer's own, a clear laid
// over what is drawn before it -- kept as strokes, so it moves with them.
ls::RegionId pathShapesEraseOf(Document& doc, ls::LayerId layer) {
    const std::vector<Element> all = elementsOf(doc, layer);
    // The topmost erase, unless something was drawn after it: that one would
    // then be under the erase, and moving the erase above it would cut into
    // it whatever was erased before.
    if (!all.empty() && all.back().kind == ElementKind::Erase &&
        readStrokes(doc, all.back().region, nullptr, nullptr)) {
        return all.back().region;
    }
    ls::LSContext& engine = doc.engine();
    const ls::RegionId region = createFreehandRegion(doc);
    if (!region.valid()) {
        return ls::RegionId{};
    }
    ls::ClearRegionOp clear;
    clear.targetRegion = region;
    if (engine.addOperation(layer, clear).fail()) {
        deleteRegionAndShapes(doc, region);
        return ls::RegionId{};
    }
    keepEffectsLast(doc, layer);
    return region;
}

} // namespace

bool inkOfElement(Document& doc, ls::OperationId fill, Ink* out) {
    if (out == nullptr || !isSolid(doc, fill)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto colour = engine.getOperationParameter(fill, "fallbackColor");
    auto role = engine.getOperationParameter(fill, "paletteRole");
    if (colour.fail() || role.fail()) {
        return false;
    }
    const ls::Color* c = std::get_if<ls::Color>(&colour.value);
    const int64_t* r = std::get_if<int64_t>(&role.value);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    out->colour = *c;
    out->role = static_cast<ls::ColorRole>(*r);
    return true;
}

std::vector<ls::OperationId> elementsWithInk(Document& doc, ls::LayerId layer, const Ink& ink) {
    std::vector<ls::OperationId> out;
    for (const Element& element : elementsOf(doc, layer)) {
        Ink found;
        if (element.kind == ElementKind::Paint && inkOfElement(doc, element.fill, &found) &&
            found == ink) {
            out.push_back(element.fill);
        }
    }
    return out;
}

bool beginInkStroke(Document& doc, ls::LayerId layer, const Ink& ink, InkStroke* out) {
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = layer;
    const std::vector<Element> all = elementsOf(doc, layer);
    if (!all.empty()) {
        const Element& top = all.back();
        Ink found;
        if (isRun(doc, top) && inkOfElement(doc, top.fill, &found) && found == ink) {
            out->target.layer = layer;
            out->target.region = top.region;
            out->target.fill = top.fill;
            return true;
        }
    }
    ls::FillSolidOp fill;
    fill.fallbackColor = ink.colour;
    fill.paletteRole = ink.role;
    return addRun(doc, layer, fill, &out->target);
}

bool beginElementStroke(Document& doc, const PaintLayer& element, InkStroke* out) {
    if (out == nullptr || !element.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = element.layer;
    if (element.region.valid()) {
        const RegionMade made = regionMadeOf(doc, element.region);
        if (made == RegionMade::Strokes || made == RegionMade::Pixels) {
            out->target = element;
            return true;
        }
    }
    auto rule = doc.engine().getOperation(element.fill);
    return rule.ok() && addRun(doc, element.layer, rule.value, &out->target);
}

bool beginEraseStroke(Document& doc, ls::LayerId layer, InkStroke* out) {
    (void)doc;
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = layer;
    out->eraser = true;
    return true;
}

bool eraseFromLayer(Document& doc, ls::LayerId layer, const ls::PenStroke& eraser,
                    const ls::IntervalSet& pixels) {
    if (pixels.empty()) {
        return false;
    }
    bool any = false;
    bool pathShapeUnder = false;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind == ElementKind::Erase) {
            continue;
        }
        if (!element.region.valid()) {
            // A line or a curve: its pixels, to see whether the eraser is on it.
            pathShapeUnder = pathShapeUnder ||
                !ls::geom::intersectSets(elementCoverage(doc, element), pixels).empty();
            continue;
        }
        any = eraseFromRegion(doc, element.region, eraser, pixels) || any;
    }
    if (pathShapeUnder) {
        const ls::RegionId erase = pathShapesEraseOf(doc, layer);
        ls::GeometryId geometry;
        ls::StrokesDesc desc;
        if (erase.valid() && readStrokes(doc, erase, &geometry, &desc)) {
            ls::PenStroke rub = eraser;
            rub.erase = false;
            desc.strokes.push_back(std::move(rub));
            any = doc.engine().updateStrokes(geometry, desc).ok() || any;
        }
    }
    return any;
}

bool strokeAlong(Document& doc, InkStroke& stroke, int copy, const std::vector<ls::Vec2i>& centres,
                 const PenBrush& brush) {
    if (centres.empty()) {
        return false;
    }
    if (stroke.erasing()) {
        return eraseFromLayer(doc, stroke.layer, pathMark(centres, brush), stamped(centres, brush));
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        // A document's old pixels are still painted as pixels.
        return paintPixels(doc, stroke.target, listOf(stamped(centres, brush)));
    }
    InkStroke::Open& open = stroke.open[static_cast<size_t>(copy) & 3u];
    const ls::Vec2i first = centres.front();
    const bool carries = open.index >= 0 && open.index < static_cast<int>(desc.strokes.size()) &&
                         sameBrush(open.brush, brush) &&
                         std::max(std::abs(first.x - open.last.x), std::abs(first.y - open.last.y)) <= 1;
    if (carries) {
        std::vector<ls::Vec2f>& points = desc.strokes[static_cast<size_t>(open.index)].points;
        for (size_t i = 0; i < centres.size(); ++i) {
            if (i == 0 && centres[0].x == open.last.x && centres[0].y == open.last.y) {
                continue;                 // the pixel it stopped on
            }
            points.push_back({ static_cast<float>(centres[i].x) + 0.5f,
                               static_cast<float>(centres[i].y) + 0.5f });
        }
    } else {
        desc.strokes.push_back(pathMark(centres, brush));
        open.index = static_cast<int>(desc.strokes.size()) - 1;
        open.brush = brush;
    }
    open.last = centres.back();
    return doc.engine().updateStrokes(geometry, desc).ok();
}

bool sprayDots(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& dots) {
    if (dots.empty()) {
        return false;
    }
    if (stroke.erasing()) {
        ls::IntervalSet set;
        for (ls::Vec2i p : dots) {
            set.intervals.push_back({ p.y, p.x, p.x + 1 });
        }
        set = ls::geom::normalize(set);
        return eraseFromLayer(doc, stroke.layer, areaMark(set), set);
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        return paintPixels(doc, stroke.target, dots);
    }
    ls::PenStroke mark = pathMark(dots, PenBrush{});
    mark.kind = ls::PenKind::Dots;
    desc.strokes.push_back(std::move(mark));
    return doc.engine().updateStrokes(geometry, desc).ok();
}

void closePaths(InkStroke& stroke) {
    for (InkStroke::Open& open : stroke.open) {
        open.index = -1;
    }
}

bool strokeInk(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& pixels) {
    if (pixels.empty()) {
        return false;
    }
    ls::IntervalSet set;
    for (ls::Vec2i p : pixels) {
        set.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    set = ls::geom::normalize(set);
    if (stroke.erasing()) {
        return eraseFromLayer(doc, stroke.layer, areaMark(set), set);
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        return paintPixels(doc, stroke.target, pixels);
    }
    // An area laid right after another grows it, rather than stacking up one
    // mark for every stamp of a drag.
    if (!desc.strokes.empty() && desc.strokes.back().kind == ls::PenKind::Area &&
        !desc.strokes.back().erase) {
        const ls::IntervalSet grown = ls::geom::unionSets(
            ls::geom::rasterizeAreaDesc(desc.strokes.back().area), set);
        desc.strokes.back().area = ls::geom::traceArea(grown);
    } else {
        desc.strokes.push_back(areaMark(set));
    }
    return doc.engine().updateStrokes(geometry, desc).ok();
}

bool setElementInk(Document& doc, ls::OperationId fill, const Ink& ink) {
    if (!isSolid(doc, fill)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    const bool colour = engine.setOperationParameter(fill, "fallbackColor",
                                                     ls::ParameterValue{ink.colour}).ok();
    const bool role = engine.setOperationParameter(
        fill, "paletteRole", ls::ParameterValue{static_cast<int64_t>(ink.role)}).ok();
    return colour && role;
}

int pruneEmptyInks(Document& doc, ls::LayerId layer, ls::OperationId keep) {
    ls::LSContext& engine = doc.engine();
    int removed = 0;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.fill == keep || !element.region.valid()) {
            continue;
        }
        const bool erase = element.kind == ElementKind::Erase;
        const bool freehand = element.kind == ElementKind::Paint || element.kind == ElementKind::Fill;
        if (!erase && !(freehand && isSolid(doc, element.fill))) {
            continue;
        }
        auto intervals = engine.getRegionIntervals(element.region);
        if (intervals.fail() || !intervals.value.empty()) {
            continue;
        }
        if (!erase && elementsOf(doc, layer).size() <= 1) {
            break;
        }
        if (engine.removeOperation(layer, element.fill).ok()) {
            deleteRegionAndShapes(doc, element.region);
            ++removed;
        }
    }
    return removed;
}

bool beginInkMode(Document& doc, ls::LayerId layer, InkMode mode, const Ink& second,
                  const std::vector<std::pair<ls::ColorRole, ls::Color>>& palette,
                  InkModeState* out, ls::OperationId own, const ls::IntervalSet* selection) {
    if (out == nullptr) {
        return false;
    }
    *out = InkModeState{};
    out->mode = mode;
    out->layer = layer;
    out->second = second;
    if (selection != nullptr && !selection->empty()) {
        out->selected = true;
        out->selection = ls::geom::traceArea(*selection);
    }
    if (mode == InkMode::Simple) {
        return true;
    }
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.fill == own) {
            continue;
        }
        InkModeState::Under under;
        under.region = element.region;
        under.shape = element.region.valid() ? ls::GeometryId{} : element.geometry;
        if (!under.region.valid() && !under.shape.valid()) {
            continue;
        }
        under.erase = element.kind == ElementKind::Erase;
        under.solid = (element.kind == ElementKind::Paint || element.kind == ElementKind::Fill) &&
                      inkOfElement(doc, element.fill, &under.ink);
        under.pixels = elementCoverage(doc, element);
        out->under.push_back(std::move(under));
    }
    for (const auto& [role, colour] : palette) {
        out->ramp.push_back(role);
        out->rampColours.push_back(colour);
    }
    return true;
}

namespace {

// A term of a clip for something the layer held.
ls::RegionClipTerm termOf(const InkModeState::Under& under, ls::ClipOp op) {
    ls::RegionClipTerm term;
    term.region = under.region;
    term.geometry = under.shape;
    term.op = op;
    return term;
}

// Where `showing` is what shows, from what the layer held: added where it
// draws, taken away where anything drawn over it does. False when it shows
// nowhere.
bool clipWhereShowing(const InkModeState& state,
                      const std::function<bool(const InkModeState::Under&)>& showing,
                      std::vector<ls::RegionClipTerm>* out) {
    bool any = false;
    for (const InkModeState::Under& under : state.under) {
        if (!under.erase && showing(under)) {
            out->push_back(termOf(under, ls::ClipOp::Add));
            any = true;
        } else if (any) {
            out->push_back(termOf(under, ls::ClipOp::Remove));   // before the first, nothing to take
        }
    }
    return any;
}

// Whether a run's clip is `terms`, then inside `within` when that is given.
bool sameClip(Document& doc, const std::vector<ls::RegionClipTerm>& current,
              const std::vector<ls::RegionClipTerm>& terms, const ls::AreaDesc* within) {
    if (current.size() != terms.size() + (within != nullptr ? 1u : 0u) ||
        !std::equal(terms.begin(), terms.end(), current.begin())) {
        return false;
    }
    if (within == nullptr) {
        return true;
    }
    auto area = doc.engine().getArea(current.back().geometry);
    if (current.back().op != ls::ClipOp::Within || area.fail() ||
        area.value.contours.size() != within->contours.size()) {
        return false;
    }
    for (size_t c = 0; c < within->contours.size(); ++c) {
        const std::vector<ls::Vec2f>& a = area.value.contours[c];
        const std::vector<ls::Vec2f>& b = within->contours[c];
        if (a.size() != b.size() ||
            !std::equal(a.begin(), a.end(), b.begin(), [](ls::Vec2f p, ls::Vec2f q) {
                return p.x == q.x && p.y == q.y;
            })) {
            return false;
        }
    }
    return true;
}

// `terms`, then a Within term for a copy of `within` when that is given.
bool withSelection(Document& doc, std::vector<ls::RegionClipTerm>* terms,
                   const ls::AreaDesc* within) {
    if (within == nullptr) {
        return true;
    }
    auto area = doc.engine().createArea(doc.id(), *within);
    if (area.fail()) {
        return false;
    }
    ls::RegionClipTerm term;
    term.geometry = area.value;
    term.op = ls::ClipOp::Within;
    terms->push_back(term);
    return true;
}

// `stroke`'s run clipped to `terms` and `within`: the run itself when it is
// already, or has nothing in it, and otherwise a new run of its colour on top.
bool clipRun(Document& doc, InkStroke& stroke, std::vector<ls::RegionClipTerm> terms,
             const ls::AreaDesc* within) {
    ls::LSContext& engine = doc.engine();
    auto current = engine.getRegionClip(stroke.target.region);
    if (current.ok() && sameClip(doc, current.value, terms, within)) {
        return true;
    }
    ls::StrokesDesc marks;
    const bool empty = current.ok() && current.value.empty() &&
                       readStrokes(doc, stroke.target.region, nullptr, &marks) &&
                       marks.strokes.empty();
    if (!empty) {
        auto rule = engine.getOperation(stroke.target.fill);
        if (rule.fail() || !addRun(doc, stroke.layer, rule.value, &stroke.target)) {
            return false;
        }
    }
    for (InkStroke::Open& open : stroke.open) {
        open.index = -1;
    }
    return withSelection(doc, &terms, within) &&
           engine.setRegionClip(stroke.target.region, terms).ok();
}

// The whole of a piece of a stroke, laid into `stroke`.
bool layPiece(Document& doc, InkStroke& stroke, const InkModeState::Piece& piece) {
    switch (piece.kind) {
        case InkModeState::Piece::Kind::Path:
            return strokeAlong(doc, stroke, piece.copy, piece.points, piece.brush);
        case InkModeState::Piece::Kind::Dots:
            return sprayDots(doc, stroke, piece.points);
        case InkModeState::Piece::Kind::Area:
            return strokeInk(doc, stroke, piece.points);
    }
    return false;
}

} // namespace

bool keepStrokeToMode(Document& doc, InkModeState& state, InkStroke& stroke) {
    if (stroke.erasing() || state.mode == InkMode::Shading) {
        return true;
    }
    std::vector<ls::RegionClipTerm> clip;
    if (state.mode == InkMode::LockAlpha) {
        // Wherever anything on the layer draws -- shapes too, since a shape's
        // pixels are the layer's pixels to the eye -- less what is erased.
        bool any = false;
        for (const InkModeState::Under& under : state.under) {
            if (under.erase) {
                if (any) {
                    clip.push_back(termOf(under, ls::ClipOp::Remove));
                }
            } else {
                clip.push_back(termOf(under, ls::ClipOp::Add));
                any = true;
            }
        }
        state.nothing = !any;
    } else if (state.mode == InkMode::Replace) {
        const Ink second = state.second;
        state.nothing = !clipWhereShowing(state, [&second](const InkModeState::Under& under) {
            return under.solid && under.ink == second;
        }, &clip);
    }
    if (state.nothing || (clip.empty() && !state.selected)) {
        return true;
    }
    return clipRun(doc, stroke, clip, state.selected ? &state.selection : nullptr);
}

bool shadeMark(Document& doc, InkModeState& state, const InkModeState::Piece& piece) {
    // Which slots the piece passes over, as they showed when the stroke began:
    // the topmost of what the layer held, at each pixel it covers.
    std::vector<ls::Vec2i> covers = piece.points;
    if (piece.kind == InkModeState::Piece::Kind::Path) {
        covers.clear();
        for (const ls::Interval& run : markPixels(pathMark(piece.points, piece.brush)).intervals) {
            for (int32_t x = run.x0; x < run.x1; ++x) {
                covers.push_back({ x, run.y });
            }
        }
    }
    std::set<ls::ColorRole> passed;
    for (ls::Vec2i p : covers) {
        const InkModeState::Under* top = nullptr;
        for (const InkModeState::Under& under : state.under) {
            if (ls::geom::contains(under.pixels, p)) {
                top = &under;
            }
        }
        if (top != nullptr && !top->erase && top->solid && top->ink.usesSlot()) {
            passed.insert(top->ink.role);
        }
    }
    bool ok = true;
    for (ls::ColorRole from : passed) {
        if (state.strokes.count(from) != 0) {
            continue;
        }
        const auto at = std::find(state.ramp.begin(), state.ramp.end(), from);
        if (at == state.ramp.end()) {
            continue;
        }
        const long long index = at - state.ramp.begin();
        const long long next = state.forward ? index + 1 : index - 1;
        if (next < 0 || next >= static_cast<long long>(state.ramp.size())) {
            continue;                   // already at the end of the ramp
        }
        // A run of the next slot, clipped to where this one showed, and
        // given what the stroke has laid so far.
        ls::FillSolidOp fill;
        fill.paletteRole = state.ramp[static_cast<size_t>(next)];
        fill.fallbackColor = state.rampColours[static_cast<size_t>(next)];
        InkStroke made;
        made.layer = state.layer;
        std::vector<ls::RegionClipTerm> clip;
        if (!addRun(doc, state.layer, fill, &made.target) ||
            !clipWhereShowing(state, [from](const InkModeState::Under& under) {
                return under.solid && under.ink.usesSlot() && under.ink.role == from;
            }, &clip)) {
            ok = false;
            continue;
        }
        if (!withSelection(doc, &clip, state.selected ? &state.selection : nullptr) ||
            doc.engine().setRegionClip(made.target.region, clip).fail()) {
            ok = false;
            continue;
        }
        for (const InkModeState::Piece& before : state.pieces) {
            layPiece(doc, made, before);
        }
        state.strokes.emplace(from, made);
    }
    state.pieces.push_back(piece);
    for (auto& [from, stroke] : state.strokes) {
        ok = layPiece(doc, stroke, piece) && ok;
    }
    return ok;
}

bool strokeInkMode(Document& doc, InkModeState& state, const InkStroke& stroke,
                   const std::vector<ls::Vec2i>& pixels) {
    if (state.nothing) {
        return true;
    }
    if (state.mode == InkMode::Shading) {
        InkModeState::Piece piece;
        piece.kind = InkModeState::Piece::Kind::Area;
        piece.points = pixels;
        return shadeMark(doc, state, piece);
    }
    return strokeInk(doc, stroke, pixels);
}

bool inkAt(Document& doc, ls::SpriteId sprite, ls::Vec2i pixel, Ink* out) {
    if (out == nullptr) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto info = engine.getSpriteInfo(sprite);
    if (info.fail()) {
        return false;
    }
    // Topmost first: the last layer in the list draws over the rest.
    for (size_t i = info.value.layers.size(); i-- > 0;) {
        const ls::LayerId layer = info.value.layers[i];
        auto layerInfo = engine.getLayerInfo(layer);
        if (layerInfo.fail() || !layerInfo.value.visible) {
            continue;
        }
        if (layerInfo.value.parentId.valid()) {
            auto group = engine.getGroupInfo(layerInfo.value.parentId);
            if (group.ok() && !group.value.visible) {
                continue;
            }
        }
        ls::Vec2f mapped;
        if (!mapCanvasPointToLayer(doc, layer,
                                   { static_cast<float>(pixel.x), static_cast<float>(pixel.y) },
                                   &mapped)) {
            continue;
        }
        const ls::Vec2i local { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
                                static_cast<int32_t>(std::floor(mapped.y + 0.5f)) };

        // Topmost element first, for the same reason.
        const std::vector<Element> elements = elementsOf(doc, layer);
        for (size_t e = elements.size(); e-- > 0;) {
            const Element& element = elements[e];
            if (!element.region.valid()) {
                continue;
            }
            auto inside = engine.regionContainsPoint(element.region, local);
            if (inside.fail() || !inside.value) {
                continue;
            }
            if (element.kind == ElementKind::Erase) {
                return false;              // rubbed out: nothing there to pick
            }
            // Whatever covers the pixel answers, even a shape or a dither: a
            // pixel under a rectangle is the rectangle's colour, not the ink
            // hidden beneath it. Only a solid fill has an ink to hand back.
            return inkOfElement(doc, element.fill, out);
        }
    }
    return false;
}

} // namespace fast
