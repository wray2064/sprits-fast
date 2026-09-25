// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/export_anim.h"

#include "app/export_png.h"
#include "app/file_io.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_map>

namespace fast {

namespace {

// ------------------------------------------------------------------ bytes --

void put16(std::vector<uint8_t>& out, uint32_t v) {         // little-endian, for GIF
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put32be(std::vector<uint8_t>& out, uint32_t v) {       // big-endian, for PNG
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

uint32_t read32be(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[3]);
}

// ------------------------------------------------------------------- GIF --

// LSB-first bit packing into a byte stream, as GIF's LZW wants it.
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}
    void write(uint32_t code, int bits) {
        buffer_ |= code << used_;
        used_ += bits;
        while (used_ >= 8) {
            out_.push_back(static_cast<uint8_t>(buffer_ & 0xFF));
            buffer_ >>= 8;
            used_ -= 8;
        }
    }
    void flush() {
        if (used_ > 0) {
            out_.push_back(static_cast<uint8_t>(buffer_ & 0xFF));
        }
        buffer_ = 0;
        used_ = 0;
    }
private:
    std::vector<uint8_t>& out_;
    uint32_t buffer_ = 0;
    int      used_ = 0;
};

// Variable-width LZW over palette indices. The code width follows the
// decoder, which widens its codes when its next free entry reaches a power of
// two; the encoder runs one entry ahead of it, hence the "- 1".
std::vector<uint8_t> lzw(const std::vector<uint8_t>& indices, int minCodeSize) {
    std::vector<uint8_t> packed;
    BitWriter bits(packed);
    const uint32_t clear = 1u << minCodeSize;
    const uint32_t eoi = clear + 1;
    std::vector<int16_t> table(4096u * 256u, -1);
    uint32_t next = eoi + 1;
    int width = minCodeSize + 1;

    const auto reset = [&]() {
        std::fill(table.begin(), table.end(), static_cast<int16_t>(-1));
        next = eoi + 1;
        width = minCodeSize + 1;
    };

    bits.write(clear, width);
    if (indices.empty()) {
        bits.write(eoi, width);
        bits.flush();
        return packed;
    }
    uint32_t prefix = indices[0];
    for (size_t i = 1; i < indices.size(); ++i) {
        const uint32_t k = indices[i];
        const size_t key = static_cast<size_t>(prefix) * 256u + k;
        if (table[key] >= 0) {
            prefix = static_cast<uint32_t>(table[key]);
            continue;
        }
        bits.write(prefix, width);
        if (next < 4096) {
            table[key] = static_cast<int16_t>(next++);
            if (next - 1 == (1u << width) && width < 12) {
                ++width;
            }
        } else {
            bits.write(clear, width);
            reset();
        }
        prefix = k;
    }
    bits.write(prefix, width);
    bits.write(eoi, width);
    bits.flush();
    return packed;
}

using Rgb = std::array<uint8_t, 3>;

struct IndexedFrame {
    std::vector<Rgb>     palette;     // entry 0 is transparent
    std::vector<uint8_t> indices;
};

uint32_t rgbKey(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2];
}

// The colours of `frames`, each once, alpha below half being transparent --
// false if there are more than 255.
bool gatherColours(const std::vector<ls::RasterBuffer>& frames, std::vector<Rgb>* out,
                   bool* partialAlpha) {
    std::unordered_map<uint32_t, int> seen;
    for (const ls::RasterBuffer& frame : frames) {
        for (uint32_t y = 0; y < frame.height; ++y) {
            const uint8_t* row = frame.row(y);
            for (uint32_t x = 0; x < frame.width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4u;
                if (p[3] != 0 && p[3] != 255) {
                    *partialAlpha = true;
                }
                if (p[3] < 128) {
                    continue;
                }
                const uint32_t key = rgbKey(p);
                if (seen.emplace(key, 0).second) {
                    if (seen.size() > 255) {
                        return false;
                    }
                    out->push_back({ p[0], p[1], p[2] });
                }
            }
        }
    }
    return true;
}

// The last resort: a fixed cube of 6 x 7 x 6 levels, 252 colours, each pixel
// to its nearest. Green gets the extra level because the eye resolves it best.
Rgb cubeColour(int r, int g, int b) {
    return { static_cast<uint8_t>(r * 51), static_cast<uint8_t>(g * 255 / 6),
             static_cast<uint8_t>(b * 51) };
}

IndexedFrame index(const ls::RasterBuffer& frame, const std::vector<Rgb>& colours,
                   bool cube) {
    IndexedFrame out;
    out.palette.push_back({ 0, 0, 0 });
    std::unordered_map<uint32_t, uint8_t> lookup;
    if (cube) {
        for (int r = 0; r < 6; ++r) {
            for (int g = 0; g < 7; ++g) {
                for (int b = 0; b < 6; ++b) {
                    out.palette.push_back(cubeColour(r, g, b));
                }
            }
        }
    } else {
        for (const Rgb& c : colours) {
            lookup.emplace(static_cast<uint32_t>(c[0]) << 16 |
                               static_cast<uint32_t>(c[1]) << 8 | c[2],
                           static_cast<uint8_t>(out.palette.size()));
            out.palette.push_back(c);
        }
    }
    out.indices.reserve(static_cast<size_t>(frame.width) * frame.height);
    for (uint32_t y = 0; y < frame.height; ++y) {
        const uint8_t* row = frame.row(y);
        for (uint32_t x = 0; x < frame.width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4u;
            if (p[3] < 128) {
                out.indices.push_back(0);
            } else if (cube) {
                const int r = (p[0] * 5 + 127) / 255;
                const int g = (p[1] * 6 + 127) / 255;
                const int b = (p[2] * 5 + 127) / 255;
                out.indices.push_back(static_cast<uint8_t>(1 + (r * 7 + g) * 6 + b));
            } else {
                out.indices.push_back(lookup[rgbKey(p)]);
            }
        }
    }
    return out;
}

// Bits for a colour table of `count` entries: GIF tables are powers of two,
// two entries at the least.
int tableBits(size_t count) {
    int bits = 1;
    while ((static_cast<size_t>(1) << bits) < count) {
        ++bits;
    }
    return bits;
}

void writeTable(std::vector<uint8_t>& out, const std::vector<Rgb>& palette, int bits) {
    for (size_t i = 0; i < (static_cast<size_t>(1) << bits); ++i) {
        const Rgb c = i < palette.size() ? palette[i] : Rgb{ 0, 0, 0 };
        out.push_back(c[0]);
        out.push_back(c[1]);
        out.push_back(c[2]);
    }
}

// ------------------------------------------------------------------ APNG --

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
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
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    put32be(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32be(out, crc32(out.data() + start, out.size() - start));
}

// The IHDR and the compressed image data of a PNG stb wrote.
bool splitPng(const std::vector<uint8_t>& png, std::vector<uint8_t>* ihdr,
              std::vector<uint8_t>* idat) {
    if (png.size() < 8) {
        return false;
    }
    size_t at = 8;
    while (at + 12 <= png.size()) {
        const uint32_t length = read32be(png.data() + at);
        if (at + 12 + length > png.size()) {
            return false;
        }
        const uint8_t* type = png.data() + at + 4;
        const uint8_t* data = type + 4;
        if (std::equal(type, type + 4, "IHDR")) {
            ihdr->assign(data, data + length);
        } else if (std::equal(type, type + 4, "IDAT")) {
            idat->insert(idat->end(), data, data + length);
        }
        at += 12 + length;
    }
    return !ihdr->empty() && !idat->empty();
}

bool sameSize(const std::vector<ls::RasterBuffer>& frames, std::string* error) {
    if (frames.empty()) {
        if (error) { *error = "there are no frames to write"; }
        return false;
    }
    for (const ls::RasterBuffer& frame : frames) {
        if (frame.width != frames.front().width || frame.height != frames.front().height ||
            frame.width == 0 || frame.height == 0 || frame.width > 0xFFFF ||
            frame.height > 0xFFFF) {
            if (error) { *error = "the frames are not one size a file can hold"; }
            return false;
        }
    }
    return true;
}

int holdOf(const std::vector<int>& holdsMs, size_t i) {
    const int hold = i < holdsMs.size() ? holdsMs[i] : kDefaultFrameMs;
    return std::clamp(hold, kMinFrameMs, kMaxFrameMs);
}

} // namespace

std::vector<int> stepsToPlay(const Cycle& cycle) {
    std::vector<int> steps = cycle.frames;
    if (cycle.loop == LoopMode::PingPong && steps.size() > 2) {
        // There and back, without either end twice: 0 1 2 3 2 1.
        for (size_t i = steps.size() - 1; i-- > 1;) {
            steps.push_back(cycle.frames[i]);
        }
    }
    return steps;
}

bool encodeGif(const std::vector<ls::RasterBuffer>& frames, const std::vector<int>& holdsMs,
               bool loop, std::vector<uint8_t>* out, AnimationReport* report,
               std::string* error) {
    if (out == nullptr || !sameSize(frames, error)) {
        return false;
    }
    AnimationReport local;
    AnimationReport& said = report != nullptr ? *report : local;
    said = AnimationReport{};
    said.frames = frames.size();

    // One palette for the whole animation when the colours fit; one per frame
    // when each frame's do; and otherwise the cube.
    std::vector<Rgb> shared;
    const bool global = gatherColours(frames, &shared, &said.droppedAlpha);
    std::vector<IndexedFrame> indexed;
    for (const ls::RasterBuffer& frame : frames) {
        if (global) {
            indexed.push_back(index(frame, shared, false));
            continue;
        }
        std::vector<Rgb> own;
        bool partial = false;
        const bool fits = gatherColours({ frame }, &own, &partial);
        said.reducedColours = said.reducedColours || !fits;
        indexed.push_back(index(frame, own, !fits));
    }

    std::vector<uint8_t>& gif = *out;
    gif.clear();
    const char* header = "GIF89a";
    gif.insert(gif.end(), header, header + 6);
    put16(gif, frames.front().width);
    put16(gif, frames.front().height);
    if (global) {
        const int bits = tableBits(indexed.front().palette.size());
        gif.push_back(static_cast<uint8_t>(0x80 | 0x70 | (bits - 1)));
        gif.push_back(0);           // background: the transparent entry
        gif.push_back(0);           // square pixels
        writeTable(gif, indexed.front().palette, bits);
    } else {
        gif.push_back(0x70);
        gif.push_back(0);
        gif.push_back(0);
    }
    if (loop) {
        const uint8_t netscape[] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                     'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00 };
        gif.insert(gif.end(), netscape, netscape + sizeof(netscape));
    }

    for (size_t i = 0; i < indexed.size(); ++i) {
        // Hundredths of a second. Two is the least any browser honours --
        // below that most play it at a tenth -- so a faster hold is raised to
        // it rather than slowed to ten times its length.
        const int centis = std::max(2, (holdOf(holdsMs, i) + 5) / 10);
        const uint8_t control[] = { 0x21, 0xF9, 0x04,
                                    static_cast<uint8_t>((2 << 2) | 1),   // clear, transparent
                                    static_cast<uint8_t>(centis & 0xFF),
                                    static_cast<uint8_t>((centis >> 8) & 0xFF),
                                    0, 0 };
        gif.insert(gif.end(), control, control + sizeof(control));

        gif.push_back(0x2C);
        put16(gif, 0);
        put16(gif, 0);
        put16(gif, frames[i].width);
        put16(gif, frames[i].height);
        const int bits = tableBits(indexed[i].palette.size());
        if (global) {
            gif.push_back(0);
        } else {
            gif.push_back(static_cast<uint8_t>(0x80 | (bits - 1)));
            writeTable(gif, indexed[i].palette, bits);
        }
        const int minCode = std::max(2, bits);
        gif.push_back(static_cast<uint8_t>(minCode));
        const std::vector<uint8_t> data = lzw(indexed[i].indices, minCode);
        for (size_t at = 0; at < data.size(); at += 255) {
            const size_t length = std::min<size_t>(255, data.size() - at);
            gif.push_back(static_cast<uint8_t>(length));
            gif.insert(gif.end(), data.begin() + static_cast<long long>(at),
                       data.begin() + static_cast<long long>(at + length));
        }
        gif.push_back(0);
    }
    gif.push_back(0x3B);
    return true;
}

bool encodeApng(const std::vector<ls::RasterBuffer>& frames, const std::vector<int>& holdsMs,
                bool loop, std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr || !sameSize(frames, error)) {
        return false;
    }
    std::vector<uint8_t>& png = *out;
    png.clear();
    const uint8_t signature[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    png.insert(png.end(), signature, signature + 8);

    uint32_t sequence = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        std::vector<uint8_t> single;
        ExportSettings once;
        if (!encodeRasterToPng(frames[i], once, &single, error)) {
            return false;
        }
        std::vector<uint8_t> ihdr;
        std::vector<uint8_t> data;
        if (!splitPng(single, &ihdr, &data)) {
            if (error) { *error = "a frame could not be encoded"; }
            return false;
        }
        if (i == 0) {
            chunk(png, "IHDR", ihdr);
            std::vector<uint8_t> actl;
            put32be(actl, static_cast<uint32_t>(frames.size()));
            put32be(actl, loop ? 0u : 1u);            // plays: 0 is forever
            chunk(png, "acTL", actl);
        }
        std::vector<uint8_t> fctl;
        put32be(fctl, sequence++);
        put32be(fctl, frames[i].width);
        put32be(fctl, frames[i].height);
        put32be(fctl, 0);
        put32be(fctl, 0);
        const int hold = holdOf(holdsMs, i);
        fctl.push_back(static_cast<uint8_t>(hold >> 8));
        fctl.push_back(static_cast<uint8_t>(hold & 0xFF));
        fctl.push_back(static_cast<uint8_t>(1000 >> 8));
        fctl.push_back(static_cast<uint8_t>(1000 & 0xFF));
        fctl.push_back(0);        // dispose: none -- every frame covers the canvas
        fctl.push_back(0);        // blend: source, so transparency replaces
        chunk(png, "fcTL", fctl);
        if (i == 0) {
            chunk(png, "IDAT", data);
        } else {
            std::vector<uint8_t> fdat;
            put32be(fdat, sequence++);
            fdat.insert(fdat.end(), data.begin(), data.end());
            chunk(png, "fdAT", fdat);
        }
    }
    chunk(png, "IEND", {});
    return true;
}

const char* animationExtension(AnimationFormat format) {
    switch (format) {
        case AnimationFormat::Gif:         return ".gif";
        case AnimationFormat::Apng:        return ".png";
        case AnimationFormat::PngSequence: return ".png";
    }
    return ".gif";
}

bool exportAnimation(Document& doc, const std::vector<Frame>& frames, const Cycle& cycle,
                     const std::string& path, const AnimationSettings& settings,
                     AnimationReport* report, std::string* error) {
    const std::vector<int> steps = stepsToPlay(cycle);
    if (steps.empty()) {
        if (error) { *error = "there are no frames to write"; }
        return false;
    }
    if (settings.scale == 0 || settings.scale > ExportSettings::kMaxScale) {
        if (error) { *error = "that scale is out of range"; }
        return false;
    }
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        if (error) { *error = "this document has no canvas"; }
        return false;
    }
    const uint64_t pixels = static_cast<uint64_t>(size.value.x) * settings.scale *
                            static_cast<uint64_t>(size.value.y) * settings.scale;
    if (pixels * steps.size() > 256ull * 1024ull * 1024ull) {
        if (error) { *error = "the animation is too large at that scale"; }
        return false;
    }

    std::vector<ls::RasterBuffer> rasters;
    std::vector<int> holds;
    for (int step : steps) {
        if (step < 0 || step >= static_cast<int>(frames.size())) {
            continue;
        }
        const ls::CompileProfile profile =
            compileProfile(ls::CompileProfileType::Export,
                           static_cast<uint32_t>(size.value.x),
                           static_cast<uint32_t>(size.value.y));
        auto compiled = doc.engine().compileSprite(frames[static_cast<size_t>(step)].sprite,
                                                   profile);
        if (compiled.fail()) {
            if (error) { *error = "frame " + std::to_string(step + 1) + " would not compile"; }
            return false;
        }
        rasters.push_back(magnifyRaster(compiled.value.raster, settings.scale));
        holds.push_back(frames[static_cast<size_t>(step)].durationMs);
    }

    const bool loop = cycle.loop != LoopMode::Once;
    AnimationReport local;
    AnimationReport& said = report != nullptr ? *report : local;
    said = AnimationReport{};
    said.frames = rasters.size();

    if (settings.format == AnimationFormat::PngSequence) {
        // Numbered with enough digits for the count, so they sort.
        // The chosen name without its extension: "walk.png" -> "walk".
        std::string stem = path;
        const size_t dot = stem.find_last_of('.');
        const size_t slash = stem.find_last_of("/\\");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            stem.erase(dot);
        }
        const int digits = rasters.size() >= 100 ? 3 : 2;
        for (size_t i = 0; i < rasters.size(); ++i) {
            char number[16];
            std::snprintf(number, sizeof(number), "-%0*zu", digits, i + 1);
            std::vector<uint8_t> bytes;
            ExportSettings once;
            if (!encodeRasterToPng(rasters[i], once, &bytes, error) ||
                !writeFileAtomic(stem + number + ".png", bytes, error)) {
                return false;
            }
        }
        return true;
    }

    std::vector<uint8_t> bytes;
    const bool encoded = settings.format == AnimationFormat::Gif
        ? encodeGif(rasters, holds, loop, &bytes, &said, error)
        : encodeApng(rasters, holds, loop, &bytes, error);
    if (!encoded) {
        return false;
    }
    said.frames = rasters.size();
    return writeFileAtomic(path, bytes, error);
}

} // namespace fast
