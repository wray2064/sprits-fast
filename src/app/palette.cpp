// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/palette.h"
#include "app/dither.h"

namespace fast {
namespace {

// A starter palette: sixteen colours that can build a readable sprite without
// anyone choosing anything first. A neutral ramp to sit things on, warm and cool
// ranges for form, and four accents. Deliberately balanced in lightness rather
// than in hue, because value is what makes pixel art read.
const ls::Color kStarter[] = {
    {  18,  20,  28, 255 },   // near-black
    {  46,  50,  64, 255 },
    {  86,  92, 110, 255 },
    { 138, 146, 164, 255 },
    { 200, 206, 218, 255 },
    { 246, 248, 252, 255 },   // near-white

    { 112,  54,  48, 255 },   // warm: shadow to highlight
    { 178,  84,  60, 255 },
    { 226, 143,  65, 255 },
    { 244, 202, 122, 255 },

    {  34,  70,  96, 255 },   // cool: shadow to highlight
    {  52, 118, 146, 255 },
    {  92, 176, 190, 255 },

    { 104, 154,  84, 255 },   // accents
    { 168,  86, 132, 255 },
    { 206,  88,  76, 255 },
};

ls::PaletteId paletteOf(Document& doc, ls::SpriteId sprite) {
    auto bound = doc.engine().getEffectivePalette(sprite);
    return bound.ok() ? bound.value : ls::PaletteId{};
}

// The document's palette. Older files bound only the first sprite and never
// the document; for those the first sprite's is the answer, and ensurePalette
// puts the binding where it belongs the first time it runs.
ls::PaletteId paletteOf(Document& doc) {
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return ls::PaletteId{};
    }
    if (info.value.palette.valid()) {
        return info.value.palette;
    }
    if (info.value.sprites.empty()) {
        return ls::PaletteId{};
    }
    return paletteOf(doc, info.value.sprites.front());
}

ls::PaletteDesc starterDesc(const std::string& name) {
    ls::PaletteDesc desc;
    desc.name = name;
    for (size_t i = 0; i < sizeof(kStarter) / sizeof(kStarter[0]); ++i) {
        ls::PaletteColorEntry entry;
        entry.role = static_cast<ls::ColorRole>(i);
        entry.color = kStarter[i];
        desc.entries.push_back(entry);
    }
    return desc;
}

uint64_t roleParam(Document& doc, ls::OperationId op) {
    auto value = doc.engine().getOperationParameter(op, "paletteRole");
    if (value.fail()) {
        return ls::kColorRoleNone;
    }
    if (const int64_t* found = std::get_if<int64_t>(&value.value)) {
        return static_cast<uint64_t>(*found);
    }
    return ls::kColorRoleNone;
}

} // namespace

bool ensurePalette(Document& doc, ls::SpriteId sprite) {
    ls::LSContext& engine = doc.engine();
    ls::PaletteId palette = paletteOf(doc);
    if (!palette.valid()) {
        auto created = engine.createPalette(doc.id(), starterDesc("palette"));
        if (created.fail()) {
            return false;
        }
        palette = created.value;
    }
    auto info = engine.getDocumentInfo(doc.id());
    if (info.ok() && info.value.palette != palette) {
        // Bound to the document, so a frame added later inherits it and a
        // swap reaches every frame at once.
        if (engine.bindDocumentPalette(doc.id(), palette).fail()) {
            return false;
        }
    }
    // A frame bound by name to the document's own palette -- which is how
    // every file before this was written -- would sit out a swap. The binding
    // says nothing the document's does not, so it goes.
    auto own = engine.getSpritePalette(sprite);
    if (own.ok() && own.value.valid() && own.value == palette) {
        engine.bindSpritePalette(sprite, ls::PaletteId{});
    }
    return true;
}

std::vector<PaletteInfo> listPalettes(Document& doc) {
    std::vector<PaletteInfo> out;
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return out;
    }
    for (ls::PaletteId id : info.value.palettes) {
        PaletteInfo entry;
        entry.id = id;
        auto name = doc.engine().getPaletteName(id);
        if (name.ok()) {
            entry.name = name.value;
        }
        auto entries = doc.engine().getPaletteEntries(id);
        if (entries.ok()) {
            entry.colours = entries.value.size();
        }
        out.push_back(std::move(entry));
    }
    return out;
}

ls::PaletteId documentPalette(Document& doc) {
    return paletteOf(doc);
}

ls::PaletteId paletteFor(Document& doc, ls::SpriteId sprite) {
    const ls::PaletteId own = paletteOf(doc, sprite);
    return own.valid() ? own : paletteOf(doc);
}

ls::PaletteId paletteOfLayer(Document& doc, ls::LayerId layer) {
    auto info = doc.engine().getLayerInfo(layer);
    if (info.fail()) {
        return paletteOf(doc);
    }
    return paletteFor(doc, info.value.sprite);
}

bool usePalette(Document& doc, ls::PaletteId palette) {
    if (!palette.valid()) {
        return false;
    }
    return doc.engine().bindDocumentPalette(doc.id(), palette).ok();
}

ls::PaletteId nextPalette(Document& doc, ls::PaletteId current) {
    const std::vector<PaletteInfo> all = listPalettes(doc);
    if (all.empty()) {
        return ls::PaletteId{};
    }
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].id == current) {
            return all[(i + 1) % all.size()].id;
        }
    }
    return all.front().id;
}

ls::PaletteId addPalette(Document& doc, const std::string& name, ls::PaletteId copyOf) {
    ls::PaletteDesc desc = starterDesc(name);
    if (copyOf.valid()) {
        auto entries = doc.engine().getPaletteEntries(copyOf);
        if (entries.fail()) {
            return ls::PaletteId{};
        }
        desc.entries = entries.value;
    }
    auto created = doc.engine().createPalette(doc.id(), desc);
    return created.ok() ? created.value : ls::PaletteId{};
}

bool renamePalette(Document& doc, ls::PaletteId palette, const std::string& name) {
    return doc.engine().setPaletteName(palette, name).ok();
}

bool deletePalette(Document& doc, ls::PaletteId palette) {
    if (listPalettes(doc).size() <= 1) {
        return false;
    }
    return doc.engine().deletePalette(palette).ok();
}

ls::PaletteId frameBinding(Document& doc, ls::SpriteId sprite) {
    auto own = doc.engine().getSpritePalette(sprite);
    return own.ok() ? own.value : ls::PaletteId{};
}

bool bindFrame(Document& doc, ls::SpriteId sprite, ls::PaletteId palette) {
    return doc.engine().bindSpritePalette(sprite, palette).ok();
}

std::vector<PaletteEntry> paletteEntries(Document& doc) {
    return paletteEntries(doc, paletteOf(doc));
}

std::vector<PaletteEntry> paletteEntries(Document& doc, ls::PaletteId palette) {
    std::vector<PaletteEntry> out;
    if (!palette.valid()) {
        return out;
    }
    auto entries = doc.engine().getPaletteEntries(palette);
    if (entries.fail()) {
        return out;
    }
    out.reserve(entries.value.size());
    for (const ls::PaletteColorEntry& entry : entries.value) {
        out.push_back({ entry.role, entry.color, entry.label });
    }
    return out;
}

bool removePaletteEntry(Document& doc, ls::ColorRole role) {
    return removePaletteEntry(doc, paletteOf(doc), role);
}

bool removePaletteEntry(Document& doc, ls::PaletteId palette, ls::ColorRole role) {
    if (!palette.valid()) {
        return false;
    }
    return doc.engine().removePaletteColor(palette, role).ok();
}

bool paletteRoleInUse(Document& doc, ls::ColorRole role) {
    auto used = doc.engine().usesPaletteRole(doc.id(), role);
    return used.ok() && used.value;
}

bool setPaletteLabel(Document& doc, ls::ColorRole role, const std::string& label) {
    return setPaletteLabel(doc, paletteOf(doc), role, label);
}

bool setPaletteLabel(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                     const std::string& label) {
    if (!palette.valid()) {
        return false;
    }
    return doc.engine().setPaletteLabel(palette, role, label).ok();
}

bool setPaletteEntry(Document& doc, ls::ColorRole role, ls::Color color) {
    return setPaletteEntry(doc, paletteOf(doc), role, color);
}

bool setPaletteEntry(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                     ls::Color color) {
    if (!palette.valid()) {
        return false;
    }
    // One call. Every layer painting through this role recolours on the next
    // compile, from the drawing rather than over it.
    return doc.engine().setPaletteColor(palette, role, color).ok();
}

ls::ColorRole addPaletteEntry(Document& doc, ls::SpriteId sprite, ls::Color color) {
    if (!ensurePalette(doc, sprite)) {
        return ls::kColorRoleNone;
    }
    const ls::PaletteId palette = paletteFor(doc, sprite);
    const std::vector<PaletteEntry> existing = paletteEntries(doc, palette);

    // The next free role, rather than the count: a removed entry in the middle
    // must not cause a new one to collide with a role already in use.
    ls::ColorRole role = 0;
    for (const PaletteEntry& entry : existing) {
        if (entry.role != ls::kColorRoleNone && entry.role >= role) {
            role = entry.role + 1;
        }
    }

    return setPaletteEntry(doc, palette, role, color) ? role : ls::kColorRoleNone;
}

ls::ColorRole layerRole(Document& doc, const PaintLayer& layer) {
    if (!layer.valid()) {
        return ls::kColorRoleNone;
    }
    return static_cast<ls::ColorRole>(roleParam(doc, layer.fill));
}

bool setLayerRole(Document& doc, const PaintLayer& layer, ls::ColorRole role) {
    if (!layer.valid()) {
        return false;
    }
    return doc.engine()
        .setOperationParameter(layer.fill, "paletteRole",
                               ls::ParameterValue{static_cast<int64_t>(role)})
        .ok();
}

bool resolvePaletteRole(Document& doc, ls::ColorRole role, ls::Color* out) {
    return resolvePaletteRole(doc, paletteOf(doc), role, out);
}

bool resolvePaletteRole(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                        ls::Color* out) {
    if (out == nullptr || role == ls::kColorRoleNone || !palette.valid()) {
        return false;
    }
    auto resolved = doc.engine().resolveSemanticColor(palette, role);
    if (resolved.fail()) {
        return false;
    }
    *out = resolved.value;
    return true;
}

ls::Color effectiveLayerColor(Document& doc, ls::SpriteId sprite,
                              const PaintLayer& layer) {
    const ls::ColorRole role = layerRole(doc, layer);
    if (role != ls::kColorRoleNone) {
        const ls::PaletteId palette = paletteOf(doc, sprite);
        if (palette.valid()) {
            auto resolved = doc.engine().resolveSemanticColor(palette, role);
            if (resolved.ok()) {
                return resolved.value;
            }
        }
    }
    // A dithered layer has no single colour, but a swatch showing nothing is
    // worse than one showing something representative. The middle of its ramp
    // is what the layer mostly looks like.
    if (layerIsDithered(doc, layer)) {
        DitherSettings settings;
        if (readDitherSettings(doc, layer, &settings)) {
            return ls::Color{
                static_cast<uint8_t>((settings.from.r + settings.to.r) / 2),
                static_cast<uint8_t>((settings.from.g + settings.to.g) / 2),
                static_cast<uint8_t>((settings.from.b + settings.to.b) / 2),
                static_cast<uint8_t>((settings.from.a + settings.to.a) / 2) };
        }
    }

    // No role, or a role the palette does not define: the layer's own colour is
    // what the engine would fall back to, so it is what the interface shows.
    return paintColor(doc, layer);
}

} // namespace fast
