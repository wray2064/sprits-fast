// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/adjust.h"

#include "app/dither.h"
#include "app/palette_tools.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fast {

namespace {

uint8_t byte(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

int64_t roleOf(Document& doc, ls::OperationId op, const char* name) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return static_cast<int64_t>(ls::kColorRoleNone);
    }
    if (const int64_t* role = std::get_if<int64_t>(&value.value)) {
        return *role;
    }
    return static_cast<int64_t>(ls::kColorRoleNone);
}

bool isOwn(int64_t role) {
    return role == static_cast<int64_t>(ls::kColorRoleNone);
}

} // namespace

ls::Color adjustColour(ls::Color colour, const ColourAdjust& a) {
    if (a.identity()) {
        return colour;
    }
    float h = 0.f;
    float s = 0.f;
    float l = 0.f;
    rgbToHsl(colour, &h, &s, &l);
    // Saturation and lightness move toward their ends in proportion to the
    // room left, as the palette's adjustment does.
    s = a.saturation >= 0.f ? s + (1.f - s) * a.saturation : s * (1.f + a.saturation);
    l = a.lightness >= 0.f ? l + (1.f - l) * a.lightness : l * (1.f + a.lightness);
    ls::Color out = hslToRgb(h + a.hue, std::clamp(s, 0.f, 1.f), std::clamp(l, 0.f, 1.f),
                             colour.a);
    float r = out.r;
    float g = out.g;
    float b = out.b;
    const float lift = a.brightness * 255.f;
    // Contrast about the middle: -1 is flat grey, +1 very nearly two levels.
    const float factor = a.contrast >= 0.f ? 1.f + a.contrast * 4.f : 1.f + a.contrast;
    const auto channel = [&](float v) {
        v += lift;
        v = (v - 127.5f) * factor + 127.5f;
        return a.invert ? 255.f - std::clamp(v, 0.f, 255.f) : v;
    };
    out.r = byte(channel(r));
    out.g = byte(channel(g));
    out.b = byte(channel(b));
    return out;
}

AdjustBase adjustBase(Document& doc, const std::vector<ls::LayerId>& layers, bool includeSlots) {
    AdjustBase base;
    ls::LSContext& engine = doc.engine();
    std::set<ls::ColorRole> roles;
    for (ls::LayerId layer : layers) {
        auto operations = engine.getLayerOperations(layer);
        if (operations.fail()) {
            continue;
        }
        for (const ls::OperationInfo& op : operations.value) {
            if (op.type == "FillDitherOp") {
                PaintLayer element;
                element.layer = layer;
                element.fill = op.id;
                auto region = engine.getOperationParameter(op.id, "targetRegion");
                if (region.ok()) {
                    if (const uint64_t* handle = std::get_if<uint64_t>(&region.value)) {
                        element.region.value = *handle;
                    }
                }
                DitherSettings settings;
                if (!readDitherSettings(doc, element, &settings)) {
                    continue;
                }
                AdjustBase::Site site;
                site.layer = layer;
                site.op = op.id;
                site.region = element.region;
                site.dither = true;
                site.colour = settings.from;
                site.second = settings.to;
                site.firstOwn = settings.fromRole == ls::kColorRoleNone;
                site.secondOwn = settings.toRole == ls::kColorRoleNone;
                if (!site.firstOwn) { roles.insert(settings.fromRole); }
                if (!site.secondOwn) { roles.insert(settings.toRole); }
                base.sites.push_back(site);
                continue;
            }
            // Any other operation: its colour parameters, where no slot
            // stands in for them.
            auto params = engine.describeOperation(op.id);
            if (params.fail()) {
                continue;
            }
            const int64_t role = roleOf(doc, op.id, "paletteRole");
            if (!isOwn(role)) {
                roles.insert(static_cast<ls::ColorRole>(role));
                continue;
            }
            for (const ls::ParameterInfo& param : params.value) {
                if (param.type != ls::ParameterType::Color) {
                    continue;
                }
                auto value = engine.getOperationParameter(op.id, param.name);
                const ls::Color* colour =
                    value.ok() ? std::get_if<ls::Color>(&value.value) : nullptr;
                if (colour == nullptr) {
                    continue;
                }
                AdjustBase::Site site;
                site.layer = layer;
                site.op = op.id;
                site.param = param.name;
                site.colour = *colour;
                base.sites.push_back(site);
            }
        }
    }
    if (includeSlots && !roles.empty() && !layers.empty()) {
        base.palette = paletteOfLayer(doc, layers.front());
        for (const PaletteEntry& entry : paletteEntries(doc, base.palette)) {
            if (roles.count(entry.role) != 0) {
                base.slots.push_back(entry);
            }
        }
    }
    return base;
}

bool applyAdjust(Document& doc, const AdjustBase& base, const ColourAdjust& adjust) {
    bool ok = true;
    ls::LSContext& engine = doc.engine();
    for (const AdjustBase::Site& site : base.sites) {
        if (site.dither) {
            PaintLayer element;
            element.layer = site.layer;
            element.fill = site.op;
            element.region = site.region;
            DitherSettings settings;
            if (!readDitherSettings(doc, element, &settings)) {
                ok = false;
                continue;
            }
            if (site.firstOwn) { settings.from = adjustColour(site.colour, adjust); }
            if (site.secondOwn) { settings.to = adjustColour(site.second, adjust); }
            ok = applyDitherSettings(doc, element, settings) && ok;
            continue;
        }
        ok = engine.setOperationParameter(site.op, site.param,
                                          ls::ParameterValue{ adjustColour(site.colour, adjust) })
                 .ok() && ok;
    }
    for (const PaletteEntry& entry : base.slots) {
        ok = setPaletteEntry(doc, base.palette, entry.role, adjustColour(entry.color, adjust)) && ok;
    }
    return ok;
}

} // namespace fast
