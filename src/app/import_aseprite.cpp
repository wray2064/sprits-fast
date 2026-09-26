// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/import_aseprite.h"
#include "app/layers.h"

#include "app/animation.h"
#include "app/file_io.h"
#include "app/palette.h"
#include "app/zlib.h"

#include <algorithm>
#include <map>

namespace fast {

namespace {

// Chunk types, from the published description.
constexpr uint16_t kOldPalette     = 0x0004;
constexpr uint16_t kOldPalette64   = 0x0011;
constexpr uint16_t kLayerChunk     = 0x2004;
constexpr uint16_t kCelChunk       = 0x2005;
constexpr uint16_t kTagsChunk      = 0x2018;
constexpr uint16_t kPaletteChunk   = 0x2019;

constexpr uint16_t kLayerVisible    = 1;
constexpr uint16_t kLayerBackground = 8;

// Every pixel of every cel, together. Past this a file is refused: the cels
// are held decoded while the document is built.
constexpr uint64_t kMaxCelPixels = 64ull * 1024ull * 1024ull;

// Little-endian reads that stop, and say so, at the end of what they were
// given -- never past it.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool   ok() const { return ok_; }
    size_t at() const { return at_; }
    size_t left() const { return ok_ ? size_ - at_ : 0; }
    void   seek(size_t to) { if (to > size_) { ok_ = false; } else { at_ = to; } }
    void   skip(size_t n) { seek(at_ + n); }

    uint8_t u8() {
        if (!need(1)) { return 0; }
        return data_[at_++];
    }
    uint16_t u16() {
        if (!need(2)) { return 0; }
        const uint16_t v = static_cast<uint16_t>(data_[at_] | data_[at_ + 1] << 8);
        at_ += 2;
        return v;
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        if (!need(4)) { return 0; }
        const uint32_t v = static_cast<uint32_t>(data_[at_]) |
                           static_cast<uint32_t>(data_[at_ + 1]) << 8 |
                           static_cast<uint32_t>(data_[at_ + 2]) << 16 |
                           static_cast<uint32_t>(data_[at_ + 3]) << 24;
        at_ += 4;
        return v;
    }
    std::string text() {
        const uint16_t length = u16();
        if (!need(length)) { return {}; }
        std::string out(reinterpret_cast<const char*>(data_ + at_), length);
        at_ += length;
        return out;
    }
    const uint8_t* here() const { return data_ + at_; }

private:
    bool need(size_t n) {
        if (!ok_ || n > size_ - at_) {
            ok_ = false;
            return false;
        }
        return true;
    }
    const uint8_t* data_;
    size_t size_;
    size_t at_ = 0;
    bool ok_ = true;
};

bool fail(std::string* error, const std::string& why) {
    if (error != nullptr) {
        *error = why;
    }
    return false;
}

// Aseprite's nineteen blend modes onto the engine's eleven: the same where
// there is one, the nearest where there is not -- and said.
ls::BlendMode blendFor(uint16_t mode, bool* approximated) {
    switch (mode) {
        case 0:  return ls::BlendMode::Normal;
        case 1:  return ls::BlendMode::Multiply;
        case 2:  return ls::BlendMode::Screen;
        case 3:  return ls::BlendMode::Overlay;
        case 4:  return ls::BlendMode::Darken;
        case 5:  return ls::BlendMode::Lighten;
        case 10: return ls::BlendMode::Difference;
        case 16: return ls::BlendMode::Add;
        case 17: return ls::BlendMode::Subtract;
        case 8:                                    // hard light
        case 9:                                    // soft light
            *approximated = true;
            return ls::BlendMode::Overlay;
        case 11:                                   // exclusion
            *approximated = true;
            return ls::BlendMode::Difference;
        case 6:                                    // colour dodge
            *approximated = true;
            return ls::BlendMode::Screen;
        case 7:                                    // colour burn
            *approximated = true;
            return ls::BlendMode::Multiply;
        default:                                   // hue, saturation, colour, luminosity, divide
            *approximated = true;
            return ls::BlendMode::Normal;
    }
}

bool parseCel(Reader& chunk, const AseFile& file, AseFile::Cel* cel, uint64_t* budget,
              std::string* error) {
    cel->layer = chunk.u16();
    cel->x = chunk.i16();
    cel->y = chunk.i16();
    cel->opacity = chunk.u8();
    const uint16_t type = chunk.u16();
    chunk.skip(2 + 5);                     // z-index, reserved
    if (!chunk.ok()) {
        return fail(error, "a cel is cut short");
    }
    if (type == 1) {
        cel->linkedFrame = chunk.u16();
        return chunk.ok() || fail(error, "a linked cel is cut short");
    }
    if (type != 0 && type != 2) {
        return true;                       // a tilemap cel: skipped, and reported
    }
    cel->width = chunk.u16();
    cel->height = chunk.u16();
    const uint64_t pixels = static_cast<uint64_t>(cel->width) * cel->height;
    if (!chunk.ok() || cel->width == 0 || cel->height == 0) {
        return fail(error, "a cel has no size");
    }
    if (pixels > *budget) {
        return fail(error, "its cels hold more pixels than Fast will read at once");
    }
    *budget -= pixels;
    const size_t bytes = static_cast<size_t>(pixels) * static_cast<size_t>(file.depth / 8);
    if (type == 0) {
        if (chunk.left() < bytes) {
            return fail(error, "a cel's pixels are cut short");
        }
        cel->pixels.assign(chunk.here(), chunk.here() + bytes);
        chunk.skip(bytes);
        return true;
    }
    if (!zlibInflate(chunk.here(), chunk.left(), bytes, &cel->pixels)) {
        return fail(error, "a cel's compressed pixels do not decode to its size");
    }
    return true;
}

} // namespace

bool looksLikeAsepriteName(const std::string& path) {
    return hasExtension(path, ".aseprite") || hasExtension(path, ".ase");
}

bool parseAseprite(const std::vector<uint8_t>& bytes, AseFile* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    Reader file(bytes.data(), bytes.size());
    const uint32_t fileSize = file.u32();
    const uint16_t magic = file.u16();
    if (!file.ok() || magic != 0xA5E0) {
        return fail(error, "it is not an Aseprite file");
    }
    if (fileSize > bytes.size()) {
        return fail(error, "the file is shorter than it says it is");
    }
    AseFile ase;
    const uint16_t frames = file.u16();
    ase.width = file.u16();
    ase.height = file.u16();
    ase.depth = file.u16();
    const uint32_t flags = file.u32();
    ase.layerOpacityValid = (flags & 1) != 0;
    const bool layerUuids = (flags & 4) != 0;
    file.skip(2 + 4 + 4);                  // speed, two reserved
    ase.transparentIndex = file.u8();
    file.skip(3 + 2 + 1 + 1 + 2 + 2 + 2 + 2 + 84);
    if (!file.ok()) {
        return fail(error, "the header is cut short");
    }
    if (ase.depth != 32 && ase.depth != 16 && ase.depth != 8) {
        return fail(error, "it has a colour depth Aseprite does not write");
    }
    if (frames == 0 || frames > kMaxFrames) {
        return fail(error, "it has " + std::to_string(frames) + " frames; Fast holds " +
                    std::to_string(kMaxFrames));
    }
    if (ase.width == 0 || ase.height == 0 || ase.width > kMaxCanvasDimension ||
        ase.height > kMaxCanvasDimension ||
        static_cast<uint64_t>(ase.width) * ase.height > kMaxCanvasPixels) {
        return fail(error, "its canvas is not a size Fast works on");
    }

    uint64_t budget = kMaxCelPixels;
    bool paletteChunkSeen = false;
    for (uint16_t f = 0; f < frames; ++f) {
        const size_t frameStart = file.at();
        const uint32_t frameBytes = file.u32();
        const uint16_t frameMagic = file.u16();
        const uint16_t oldChunks = file.u16();
        AseFile::Frame frame;
        frame.durationMs = file.u16();
        file.skip(2);
        const uint32_t newChunks = file.u32();
        if (!file.ok() || frameMagic != 0xF1FA || frameBytes < 16 ||
            frameBytes > bytes.size() - frameStart) {
            return fail(error, "frame " + std::to_string(f + 1) + " is damaged");
        }
        const uint32_t chunks = newChunks != 0 ? newChunks : oldChunks;
        const size_t frameEnd = frameStart + frameBytes;

        for (uint32_t c = 0; c < chunks; ++c) {
            const size_t chunkStart = file.at();
            const uint32_t chunkSize = file.u32();
            const uint16_t type = file.u16();
            if (!file.ok() || chunkSize < 6 || chunkSize > frameEnd - chunkStart) {
                return fail(error, "a block in frame " + std::to_string(f + 1) + " is damaged");
            }
            Reader chunk(bytes.data() + chunkStart + 6, chunkSize - 6);
            switch (type) {
                case kLayerChunk: {
                    AseFile::Layer layer;
                    layer.flags = chunk.u16();
                    layer.type = chunk.u16();
                    layer.childLevel = chunk.u16();
                    chunk.skip(4);
                    layer.blend = chunk.u16();
                    layer.opacity = chunk.u8();
                    chunk.skip(3);
                    layer.name = chunk.text();
                    (void)layerUuids;       // after the name; nothing here needs it
                    if (!chunk.ok()) {
                        return fail(error, "a layer is damaged");
                    }
                    if (ase.layers.size() >= 1024) {
                        return fail(error, "it has more layers than Fast will read");
                    }
                    ase.layers.push_back(layer);
                    break;
                }
                case kCelChunk: {
                    AseFile::Cel cel;
                    if (!parseCel(chunk, ase, &cel, &budget, error)) {
                        return false;
                    }
                    frame.cels.push_back(std::move(cel));
                    break;
                }
                case kPaletteChunk: {
                    const uint32_t size = chunk.u32();
                    const uint32_t first = chunk.u32();
                    const uint32_t last = chunk.u32();
                    chunk.skip(8);
                    if (!chunk.ok() || size > 256 || first > last || last >= 256) {
                        return fail(error, "the palette is damaged");
                    }
                    ase.palette.resize(std::max<size_t>(ase.palette.size(), size));
                    ase.paletteNames.resize(ase.palette.size());
                    for (uint32_t i = first; i <= last; ++i) {
                        const uint16_t entryFlags = chunk.u16();
                        ls::Color colour;
                        colour.r = chunk.u8();
                        colour.g = chunk.u8();
                        colour.b = chunk.u8();
                        colour.a = chunk.u8();
                        std::string label = (entryFlags & 1) ? chunk.text() : std::string();
                        if (!chunk.ok()) {
                            return fail(error, "the palette is cut short");
                        }
                        if (i >= ase.palette.size()) {
                            ase.palette.resize(i + 1);
                            ase.paletteNames.resize(i + 1);
                        }
                        ase.palette[i] = colour;
                        ase.paletteNames[i] = label;
                    }
                    paletteChunkSeen = true;
                    break;
                }
                case kOldPalette:
                case kOldPalette64: {
                    if (paletteChunkSeen) {
                        break;          // the new chunk says it better
                    }
                    const uint16_t packets = chunk.u16();
                    size_t index = 0;
                    for (uint16_t p = 0; p < packets && chunk.ok(); ++p) {
                        index += chunk.u8();
                        size_t count = chunk.u8();
                        if (count == 0) { count = 256; }
                        for (size_t i = 0; i < count && chunk.ok(); ++i, ++index) {
                            uint8_t r = chunk.u8(), g = chunk.u8(), b = chunk.u8();
                            if (type == kOldPalette64) {
                                r = static_cast<uint8_t>(r * 255 / 63);
                                g = static_cast<uint8_t>(g * 255 / 63);
                                b = static_cast<uint8_t>(b * 255 / 63);
                            }
                            if (index < 256) {
                                if (index >= ase.palette.size()) {
                                    ase.palette.resize(index + 1);
                                    ase.paletteNames.resize(index + 1);
                                }
                                ase.palette[index] = ls::Color{ r, g, b, 255 };
                            }
                        }
                    }
                    break;
                }
                case kTagsChunk: {
                    const uint16_t count = chunk.u16();
                    chunk.skip(8);
                    for (uint16_t t = 0; t < count && chunk.ok(); ++t) {
                        AseFile::Tag tag;
                        tag.from = chunk.u16();
                        tag.to = chunk.u16();
                        tag.direction = chunk.u8();
                        tag.repeat = chunk.u16();
                        chunk.skip(6 + 3 + 1);
                        tag.name = chunk.text();
                        if (chunk.ok()) {
                            ase.tags.push_back(tag);
                        }
                    }
                    break;
                }
                default:
                    break;               // user data, slices, profiles: not Fast's
            }
            file.seek(chunkStart + chunkSize);
        }
        file.seek(frameEnd);
        if (!file.ok()) {
            return fail(error, "the file ends inside frame " + std::to_string(f + 1));
        }
        ase.frames.push_back(std::move(frame));
    }
    *out = std::move(ase);
    return true;
}

bool documentFromAseprite(Document& doc, const AseFile& file, const std::string& name,
                          AsepriteReport* report, std::string* error) {
    AsepriteReport said;
    said.frames = file.frames.size();
    said.indexed = file.depth == 8;

    if (!doc.create(name, file.width, file.height)) {
        return fail(error, "the engine would not make a canvas that size");
    }
    ls::LSContext& engine = doc.engine();
    auto info = engine.getDocumentInfo(doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return fail(error, "the document could not be set up");
    }

    // The palette, index for slot. In an indexed sprite the index *is* the
    // colour's identity, so every pixel paints through its index's slot.
    if (!file.palette.empty()) {
        ls::PaletteDesc desc;
        desc.name = name.empty() ? std::string("palette") : name;
        for (size_t i = 0; i < file.palette.size(); ++i) {
            ls::PaletteColorEntry entry;
            entry.role = static_cast<ls::ColorRole>(i);
            entry.color = file.palette[i];
            entry.label = i < file.paletteNames.size() ? file.paletteNames[i] : std::string();
            desc.entries.push_back(entry);
        }
        auto palette = engine.createPalette(doc.id(), desc);
        if (palette.fail() || engine.bindDocumentPalette(doc.id(), palette.value).fail()) {
            return fail(error, "the palette could not be made");
        }
    } else {
        ensurePalette(doc, info.value.sprites.front());
    }
    // An RGBA pixel of exactly a palette colour paints through that slot.
    std::map<uint32_t, ls::ColorRole> slotOf;
    for (size_t i = file.palette.size(); i-- > 0;) {
        const ls::Color& c = file.palette[i];
        slotOf[static_cast<uint32_t>(c.r) << 24 | static_cast<uint32_t>(c.g) << 16 |
               static_cast<uint32_t>(c.b) << 8 | c.a] = static_cast<ls::ColorRole>(i);
    }

    std::vector<ls::SpriteId> sprites { info.value.sprites.front() };
    for (size_t f = 1; f < file.frames.size(); ++f) {
        auto made = engine.createSprite(doc.id());
        if (made.fail()) {
            return fail(error, "a frame could not be made");
        }
        sprites.push_back(made.value);
    }

    // The cel of a layer in a frame, following a link to the frame it shows.
    const auto celOf = [&](size_t frame, size_t layer) -> const AseFile::Cel* {
        for (int hops = 0; hops < 8 && frame < file.frames.size(); ++hops) {
            const AseFile::Cel* found = nullptr;
            for (const AseFile::Cel& cel : file.frames[frame].cels) {
                if (static_cast<size_t>(cel.layer) == layer) {
                    found = &cel;
                }
            }
            if (found == nullptr || found->linkedFrame < 0) {
                return found;
            }
            frame = static_cast<size_t>(found->linkedFrame);
        }
        return nullptr;
    };

    for (size_t f = 0; f < file.frames.size(); ++f) {
        const ls::SpriteId sprite = sprites[f];
        // The group each layer sits in. The engine's groups are one level
        // deep, so a nested one joins its outermost ancestor.
        ls::GroupId openGroup;
        for (size_t l = 0; l < file.layers.size(); ++l) {
            const AseFile::Layer& layer = file.layers[l];
            if (layer.childLevel == 0) {
                openGroup = ls::GroupId{};
            }
            const bool visible = (layer.flags & kLayerVisible) != 0;
            const float opacity = file.layerOpacityValid ? layer.opacity / 255.f : 1.f;
            if (layer.type == 1) {
                if (layer.childLevel == 0) {
                    ls::GroupDesc desc;
                    desc.name = layer.name;
                    desc.visible = visible;
                    desc.opacity = opacity;
                    desc.blend = blendFor(layer.blend, &said.approximatedBlends);
                    auto group = engine.createGroup(sprite, desc);
                    if (group.ok()) {
                        openGroup = group.value;
                    }
                }
                continue;
            }
            if (layer.type == 2) {
                said.skippedTilemaps = true;
                continue;
            }
            ls::LayerDesc desc;
            desc.name = layer.name;
            desc.visible = visible;
            desc.opacity = opacity;
            desc.blend = blendFor(layer.blend, &said.approximatedBlends);
            auto made = engine.createLayer(sprite, desc);
            if (made.fail()) {
                return fail(error, "a layer could not be made");
            }
            if (openGroup.valid() && layer.childLevel > 0) {
                engine.addLayerToGroup(openGroup, made.value);
            }

            // The cel's pixels, one region per colour -- or per index, for an
            // indexed sprite, so two indices sharing a colour stay two slots.
            std::map<uint64_t, ls::IntervalSet> runs;     // key: role or 1<<40 | rgba
            const AseFile::Cel* cel = celOf(f, l);
            const bool background = (layer.flags & kLayerBackground) != 0;
            if (cel != nullptr && !cel->pixels.empty()) {
                const int bytesPer = file.depth / 8;
                for (uint32_t y = 0; y < cel->height; ++y) {
                    uint64_t runKey = 0;
                    int32_t runStart = 0;
                    bool inRun = false;
                    const auto close = [&](int32_t end) {
                        if (inRun) {
                            runs[runKey].intervals.push_back(
                                { cel->y + static_cast<int32_t>(y), cel->x + runStart, cel->x + end });
                        }
                        inRun = false;
                    };
                    for (uint32_t x = 0; x < cel->width; ++x) {
                        const uint8_t* p = cel->pixels.data() +
                                           (static_cast<size_t>(y) * cel->width + x) * bytesPer;
                        uint64_t key = 0;
                        bool opaque = true;
                        if (file.depth == 8) {
                            opaque = background || p[0] != file.transparentIndex;
                            key = p[0];
                        } else {
                            const ls::Color c = file.depth == 32
                                ? ls::Color{ p[0], p[1], p[2], p[3] }
                                : ls::Color{ p[0], p[0], p[0], p[1] };
                            opaque = c.a != 0;
                            const uint32_t rgba = static_cast<uint32_t>(c.r) << 24 |
                                                  static_cast<uint32_t>(c.g) << 16 |
                                                  static_cast<uint32_t>(c.b) << 8 | c.a;
                            auto slot = slotOf.find(rgba);
                            key = slot != slotOf.end() ? slot->second
                                                       : (1ull << 40) | rgba;
                        }
                        if (!opaque) {
                            close(static_cast<int32_t>(x));
                            continue;
                        }
                        if (inRun && key == runKey) {
                            continue;
                        }
                        close(static_cast<int32_t>(x));
                        inRun = true;
                        runKey = key;
                        runStart = static_cast<int32_t>(x);
                    }
                    close(static_cast<int32_t>(cel->width));
                }
            }

            const float celOpacity = cel != nullptr ? cel->opacity / 255.f : 1.f;
            bool any = false;
            for (auto& [key, set] : runs) {
                auto region = engine.createRegionFromIntervals(doc.id(), ls::geom::normalize(set));
                if (region.fail()) {
                    return fail(error, "the pixels could not be read in");
                }
                ls::FillSolidOp fill;
                fill.targetRegion = region.value;

                if (key & (1ull << 40)) {
                    const uint32_t rgba = static_cast<uint32_t>(key);
                    fill.fallbackColor = { static_cast<uint8_t>(rgba >> 24),
                                           static_cast<uint8_t>(rgba >> 16),
                                           static_cast<uint8_t>(rgba >> 8),
                                           static_cast<uint8_t>(rgba) };
                } else {
                    fill.paletteRole = static_cast<ls::ColorRole>(key);
                    fill.fallbackColor = key < file.palette.size() ? file.palette[key]
                                                                   : ls::Color{ 0, 0, 0, 255 };
                }
                if (engine.addOperation(made.value, fill).fail()) {
                    return fail(error, "the pixels could not be read in");
                }
                any = true;
            }
            if (!any) {
                auto region = engine.createRegionFromIntervals(doc.id(), ls::IntervalSet{});
                if (region.ok()) {
                    ls::FillSolidOp fill;
                    fill.targetRegion = region.value;
                    fill.fallbackColor = file.palette.empty() ? ls::Color{ 0, 0, 0, 255 }
                                                              : file.palette.front();
                    engine.addOperation(made.value, fill);
                }
            }
            // The cel's opacity is the cel's: one fade over its drawing.
            if (celOpacity < 1.f) {
                setCelOpacity(doc, made.value, celOpacity);
            }
            if (f == 0) {
                ++said.layers;
            }
        }
    }

    for (size_t f = 0; f < file.frames.size(); ++f) {
        if (file.frames[f].durationMs > 0) {
            setFrameDuration(doc, static_cast<int>(f), file.frames[f].durationMs);
        }
    }

    // Tags become cycles: a forward tag plays its frames, a reverse one plays
    // them backwards, and the ping-pongs go there and back.
    std::vector<Cycle> cycles;
    const int frameCount = static_cast<int>(file.frames.size());
    for (const AseFile::Tag& tag : file.tags) {
        if (tag.from < 0 || tag.to < tag.from || tag.to >= frameCount) {
            continue;
        }
        Cycle cycle;
        cycle.name = tag.name.substr(0, kMaxNameLength);
        for (int i = tag.from; i <= tag.to; ++i) {
            cycle.frames.push_back(i);
        }
        if (tag.direction == 1 || tag.direction == 3) {
            std::reverse(cycle.frames.begin(), cycle.frames.end());
        }
        cycle.loop = (tag.direction == 2 || tag.direction == 3) ? LoopMode::PingPong
                   : tag.repeat == 1 ? LoopMode::Once : LoopMode::Loop;
        cycles.push_back(cycle);
    }
    if (!cycles.empty()) {
        setCycles(doc, cycles, frameCount);
    }
    said.tags = cycles.size();

    doc.clearHistory();
    doc.markUnmodified();

    auto palette = paletteEntries(doc);
    said.colours = palette.size();
    said.throughPalette = !file.palette.empty();
    if (report != nullptr) {
        *report = said;
    }
    return true;
}

bool openAsepriteAsDocument(Document& doc, const std::string& path, AsepriteReport* report,
                            std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return false;
    }
    AseFile file;
    if (!parseAseprite(bytes, &file, error)) {
        return false;
    }
    return documentFromAseprite(doc, file, fileStem(path), report, error);
}

} // namespace fast
