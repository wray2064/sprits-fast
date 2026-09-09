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

// The palette a document is using, found without needing a sprite: the first
// sprite's bound palette. Every sprite Fast makes shares one.
ls::PaletteId paletteOf(Document& doc) {
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return ls::PaletteId{};
    }
    return paletteOf(doc, info.value.sprites.front());
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
    if (paletteOf(doc, sprite).valid()) {
        return true;
    }

    ls::PaletteDesc desc;
    desc.name = "palette";
    for (size_t i = 0; i < sizeof(kStarter) / sizeof(kStarter[0]); ++i) {
        ls::PaletteColorEntry entry;
        entry.role = static_cast<ls::ColorRole>(i);
        entry.color = kStarter[i];
        desc.entries.push_back(entry);
    }

    auto created = doc.engine().createPalette(doc.id(), desc);
    if (created.fail()) {
        return false;
    }
    // Bound to the document as well as the sprite, so a sprite added later
    // inherits it rather than starting with nothing.
    doc.engine().bindDocumentPalette(doc.id(), created.value);
    return doc.engine().bindSpritePalette(sprite, created.value).ok();
}

std::vector<PaletteEntry> paletteEntries(Document& doc) {
    std::vector<PaletteEntry> out;

    const ls::PaletteId palette = paletteOf(doc);
    if (!palette.valid()) {
        return out;
    }
    auto entries = doc.engine().getPaletteEntries(palette);
    if (entries.fail()) {
        return out;
    }
    out.reserve(entries.value.size());
    for (const ls::PaletteColorEntry& entry : entries.value) {
        out.push_back({ entry.role, entry.color });
    }
    return out;
}

bool setPaletteEntry(Document& doc, ls::ColorRole role, ls::Color color) {
    const ls::PaletteId palette = paletteOf(doc);
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
    const std::vector<PaletteEntry> existing = paletteEntries(doc);

    // The next free role, rather than the count: a removed entry in the middle
    // must not cause a new one to collide with a role already in use.
    ls::ColorRole role = 0;
    for (const PaletteEntry& entry : existing) {
        if (entry.role != ls::kColorRoleNone && entry.role >= role) {
            role = entry.role + 1;
        }
    }

    return setPaletteEntry(doc, role, color) ? role : ls::kColorRoleNone;
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
