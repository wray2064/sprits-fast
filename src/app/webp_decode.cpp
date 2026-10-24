// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/webp.h"

#include "app/image_io.h"
#include "app/webp_predict.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

// Lossless WebP (VP8L) read from the format specification, with no library.
// Everything it reads is somebody else's file, so every count is checked against
// what is left, every size against the caps in image_io.h, and a failure is a
// sentence, never a half-filled picture.

namespace fast {
namespace {

constexpr uint64_t kMaxPixels = kMaxReferencePixels;
constexpr int kMaxCodeBits = 15;

bool fail(std::string* error, const char* why) {
    if (error != nullptr) {
        *error = why;
    }
    return false;
}

uint32_t divRoundUp(uint32_t value, int bits) {
    return static_cast<uint32_t>((static_cast<uint64_t>(value) + (1ull << bits) - 1ull) >> bits);
}

// ---------------------------------------------------------------- bits --

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // `bits` from 0 to 32, least significant first.
    uint32_t read(int bits) {
        const uint32_t value = peek(bits);
        skip(bits);
        return value;
    }
    // Up to 32 bits, not consumed.
    uint32_t peek(int bits) {
        refill();
        return bits >= 32 ? static_cast<uint32_t>(accumulator_)
                          : static_cast<uint32_t>(accumulator_ & ((1ull << bits) - 1ull));
    }
    void skip(int bits) {
        accumulator_ >>= bits;
        count_ -= bits;
        consumed_ += static_cast<uint64_t>(bits);
    }
    // Whether more bits were taken than the data holds.
    bool pastEnd() const { return consumed_ > static_cast<uint64_t>(size_) * 8u; }

private:
    void refill() {
        while (count_ <= 56) {
            const uint64_t byte = position_ < size_ ? data_[position_] : 0u;
            ++position_;
            accumulator_ |= byte << count_;
            count_ += 8;
        }
    }

    const uint8_t* data_;
    size_t         size_;
    size_t         position_ = 0;
    uint64_t       accumulator_ = 0;
    int            count_ = 0;
    uint64_t       consumed_ = 0;
};

// ------------------------------------------------------- prefix codes --

class Huffman {
public:
    // A complete canonical code from these lengths, or one symbol that costs no
    // bits. Anything else is refused.
    bool build(const std::vector<uint8_t>& lengths) {
        std::fill(std::begin(count_), std::end(count_), 0);
        int used = 0;
        int only = -1;
        for (size_t s = 0; s < lengths.size(); ++s) {
            if (lengths[s] > kMaxCodeBits) {
                return false;
            }
            if (lengths[s] > 0) {
                ++count_[lengths[s]];
                ++used;
                only = static_cast<int>(s);
            }
        }
        if (used == 0) {
            return false;
        }
        single_ = -1;
        if (used == 1) {
            single_ = only;
            return true;
        }
        int left = 1;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            left = (left << 1) - count_[length];
            if (left < 0) {
                return false;                       // over-subscribed
            }
        }
        if (left != 0) {
            return false;                           // incomplete
        }
        int offset[kMaxCodeBits + 2] = {};
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            offset[length + 1] = offset[length] + count_[length];
        }
        symbols_.assign(static_cast<size_t>(used), 0);
        for (size_t s = 0; s < lengths.size(); ++s) {
            if (lengths[s] > 0) {
                symbols_[static_cast<size_t>(offset[lengths[s]]++)] = static_cast<uint16_t>(s);
            }
        }
        // A table for codes of eight bits or fewer, indexed by the next eight
        // bits of the stream (the code's first bit lowest).
        std::fill(fast_.begin(), fast_.end(), 0u);
        uint32_t code = 0;
        size_t at = 0;
        for (int length = 1; length <= 8; ++length) {
            for (int i = 0; i < count_[length]; ++i, ++code, ++at) {
                uint32_t reversed = 0;
                for (int b = 0; b < length; ++b) {
                    reversed |= ((code >> b) & 1u) << (length - 1 - b);
                }
                for (uint32_t index = reversed; index < 256u; index += 1u << length) {
                    fast_[index] = (static_cast<uint32_t>(length) << 16) | symbols_[at];
                }
            }
            code <<= 1;
        }
        return true;
    }

    // The next symbol, or -1 for bits that are no code.
    int decode(BitReader& bits) const {
        if (single_ >= 0) {
            return single_;
        }
        const uint32_t entry = fast_[bits.peek(8)];
        if (entry != 0) {
            bits.skip(static_cast<int>(entry >> 16));
            return static_cast<int>(entry & 0xFFFFu);
        }
        const uint32_t ahead = bits.peek(kMaxCodeBits);
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            code |= static_cast<int>((ahead >> (length - 1)) & 1u);
            const int n = count_[length];
            if (code - n < first) {
                bits.skip(length);
                return symbols_[static_cast<size_t>(index + (code - first))];
            }
            index += n;
            first += n;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

private:
    int                    count_[kMaxCodeBits + 1] = {};
    std::vector<uint16_t>  symbols_;
    std::array<uint32_t, 256> fast_{};
    int                    single_ = -1;
};

const int kCodeLengthOrder[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13,
                                   14, 15 };

// Lengths for an alphabet, read from the stream.
bool readLengths(BitReader& bits, int alphabet, std::vector<uint8_t>* lengths) {
    lengths->assign(static_cast<size_t>(alphabet), 0);
    if (bits.read(1) != 0) {                        // a simple code: one or two symbols
        const int count = static_cast<int>(bits.read(1)) + 1;
        const bool wide = bits.read(1) != 0;
        const int first = static_cast<int>(bits.read(wide ? 8 : 1));
        if (first >= alphabet) {
            return false;
        }
        (*lengths)[static_cast<size_t>(first)] = 1;
        if (count == 2) {
            const int second = static_cast<int>(bits.read(8));
            if (second >= alphabet) {
                return false;
            }
            (*lengths)[static_cast<size_t>(second)] = 1;
        }
        return !bits.pastEnd();
    }

    std::vector<uint8_t> metaLengths(19, 0);
    const int used = static_cast<int>(bits.read(4)) + 4;
    for (int i = 0; i < used; ++i) {
        metaLengths[static_cast<size_t>(kCodeLengthOrder[i])] = static_cast<uint8_t>(bits.read(3));
    }
    Huffman meta;
    if (!meta.build(metaLengths)) {
        return false;
    }
    int maxSymbol = alphabet;
    if (bits.read(1) != 0) {
        const int lengthBits = 2 + 2 * static_cast<int>(bits.read(3));
        maxSymbol = 2 + static_cast<int>(bits.read(lengthBits));
        if (maxSymbol > alphabet) {
            return false;
        }
    }
    int symbol = 0;
    int previous = 8;
    while (symbol < alphabet) {
        if (maxSymbol-- == 0) {
            break;
        }
        const int code = meta.decode(bits);
        if (code < 0 || bits.pastEnd()) {
            return false;
        }
        if (code < 16) {
            (*lengths)[static_cast<size_t>(symbol++)] = static_cast<uint8_t>(code);
            if (code != 0) {
                previous = code;
            }
            continue;
        }
        const int extraBits = code == 16 ? 2 : code == 17 ? 3 : 7;
        const int offset = code == 18 ? 11 : 3;
        const int repeat = static_cast<int>(bits.read(extraBits)) + offset;
        if (symbol + repeat > alphabet) {
            return false;
        }
        const uint8_t value = code == 16 ? static_cast<uint8_t>(previous) : uint8_t{0};
        for (int i = 0; i < repeat; ++i) {
            (*lengths)[static_cast<size_t>(symbol++)] = value;
        }
    }
    return !bits.pastEnd();
}

// ------------------------------------------------------------ pictures --

struct CodeGroup {
    Huffman green, red, blue, alpha, distance;
};

// (x, y) offsets for the distance codes 1 to 120; a distance is x + y * width.
const int8_t kDistanceMap[120][2] = {
    {0, 1},  {1, 0},  {1, 1},  {-1, 1}, {0, 2},  {2, 0},  {1, 2},  {-1, 2},
    {2, 1},  {-2, 1}, {2, 2},  {-2, 2}, {0, 3},  {3, 0},  {1, 3},  {-1, 3},
    {3, 1},  {-3, 1}, {2, 3},  {-2, 3}, {3, 2},  {-3, 2}, {0, 4},  {4, 0},
    {1, 4},  {-1, 4}, {4, 1},  {-4, 1}, {3, 3},  {-3, 3}, {2, 4},  {-2, 4},
    {4, 2},  {-4, 2}, {0, 5},  {3, 4},  {-3, 4}, {4, 3},  {-4, 3}, {5, 0},
    {1, 5},  {-1, 5}, {5, 1},  {-5, 1}, {2, 5},  {-2, 5}, {5, 2},  {-5, 2},
    {4, 4},  {-4, 4}, {3, 5},  {-3, 5}, {5, 3},  {-5, 3}, {0, 6},  {6, 0},
    {1, 6},  {-1, 6}, {6, 1},  {-6, 1}, {2, 6},  {-2, 6}, {6, 2},  {-6, 2},
    {4, 5},  {-4, 5}, {5, 4},  {-5, 4}, {3, 6},  {-3, 6}, {6, 3},  {-6, 3},
    {0, 7},  {7, 0},  {1, 7},  {-1, 7}, {5, 5},  {-5, 5}, {7, 1},  {-7, 1},
    {4, 6},  {-4, 6}, {6, 4},  {-6, 4}, {2, 7},  {-2, 7}, {7, 2},  {-7, 2},
    {3, 7},  {-3, 7}, {7, 3},  {-7, 3}, {5, 6},  {-5, 6}, {6, 5},  {-6, 5},
    {8, 0},  {4, 7},  {-4, 7}, {7, 4},  {-7, 4}, {8, 1},  {8, 2},  {6, 6},
    {-6, 6}, {8, 3},  {5, 7},  {-5, 7}, {7, 5},  {-7, 5}, {8, 4},  {6, 7},
    {-6, 7}, {7, 6},  {-7, 6}, {8, 5},  {7, 7},  {-7, 7}, {8, 6},  {8, 7},
};

uint32_t copyValue(int prefix, BitReader& bits) {
    if (prefix < 4) {
        return static_cast<uint32_t>(prefix) + 1u;
    }
    const int extraBits = (prefix - 2) >> 1;
    const uint32_t offset = static_cast<uint32_t>(2 + (prefix & 1)) << extraBits;
    return offset + bits.read(extraBits) + 1u;
}

uint32_t cacheKey(uint32_t argb, int cacheBits) {
    return (0x1e35a7bdu * argb) >> (32 - cacheBits);
}

// An entropy-coded image of width x height. The main picture may use several
// sets of prefix codes by position; the pictures that carry a transform's data
// use one.
bool readImage(BitReader& bits, uint32_t width, uint32_t height, bool main,
               std::vector<uint32_t>* out, std::string* error) {
    const uint64_t total = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || total > kMaxPixels) {
        return fail(error, "the WebP picture is larger than this build will open");
    }
    int cacheBits = 0;
    if (bits.read(1) != 0) {
        cacheBits = static_cast<int>(bits.read(4));
        if (cacheBits < 1 || cacheBits > 11) {
            return fail(error, "the WebP colour cache has a size it may not have");
        }
    }

    std::vector<uint32_t> meta;
    int metaBits = 0;
    uint32_t metaWidth = 0;
    uint32_t groupCount = 1;
    if (main && bits.read(1) != 0) {
        metaBits = static_cast<int>(bits.read(3)) + 2;
        metaWidth = divRoundUp(width, metaBits);
        if (!readImage(bits, metaWidth, divRoundUp(height, metaBits), false, &meta, error)) {
            return false;
        }
        for (uint32_t& p : meta) {
            p = (p >> 8) & 0xFFFFu;
            groupCount = std::max(groupCount, p + 1u);
        }
    }

    // Every group's codes are in the stream; only the ones a position names are
    // kept, so a file that claims thousands costs only what it uses.
    std::vector<bool> used(groupCount, metaBits == 0);
    for (uint32_t p : meta) {
        used[p] = true;
    }
    std::vector<int> slotOf(groupCount, -1);
    std::vector<CodeGroup> groups;
    const int greenAlphabet = 256 + 24 + (cacheBits > 0 ? (1 << cacheBits) : 0);
    for (uint32_t g = 0; g < groupCount; ++g) {
        std::vector<uint8_t> lengths[5];
        const int alphabets[5] = { greenAlphabet, 256, 256, 256, 40 };
        for (int i = 0; i < 5; ++i) {
            if (!readLengths(bits, alphabets[i], &lengths[i])) {
                return fail(error, "a WebP prefix code is damaged");
            }
        }
        if (!used[g]) {
            continue;
        }
        slotOf[g] = static_cast<int>(groups.size());
        groups.emplace_back();
        CodeGroup& group = groups.back();
        if (!group.green.build(lengths[0]) || !group.red.build(lengths[1]) ||
            !group.blue.build(lengths[2]) || !group.alpha.build(lengths[3]) ||
            !group.distance.build(lengths[4])) {
            return fail(error, "a WebP prefix code is not a code");
        }
    }

    std::vector<uint32_t> cache(cacheBits > 0 ? (1u << cacheBits) : 0u, 0u);
    out->assign(static_cast<size_t>(total), 0u);
    uint32_t* pixels = out->data();
    size_t position = 0;
    const size_t end = static_cast<size_t>(total);
    while (position < end) {
        const uint32_t x = static_cast<uint32_t>(position % width);
        const uint32_t y = static_cast<uint32_t>(position / width);
        const CodeGroup& group =
            groups[static_cast<size_t>(
                slotOf[metaBits == 0 ? 0 : meta[(y >> metaBits) * metaWidth + (x >> metaBits)]])];
        const int symbol = group.green.decode(bits);
        if (symbol < 0 || bits.pastEnd()) {
            return fail(error, "the WebP picture ends early");
        }
        if (symbol < 256) {
            const int red = group.red.decode(bits);
            const int blue = group.blue.decode(bits);
            const int alpha = group.alpha.decode(bits);
            if (red < 0 || blue < 0 || alpha < 0) {
                return fail(error, "the WebP picture is damaged");
            }
            const uint32_t argb = (static_cast<uint32_t>(alpha) << 24) |
                                  (static_cast<uint32_t>(red) << 16) |
                                  (static_cast<uint32_t>(symbol) << 8) |
                                  static_cast<uint32_t>(blue);
            pixels[position++] = argb;
            if (cacheBits > 0) {
                cache[cacheKey(argb, cacheBits)] = argb;
            }
        } else if (symbol < 256 + 24) {
            const uint32_t length = copyValue(symbol - 256, bits);
            const int distanceSymbol = group.distance.decode(bits);
            if (distanceSymbol < 0 || bits.pastEnd()) {
                return fail(error, "the WebP picture ends early");
            }
            const uint32_t code = copyValue(distanceSymbol, bits);
            int64_t distance = 0;
            if (code > 120) {
                distance = static_cast<int64_t>(code) - 120;
            } else {
                distance = kDistanceMap[code - 1][0] +
                           static_cast<int64_t>(kDistanceMap[code - 1][1]) * width;
                distance = std::max<int64_t>(distance, 1);
            }
            if (static_cast<uint64_t>(distance) > position || position + length > end) {
                return fail(error, "a WebP copy reaches outside the picture");
            }
            for (uint32_t i = 0; i < length; ++i, ++position) {
                const uint32_t argb = pixels[position - static_cast<size_t>(distance)];
                pixels[position] = argb;
                if (cacheBits > 0) {
                    cache[cacheKey(argb, cacheBits)] = argb;
                }
            }
        } else {
            const size_t index = static_cast<size_t>(symbol) - (256 + 24);
            if (cacheBits == 0 || index >= cache.size()) {
                return fail(error, "the WebP picture names a colour that was never cached");
            }
            const uint32_t argb = cache[index];
            pixels[position++] = argb;
            cache[cacheKey(argb, cacheBits)] = argb;
        }
    }
    return !bits.pastEnd() || fail(error, "the WebP picture ends early");
}

// -------------------------------------------------------- transforms --

enum TransformType { kPredictor = 0, kCrossColour = 1, kSubtractGreen = 2, kColourIndexing = 3 };

struct Transform {
    int                   type = 0;
    int                   bits = 0;
    uint32_t              width = 0;          // the picture's width once this is undone
    std::vector<uint32_t> data;
};

using webp_detail::addPixels;
using webp_detail::predict;

void undoPredictor(const Transform& t, uint32_t height, std::vector<uint32_t>* pixels) {
    const uint32_t width = t.width;
    const uint32_t blocksAcross = divRoundUp(width, t.bits);
    uint32_t* p = pixels->data();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            uint32_t guess = 0;
            if (y == 0) {
                guess = x == 0 ? 0xFF000000u : p[i - 1];
            } else if (x == 0) {
                guess = p[i - width];
            } else {
                const int mode = static_cast<int>(
                    (t.data[(y >> t.bits) * blocksAcross + (x >> t.bits)] >> 8) & 0xFu);
                guess = predict(mode, p[i - 1], p[i - width], p[i - width + 1], p[i - width - 1]);
            }
            p[i] = addPixels(p[i], guess);
        }
    }
}

int colourDelta(int8_t t, int8_t c) {
    return (static_cast<int>(t) * static_cast<int>(c)) >> 5;
}

void undoCrossColour(const Transform& t, uint32_t height, std::vector<uint32_t>* pixels) {
    const uint32_t width = t.width;
    const uint32_t blocksAcross = divRoundUp(width, t.bits);
    uint32_t* p = pixels->data();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t element = t.data[(y >> t.bits) * blocksAcross + (x >> t.bits)];
            const int8_t greenToRed = static_cast<int8_t>(element & 0xFFu);
            const int8_t greenToBlue = static_cast<int8_t>((element >> 8) & 0xFFu);
            const int8_t redToBlue = static_cast<int8_t>((element >> 16) & 0xFFu);
            uint32_t& pixel = p[static_cast<size_t>(y) * width + x];
            const int8_t green = static_cast<int8_t>((pixel >> 8) & 0xFFu);
            int red = static_cast<int>((pixel >> 16) & 0xFFu);
            int blue = static_cast<int>(pixel & 0xFFu);
            red += colourDelta(greenToRed, green);
            red &= 0xFF;
            blue += colourDelta(greenToBlue, green);
            blue += colourDelta(redToBlue, static_cast<int8_t>(red));
            blue &= 0xFF;
            pixel = (pixel & 0xFF00FF00u) | (static_cast<uint32_t>(red) << 16) |
                    static_cast<uint32_t>(blue);
        }
    }
}

void undoSubtractGreen(std::vector<uint32_t>* pixels) {
    for (uint32_t& pixel : *pixels) {
        const uint32_t green = (pixel >> 8) & 0xFFu;
        const uint32_t redBlue = (((pixel & 0x00FF00FFu) + ((green << 16) | green)) & 0x00FF00FFu);
        pixel = (pixel & 0xFF00FF00u) | redBlue;
    }
}

// The packed indices become colours; the picture is its full width again.
void undoColourIndexing(const Transform& t, uint32_t height, std::vector<uint32_t>* pixels) {
    const int packing = t.bits;                      // log2 of indices per pixel
    const uint32_t width = t.width;
    const uint32_t packedWidth = divRoundUp(width, packing);
    const int perIndex = 8 >> packing;
    const uint32_t mask = (1u << perIndex) - 1u;
    std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t packed =
                (*pixels)[static_cast<size_t>(y) * packedWidth + (x >> packing)] >> 8;
            const uint32_t index =
                (packed >> (perIndex * static_cast<int>(x & ((1u << packing) - 1u)))) & mask;
            out[static_cast<size_t>(y) * width + x] =
                index < t.data.size() ? t.data[index] : 0u;
        }
    }
    *pixels = std::move(out);
}

bool decodeStream(const uint8_t* data, size_t size, ls::RasterBuffer* out, std::string* error) {
    BitReader bits(data, size);
    if (size < 5 || bits.read(8) != 0x2F) {
        return fail(error, "the WebP picture does not start as lossless data does");
    }
    const uint32_t width = bits.read(14) + 1u;
    const uint32_t height = bits.read(14) + 1u;
    bits.read(1);                                    // alpha is used: a hint only
    if (bits.read(3) != 0) {
        return fail(error, "this WebP picture is of a version this build does not read");
    }
    if (static_cast<uint64_t>(width) * height > kMaxPixels) {
        return fail(error, "the WebP picture is larger than this build will open");
    }

    std::vector<Transform> transforms;
    bool seen[4] = {};
    uint32_t codedWidth = width;
    while (bits.read(1) != 0) {
        Transform t;
        t.type = static_cast<int>(bits.read(2));
        if (seen[t.type]) {
            return fail(error, "a WebP transform is used twice");
        }
        seen[t.type] = true;
        t.width = codedWidth;
        switch (t.type) {
            case kPredictor:
            case kCrossColour:
                t.bits = static_cast<int>(bits.read(3)) + 2;
                if (!readImage(bits, divRoundUp(codedWidth, t.bits), divRoundUp(height, t.bits),
                               false, &t.data, error)) {
                    return false;
                }
                break;
            case kColourIndexing: {
                const uint32_t colours = bits.read(8) + 1u;
                if (!readImage(bits, colours, 1, false, &t.data, error)) {
                    return false;
                }
                for (size_t i = 1; i < t.data.size(); ++i) {
                    t.data[i] = addPixels(t.data[i], t.data[i - 1]);
                }
                t.bits = colours <= 2 ? 3 : colours <= 4 ? 2 : colours <= 16 ? 1 : 0;
                codedWidth = divRoundUp(codedWidth, t.bits);
                break;
            }
            default:
                break;
        }
        transforms.push_back(std::move(t));
        if (bits.pastEnd()) {
            return fail(error, "the WebP picture ends early");
        }
    }

    std::vector<uint32_t> pixels;
    if (!readImage(bits, codedWidth, height, true, &pixels, error)) {
        return false;
    }
    for (size_t i = transforms.size(); i-- > 0;) {
        const Transform& t = transforms[i];
        switch (t.type) {
            case kPredictor:       undoPredictor(t, height, &pixels); break;
            case kCrossColour:     undoCrossColour(t, height, &pixels); break;
            case kSubtractGreen:   undoSubtractGreen(&pixels); break;
            default:               undoColourIndexing(t, height, &pixels); break;
        }
    }

    ls::RasterBuffer raster = ls::makeRaster(width, height);
    if (raster.pixels.empty()) {
        return fail(error, "there was not enough memory for the image");
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = raster.row(y);
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t argb = pixels[static_cast<size_t>(y) * width + x];
            row[x * 4 + 0] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
            row[x * 4 + 1] = static_cast<uint8_t>((argb >> 8) & 0xFFu);
            row[x * 4 + 2] = static_cast<uint8_t>(argb & 0xFFu);
            row[x * 4 + 3] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
        }
    }
    *out = std::move(raster);
    return true;
}

// ----------------------------------------------------------- container --

uint32_t le32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool tagIs(const uint8_t* p, const char* tag) {
    return std::memcmp(p, tag, 4) == 0;
}

// The first lossless picture in a run of chunks: a still's own, or the first
// frame of an animation. Lossy data (VP8) is named and refused.
bool findPicture(const uint8_t* data, size_t size, bool inFrame, const uint8_t** picture,
                 size_t* pictureSize, std::string* error) {
    size_t at = 0;
    while (at + 8 <= size) {
        const uint8_t* chunk = data + at;
        const size_t length = le32(chunk + 4);
        if (length > size - at - 8) {
            return fail(error, "the WebP file is cut short");
        }
        const uint8_t* payload = chunk + 8;
        if (tagIs(chunk, "VP8L")) {
            *picture = payload;
            *pictureSize = length;
            return true;
        }
        if (tagIs(chunk, "VP8 ")) {
            return fail(error, "this is a lossy WebP picture; this build reads lossless WebP only");
        }
        if (!inFrame && tagIs(chunk, "ANMF") && length >= 16) {
            return findPicture(payload + 16, length - 16, true, picture, pictureSize, error);
        }
        at += 8 + length + (length & 1u);
    }
    return fail(error, "the WebP file holds no picture");
}

bool locate(const std::vector<uint8_t>& bytes, const uint8_t** picture, size_t* pictureSize,
            std::string* error) {
    if (!looksLikeWebp(bytes)) {
        return fail(error, "not a WebP file");
    }
    const size_t declared = static_cast<size_t>(le32(bytes.data() + 4)) + 8u;
    const size_t end = std::min(bytes.size(), declared);
    if (end < 12) {
        return fail(error, "the WebP file is cut short");
    }
    return findPicture(bytes.data() + 12, end - 12, false, picture, pictureSize, error);
}

} // namespace

bool looksLikeWebp(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
}

bool webpSize(const std::vector<uint8_t>& bytes, uint32_t* width, uint32_t* height,
              std::string* error) {
    const uint8_t* picture = nullptr;
    size_t pictureSize = 0;
    if (!locate(bytes, &picture, &pictureSize, error)) {
        return false;
    }
    if (pictureSize < 5 || picture[0] != 0x2F) {
        return fail(error, "the WebP picture does not start as lossless data does");
    }
    const uint32_t packed = le32(picture + 1);
    if (width != nullptr) {
        *width = (packed & 0x3FFFu) + 1u;
    }
    if (height != nullptr) {
        *height = ((packed >> 14) & 0x3FFFu) + 1u;
    }
    return true;
}

bool decodeWebp(const std::vector<uint8_t>& bytes, ls::RasterBuffer* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    const uint8_t* picture = nullptr;
    size_t pictureSize = 0;
    if (!locate(bytes, &picture, &pictureSize, error)) {
        return false;
    }
    return decodeStream(picture, pictureSize, out, error);
}

} // namespace fast
