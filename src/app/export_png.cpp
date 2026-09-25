// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/export_png.h"
#include "app/file_io.h"
#include "app/palette.h"
#include "app/zlib.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

// stb writes to C stdio by default, and those entry points open the file with
// fopen(const char*) -- which on Windows reads the path in the active code page
// and silently mangles anything outside ASCII. Fast has already been through
// that bug once. Compiling the file-writing half out entirely means it cannot be
// reached by accident: only the to_func and to_mem forms remain.
#define STBI_WRITE_NO_STDIO

#include "stb_image_write.h"

#include <vector>

namespace fast {
namespace {

// stb hands its output over in pieces; this collects them.
void collect(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

// Whole-number magnification, by duplication. Never interpolation: a pixel-art
// sprite at 4x is four identical pixels per side, and anything else defeats the
// point of the format it is being written in.
ls::RasterBuffer magnify(const ls::RasterBuffer& source, uint32_t scale) {
    if (scale <= 1) {
        return source;
    }
    ls::RasterBuffer out = ls::makeRaster(source.width * scale, source.height * scale);
    if (out.empty()) {
        return out;
    }
    for (uint32_t y = 0; y < source.height; ++y) {
        for (uint32_t x = 0; x < source.width; ++x) {
            const ls::Color pixel = ls::readPixel(source, static_cast<int32_t>(x),
                                                  static_cast<int32_t>(y));
            for (uint32_t dy = 0; dy < scale; ++dy) {
                for (uint32_t dx = 0; dx < scale; ++dx) {
                    ls::writePixel(out,
                                   static_cast<int32_t>(x * scale + dx),
                                   static_cast<int32_t>(y * scale + dy), pixel);
                }
            }
        }
    }
    return out;
}

} // namespace

ls::RasterBuffer magnifyRaster(const ls::RasterBuffer& source, uint32_t scale) {
    return magnify(source, scale);
}

bool encodeRasterToPng(const ls::RasterBuffer& raster, const ExportSettings& settings,
                       std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    if (raster.empty() || raster.width == 0 || raster.height == 0) {
        if (error) { *error = "there is nothing to export"; }
        return false;
    }
    if (settings.scale == 0 || settings.scale > ExportSettings::kMaxScale) {
        if (error) { *error = "that scale is out of range"; }
        return false;
    }

    const ls::RasterBuffer scaled = magnify(raster, settings.scale);
    if (scaled.empty()) {
        if (error) { *error = "the image is too large at that scale"; }
        return false;
    }

    // Set every time rather than trusting whatever this global holds. It is
    // process-wide state in a library, and the same sprite has to export to the
    // same bytes.
    stbi_write_png_compression_level = 8;
    stbi_flip_vertically_on_write(0);

    const int written = stbi_write_png_to_func(
        collect, out,
        static_cast<int>(scaled.width), static_cast<int>(scaled.height),
        4,                                  // RGBA, which is how the engine compiles
        scaled.pixels.data(),
        static_cast<int>(scaled.stride));

    if (written == 0 || out->empty()) {
        out->clear();
        if (error) { *error = "the image could not be encoded"; }
        return false;
    }
    return true;
}

bool compileForExport(Document& doc, ls::SpriteId sprite, uint32_t scale,
                      ls::RasterBuffer* out, std::string* error) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (out == nullptr || size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        if (error) { *error = "this document has no canvas"; }
        return false;
    }
    if (scale == 0 || scale > ExportSettings::kMaxScale) {
        if (error) { *error = "that scale is outside 1x to 64x"; }
        return false;
    }
    auto compiled = doc.engine().compileSprite(
        sprite, compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
                               static_cast<uint32_t>(size.value.y)));
    if (compiled.fail()) {
        if (error) {
            *error = "the sprite could not be compiled: " +
                     std::string(ls::lsErrorString(compiled.error));
        }
        return false;
    }
    if (scale == 1) {
        *out = std::move(compiled.value.raster);
        return true;
    }
    *out = magnifyRaster(compiled.value.raster, scale);
    if (out->empty()) {
        if (error) { *error = "that is too large to write at that scale"; }
        return false;
    }
    return true;
}

bool exportSpriteToPng(Document& doc, ls::SpriteId sprite, const std::string& path,
                       const ExportSettings& settings, std::string* error) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        if (error) { *error = "this document has no canvas"; }
        return false;
    }

    // Export quality, not what is on screen. Preview and Export are different
    // profiles on purpose.
    const ls::CompileProfile profile = compileProfile(
        ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x), static_cast<uint32_t>(size.value.y));

    auto compiled = doc.engine().compileSprite(sprite, profile);
    if (compiled.fail()) {
        if (error) {
            *error = "the sprite could not be compiled: " +
                     std::string(ls::lsErrorString(compiled.error));
        }
        return false;
    }

    std::vector<uint8_t> png;
    if (!encodeRasterToPng(compiled.value.raster, settings, &png, error)) {
        return false;
    }
    return writeFileAtomic(withExtension(path, ".png"), png, error);
}

bool exportSpriteToIndexedPng(Document& doc, ls::SpriteId sprite, const std::string& path,
                              const ExportSettings& settings, IndexedReport* report,
                              std::string* error) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        if (error) { *error = "this document has no canvas"; }
        return false;
    }
    const ls::CompileProfile profile = compileProfile(
        ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
        static_cast<uint32_t>(size.value.y));
    auto compiled = doc.engine().compileSprite(sprite, profile);
    if (compiled.fail()) {
        if (error) { *error = "the sprite could not be compiled"; }
        return false;
    }
    std::vector<ls::Color> table;
    for (const PaletteEntry& entry : paletteEntries(doc, paletteFor(doc, sprite))) {
        table.push_back(entry.color);
    }
    std::vector<uint8_t> png;
    if (!encodeIndexedPng(compiled.value.raster, table, settings.scale, &png, report, error)) {
        return false;
    }
    return writeFileAtomic(withExtension(path, ".png"), png, error);
}

bool zlibDeflate(const std::vector<uint8_t>& data, std::vector<uint8_t>* out) {
    if (out == nullptr || data.size() > 0x7FFFFFFF) {
        return false;
    }
    int length = 0;
    unsigned char* packed = stbi_zlib_compress(const_cast<unsigned char*>(data.data()),
                                               static_cast<int>(data.size()), &length, 8);
    if (packed == nullptr || length <= 0) {
        STBIW_FREE(packed);
        return false;
    }
    out->assign(packed, packed + length);
    STBIW_FREE(packed);
    return true;
}

uint32_t pngCrc(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        built = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void appendPngChunk(std::vector<uint8_t>& out, const char type[4],
                    const std::vector<uint8_t>& data) {
    const auto put32 = [&](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    put32(static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(pngCrc(out.data() + start, out.size() - start));
}

bool encodeIndexedPng(const ls::RasterBuffer& raster, const std::vector<ls::Color>& palette,
                      uint32_t scale, std::vector<uint8_t>* out, IndexedReport* report,
                      std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) { *error = why; }
        return false;
    };
    if (out == nullptr) {
        return false;
    }
    if (raster.empty() || raster.width == 0 || raster.height == 0) {
        return fail("there is nothing to export");
    }
    if (scale == 0 || scale > ExportSettings::kMaxScale) {
        return fail("that scale is out of range");
    }
    const ls::RasterBuffer image = magnify(raster, scale);
    if (image.empty()) {
        return fail("the image is too large at that scale");
    }

    // The table: the palette, in its order, so slot n is index n and a game
    // swapping the table swaps the slots. Then what the picture needs that the
    // palette lacks -- a clear entry, colours painted as values -- after it.
    IndexedReport said;
    std::vector<ls::Color> table = palette;
    said.paletteEntries = palette.size();
    const auto key = [](ls::Color c) {
        return static_cast<uint32_t>(c.r) << 24 | static_cast<uint32_t>(c.g) << 16 |
               static_cast<uint32_t>(c.b) << 8 | c.a;
    };
    std::vector<std::pair<uint32_t, uint8_t>> lookup;
    const auto find = [&](uint32_t k) -> int {
        for (const auto& [colour, index] : lookup) {
            if (colour == k) { return index; }
        }
        return -1;
    };
    if (table.size() > 256) {
        return fail("the palette has more than 256 slots, more than a PNG table holds");
    }
    for (size_t n = 0; n < table.size(); ++n) {
        if (find(key(table[n])) < 0) {
            lookup.push_back({ key(table[n]), static_cast<uint8_t>(n) });
        }
    }
    int clear = -1;
    for (size_t n = 0; n < table.size(); ++n) {
        if (table[n].a == 0) { clear = static_cast<int>(n); break; }
    }

    std::vector<uint8_t> rows;
    rows.reserve((static_cast<size_t>(image.width) + 1) * image.height);
    for (uint32_t y = 0; y < image.height; ++y) {
        rows.push_back(0);                      // filter: none
        const uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4u;
            if (p[3] == 0) {
                if (clear < 0) {
                    if (table.size() >= 256) {
                        return fail("the picture needs more than 256 colours to be indexed");
                    }
                    clear = static_cast<int>(table.size());
                    table.push_back({ 0, 0, 0, 0 });
                    said.transparentEntry = true;
                }
                rows.push_back(static_cast<uint8_t>(clear));
                continue;
            }
            const ls::Color c { p[0], p[1], p[2], p[3] };
            int index = find(key(c));
            if (index < 0) {
                if (table.size() >= 256) {
                    return fail("the picture needs more than 256 colours to be indexed");
                }
                index = static_cast<int>(table.size());
                table.push_back(c);
                lookup.push_back({ key(c), static_cast<uint8_t>(index) });
                ++said.extraColours;
            }
            rows.push_back(static_cast<uint8_t>(index));
        }
    }
    if (table.empty()) {
        table.push_back({ 0, 0, 0, 0 });
    }

    std::vector<uint8_t> packed;
    if (!zlibDeflate(rows, &packed)) {
        return fail("the image could not be compressed");
    }
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> ihdr;
    const auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>(x >> 24));
        v.push_back(static_cast<uint8_t>(x >> 16));
        v.push_back(static_cast<uint8_t>(x >> 8));
        v.push_back(static_cast<uint8_t>(x));
    };
    put32(ihdr, image.width);
    put32(ihdr, image.height);
    ihdr.insert(ihdr.end(), { 8, 3, 0, 0, 0 });   // 8-bit, indexed, deflate, none, no interlace
    appendPngChunk(png, "IHDR", ihdr);
    std::vector<uint8_t> plte;
    std::vector<uint8_t> trns;
    size_t lastSeeThrough = 0;
    for (size_t n = 0; n < table.size(); ++n) {
        plte.insert(plte.end(), { table[n].r, table[n].g, table[n].b });
        trns.push_back(table[n].a);
        if (table[n].a != 255) { lastSeeThrough = n + 1; }
    }
    appendPngChunk(png, "PLTE", plte);
    if (lastSeeThrough > 0) {
        trns.resize(lastSeeThrough);          // entries after the last are opaque
        appendPngChunk(png, "tRNS", trns);
    }
    appendPngChunk(png, "IDAT", packed);
    appendPngChunk(png, "IEND", {});
    said.tableSize = table.size();
    if (report != nullptr) {
        *report = said;
    }
    *out = std::move(png);
    return true;
}

} // namespace fast
