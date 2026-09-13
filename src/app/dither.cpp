// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/dither.h"

#include "app/palette.h"

namespace fast {
namespace {

// In enum order. The engine's headers are the authority on that order; these are
// only the words for it.
const std::vector<const char*> kPatternNames {
    "Bayer 2x2", "Bayer 4x4", "Bayer 8x8", "Checker",
    "Horizontal lines", "Vertical lines", "Diagonal lines", "Cross hatch",
    "Dots", "Clustered dot", "Noise", "Grid",
};

const std::vector<const char*> kModulationNames {
    "Constant", "Linear", "Radial", "Angular",
};

const std::vector<const char*> kAnchorNames {
    "Local", "Global", "Fixed",
};

// Where a layer's fill sits in its operation list, so a replacement goes back in
// the same place. A fill that moved to the end would resolve after the
// transforms, which is a different picture entirely.
int32_t indexOfOperation(Document& doc, ls::LayerId layer, ls::OperationId op) {
    auto operations = doc.engine().getLayerOperations(layer);
    if (operations.fail()) {
        return -1;
    }
    for (size_t i = 0; i < operations.value.size(); ++i) {
        if (operations.value[i].id == op) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

template <typename T>
T param(Document& doc, ls::OperationId op, const char* name, T fallback) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return fallback;
    }
    if (const T* found = std::get_if<T>(&value.value)) {
        return *found;
    }
    return fallback;
}

int64_t enumParam(Document& doc, ls::OperationId op, const char* name, int64_t fallback) {
    return param<int64_t>(doc, op, name, fallback);
}

// Which built-in kind a layer's pattern is, read back from the resource itself.
//
// The engine keeps the kind's name on the tile it bakes, and nothing else: a
// pattern is a threshold tile, and which recipe made it is not something the
// compile needs. The interface does need it -- a combo that cannot say what is
// there shows its default instead, and the first edit to any other control
// writes that default back over the real one. That is what happened: every
// density drag turned a checker into Bayer 4x4.
bool patternKindOf(Document& doc, ls::OperationId fill, ls::DitherPatternKind* out) {
    const auto id = param<uint64_t>(doc, fill, "pattern", 0);
    if (id == 0) {
        return false;
    }
    ls::PatternId pattern;
    pattern.value = id;
    auto desc = doc.engine().getPattern(pattern);
    if (desc.fail()) {
        return false;
    }
    for (ls::DitherPatternKind kind : doc.engine().ditherPatternKinds()) {
        if (desc.value.name == doc.engine().ditherPatternName(kind)) {
            *out = kind;
            return true;
        }
    }
    return false;                  // a tile from elsewhere; not one of ours
}

} // namespace

const std::vector<const char*>& ditherPatternNames()    { return kPatternNames; }
const std::vector<const char*>& ditherModulationNames() { return kModulationNames; }
const std::vector<const char*>& patternAnchorNames()    { return kAnchorNames; }

bool layerIsDithered(Document& doc, const PaintLayer& layer) {
    if (!layer.valid()) {
        return false;
    }
    auto info = doc.engine().getOperationInfo(layer.fill);
    return info.ok() && info.value.type == "FillDitherOp";
}

namespace {

// The two-stop ramp a DitherSettings describes. In one place, so creating and
// updating cannot disagree about which end carries which role.
ls::RampDesc rampOf(const DitherSettings& settings) {
    ls::RampDesc desc;
    desc.name = "dither";
    desc.interpolate = true;
    ls::RampStop from;
    from.position = 0.f;
    from.color = settings.from;
    from.role = settings.fromRole;
    ls::RampStop to;
    to.position = 1.f;
    to.color = settings.to;
    to.role = settings.toRole;
    desc.stops = { from, to };
    return desc;
}

} // namespace

bool setLayerDithered(Document& doc, PaintLayer& layer, const DitherSettings& settings) {
    if (!layer.valid()) {
        return false;
    }
    if (layerIsDithered(doc, layer)) {
        return applyDitherSettings(doc, layer, settings);
    }

    const int32_t at = indexOfOperation(doc, layer.layer, layer.fill);
    if (at < 0) {
        return false;
    }

    // The ramp and the pattern are document resources, made once and pointed at.
    auto ramp = doc.engine().createRamp(doc.id(), rampOf(settings));
    if (ramp.fail()) {
        return false;
    }
    auto pattern = doc.engine().createDitherPattern(doc.id(), settings.pattern);
    if (pattern.fail()) {
        return false;
    }

    ls::FillDitherOp op;
    op.targetRegion = layer.region;     // the drawing itself is untouched
    op.ramp = ramp.value;
    op.pattern = pattern.value;
    op.density = settings.density;
    op.modulation = settings.modulation;
    op.gradientStart = settings.gradientStart;
    op.gradientEnd = settings.gradientEnd;
    op.anchor = settings.anchor;

    // In at the old fill's position, then the old one out. Done in this order so
    // a failure to add leaves the layer with the fill it had.
    auto added = doc.engine().addOperation(layer.layer, op, at);
    if (added.fail()) {
        return false;
    }
    doc.engine().removeOperation(layer.layer, layer.fill);
    layer.fill = added.value;
    return true;
}

bool setLayerSolid(Document& doc, PaintLayer& layer, ls::Color colour) {
    if (!layer.valid()) {
        return false;
    }
    if (!layerIsDithered(doc, layer)) {
        return setPaintColor(doc, layer, colour);
    }

    const int32_t at = indexOfOperation(doc, layer.layer, layer.fill);
    if (at < 0) {
        return false;
    }

    ls::FillSolidOp op;
    op.targetRegion = layer.region;
    op.fallbackColor = colour;

    auto added = doc.engine().addOperation(layer.layer, op, at);
    if (added.fail()) {
        return false;
    }
    doc.engine().removeOperation(layer.layer, layer.fill);
    layer.fill = added.value;
    return true;
}

bool readDitherSettings(Document& doc, const PaintLayer& layer, DitherSettings* out) {
    if (out == nullptr || !layerIsDithered(doc, layer)) {
        return false;
    }
    DitherSettings settings;
    settings.density = param<float>(doc, layer.fill, "density", 0.5f);
    settings.modulation = static_cast<ls::DitherModulation>(
        enumParam(doc, layer.fill, "modulation", 0));
    settings.anchor = static_cast<ls::PatternAnchor>(
        enumParam(doc, layer.fill, "anchor", 0));
    settings.gradientStart = param<ls::Vec2f>(doc, layer.fill, "gradientStart", {0.f, 0.f});
    settings.gradientEnd = param<ls::Vec2f>(doc, layer.fill, "gradientEnd", {16.f, 16.f});
    patternKindOf(doc, layer.fill, &settings.pattern);

    // The ramp's ends are on the ramp, not the operation, so they are read from
    // there -- as stops, not by sampling, because a sample is a resolved colour
    // and cannot say which role produced it. An end that names a slot reports
    // the slot's colour, which is what the compile draws: a swatch showing the
    // stop's literal would sit beside a canvas that disagrees with it after
    // every palette change, and detaching the end would then snap the picture
    // to a colour nobody had seen since the slot was assigned.
    const auto rampId = param<uint64_t>(doc, layer.fill, "ramp", 0);
    if (rampId != 0) {
        ls::RampId ramp;
        ramp.value = rampId;
        auto desc = doc.engine().getRamp(ramp);
        if (desc.ok() && desc.value.stops.size() >= 2) {
            const ls::RampStop& first = desc.value.stops.front();
            const ls::RampStop& last  = desc.value.stops.back();
            settings.from = first.color;
            settings.fromRole = first.role;
            settings.to = last.color;
            settings.toRole = last.role;
            const ls::PaletteId palette = paletteOfLayer(doc, layer.layer);
            resolvePaletteRole(doc, palette, settings.fromRole, &settings.from);
            resolvePaletteRole(doc, palette, settings.toRole, &settings.to);
        }
    }

    *out = settings;
    return true;
}

bool applyDitherSettings(Document& doc, const PaintLayer& layer,
                         const DitherSettings& settings) {
    if (!layerIsDithered(doc, layer)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();

    bool ok = true;
    ok = engine.setOperationParameter(layer.fill, "density",
                                      ls::ParameterValue{settings.density}).ok() && ok;
    ok = engine.setOperationParameter(layer.fill, "modulation",
             ls::ParameterValue{static_cast<int64_t>(settings.modulation)}).ok() && ok;
    ok = engine.setOperationParameter(layer.fill, "anchor",
             ls::ParameterValue{static_cast<int64_t>(settings.anchor)}).ok() && ok;
    ok = engine.setOperationParameter(layer.fill, "gradientStart",
             ls::ParameterValue{settings.gradientStart}).ok() && ok;
    ok = engine.setOperationParameter(layer.fill, "gradientEnd",
             ls::ParameterValue{settings.gradientEnd}).ok() && ok;

    // The ramp is shared document state rather than a parameter, so its stops
    // are updated in place. Changing a colour must not build a second ramp every
    // time the picker moves.
    const auto rampId = param<uint64_t>(doc, layer.fill, "ramp", 0);
    if (rampId != 0) {
        ls::RampId ramp;
        ramp.value = rampId;
        ok = engine.updateRamp(ramp, rampOf(settings)).ok() && ok;
    }

    // The pattern kind is not a parameter either: a different kind is a
    // different resource, made once and pointed at. Only when it actually
    // differs -- a resource made on every call would leave one behind per
    // slider tick, and a density drag was leaving a couple of hundred of them
    // in the file.
    ls::DitherPatternKind current;
    const bool known = patternKindOf(doc, layer.fill, &current);
    if (!known || current != settings.pattern) {
        auto pattern = engine.createDitherPattern(doc.id(), settings.pattern);
        if (pattern.ok()) {
            ok = engine.setOperationParameter(layer.fill, "pattern",
                     ls::ParameterValue{pattern.value.value}).ok() && ok;
        }
    }
    return ok;
}

} // namespace fast
