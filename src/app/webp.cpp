// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/webp.h"

#include <algorithm>
#include <map>
#include <queue>
#include <utility>

namespace fast {

namespace {

// ------------------------------------------------------------------ bits --

// VP8L is written least significant bit first.
class BitWriter {
public:
    void put(uint32_t value, int bits) {
        if (bits <= 0) {
            return;
        }
        accumulator_ |= (static_cast<uint64_t>(value) & ((1ull << bits) - 1ull)) << used_;
        used_ += bits;
        while (used_ >= 8) {
            bytes_.push_back(static_cast<uint8_t>(accumulator_ & 0xFF));
            accumulator_ >>= 8;
            used_ -= 8;
        }
    }
    std::vector<uint8_t> finish() {
        if (used_ > 0) {
            bytes_.push_back(static_cast<uint8_t>(accumulator_ & 0xFF));
        }
        accumulator_ = 0;
        used_ = 0;
        return std::move(bytes_);
    }

private:
    std::vector<uint8_t> bytes_;
    uint64_t accumulator_ = 0;
    int used_ = 0;
};

// ---------------------------------------------------------- prefix codes --

constexpr int kMaxCodeLength = 15;

// Huffman code lengths for these counts, none longer than `limit`. Too long
// a code is fixed the plain way: flatten the counts and build again.
std::vector<uint8_t> codeLengths(std::vector<uint32_t> counts, int limit) {
    const size_t n = counts.size();
    std::vector<uint8_t> lengths(n, 0);
    for (;;) {
        struct Node { uint64_t weight; int left; int right; int symbol; };
        std::vector<Node> nodes;
        using Entry = std::pair<uint64_t, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        for (size_t s = 0; s < n; ++s) {
            if (counts[s] > 0) {
                nodes.push_back({ counts[s], -1, -1, static_cast<int>(s) });
                queue.push({ counts[s], static_cast<int>(nodes.size() - 1) });
            }
        }
        std::fill(lengths.begin(), lengths.end(), uint8_t{0});
        if (nodes.empty()) {
            return lengths;
        }
        if (nodes.size() == 1) {
            lengths[static_cast<size_t>(nodes[0].symbol)] = 1;
            return lengths;
        }
        while (queue.size() > 1) {
            const Entry a = queue.top();
            queue.pop();
            const Entry b = queue.top();
            queue.pop();
            nodes.push_back({ a.first + b.first, a.second, b.second, -1 });
            queue.push({ a.first + b.first, static_cast<int>(nodes.size() - 1) });
        }
        // Depths, iteratively.
        int deepest = 0;
        std::vector<std::pair<int, int>> stack{ { queue.top().second, 0 } };
        while (!stack.empty()) {
            const auto [index, depth] = stack.back();
            stack.pop_back();
            const Node& node = nodes[static_cast<size_t>(index)];
            if (node.symbol >= 0) {
                lengths[static_cast<size_t>(node.symbol)] = static_cast<uint8_t>(depth);
                deepest = std::max(deepest, depth);
            } else {
                stack.push_back({ node.left, depth + 1 });
                stack.push_back({ node.right, depth + 1 });
            }
        }
        if (deepest <= limit) {
            return lengths;
        }
        for (uint32_t& c : counts) {
            if (c > 0) {
                c = std::max<uint32_t>(1u, c >> 1);
            }
        }
    }
}

// A prefix code: every symbol's length and its bits, already reversed for an
// LSB-first writer. A code with a single symbol costs nothing to write.
struct PrefixCode {
    std::vector<uint8_t>  lengths;
    std::vector<uint16_t> codes;
    int                   used = 0;

    void write(BitWriter& bits, int symbol) const {
        if (used > 1) {
            bits.put(codes[static_cast<size_t>(symbol)], lengths[static_cast<size_t>(symbol)]);
        }
    }
};

PrefixCode canonical(const std::vector<uint8_t>& lengths) {
    PrefixCode code;
    code.lengths = lengths;
    code.codes.assign(lengths.size(), 0);
    int count[kMaxCodeLength + 1] = {};
    for (uint8_t length : lengths) {
        if (length > 0) {
            ++count[length];
            ++code.used;
        }
    }
    // The canonical assignment, as DEFLATE's: shorter codes first, and within
    // a length in symbol order. count[0] is zero -- unused symbols have no code.
    int next[kMaxCodeLength + 2] = {};
    int value = 0;
    for (int bits = 1; bits <= kMaxCodeLength; ++bits) {
        value = (value + count[bits - 1]) << 1;
        next[bits] = value;
    }
    for (size_t s = 0; s < lengths.size(); ++s) {
        const int length = lengths[s];
        if (length == 0) {
            continue;
        }
        const int c = next[length]++;
        // Reverse, since the decoder takes the code's first bit first.
        uint16_t reversed = 0;
        for (int b = 0; b < length; ++b) {
            reversed = static_cast<uint16_t>(reversed | (((c >> b) & 1) << (length - 1 - b)));
        }
        code.codes[s] = reversed;
    }
    return code;
}

const int kCodeLengthOrder[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13,
                                   14, 15 };

// Writes a prefix code for these counts and returns it. Two symbols or fewer,
// both below 256, go as a "simple" code; everything else as a normal one,
// its lengths run-length coded through a code of their own.
PrefixCode writePrefixCode(BitWriter& bits, const std::vector<uint32_t>& counts) {
    std::vector<int> present;
    for (size_t s = 0; s < counts.size(); ++s) {
        if (counts[s] > 0) {
            present.push_back(static_cast<int>(s));
        }
    }
    if (present.size() <= 2 &&
        std::all_of(present.begin(), present.end(), [](int s) { return s < 256; })) {
        if (present.empty()) {
            present.push_back(0);        // an alphabet nothing uses: one symbol, no bits
        }
        bits.put(1, 1);                                   // simple
        bits.put(static_cast<uint32_t>(present.size() - 1), 1);
        const bool wide = present[0] > 1;
        bits.put(wide ? 1u : 0u, 1);
        bits.put(static_cast<uint32_t>(present[0]), wide ? 8 : 1);
        if (present.size() == 2) {
            bits.put(static_cast<uint32_t>(present[1]), 8);
        }
        std::vector<uint8_t> lengths(counts.size(), 0);
        for (int s : present) {
            lengths[static_cast<size_t>(s)] = 1;
        }
        return canonical(lengths);
    }

    const std::vector<uint8_t> lengths = codeLengths(counts, kMaxCodeLength);

    // The lengths as code-length symbols: 0..15 as themselves, 16 repeating
    // the length just written 3..6 times, 17 and 18 runs of zeros.
    struct Token { int symbol; int extra; int extraBits; };
    std::vector<Token> tokens;
    for (size_t i = 0; i < lengths.size();) {
        const int value = lengths[i];
        size_t run = 1;
        while (i + run < lengths.size() && lengths[i + run] == value) {
            ++run;
        }
        size_t left = run;
        if (value == 0) {
            while (left >= 11) {
                const size_t take = std::min<size_t>(left, 138);
                tokens.push_back({ 18, static_cast<int>(take - 11), 7 });
                left -= take;
            }
            if (left >= 3) {
                tokens.push_back({ 17, static_cast<int>(left - 3), 3 });
                left = 0;
            }
            while (left > 0) {
                tokens.push_back({ 0, 0, 0 });
                --left;
            }
        } else {
            tokens.push_back({ value, 0, 0 });
            --left;
            while (left >= 3) {
                const size_t take = std::min<size_t>(left, 6);
                tokens.push_back({ 16, static_cast<int>(take - 3), 2 });
                left -= take;
            }
            while (left > 0) {
                tokens.push_back({ value, 0, 0 });
                --left;
            }
        }
        i += run;
    }
    std::vector<uint32_t> tokenCounts(19, 0);
    for (const Token& t : tokens) {
        ++tokenCounts[static_cast<size_t>(t.symbol)];
    }
    const std::vector<uint8_t> tokenLengths = codeLengths(tokenCounts, 7);
    const PrefixCode tokenCode = canonical(tokenLengths);

    int written = 19;
    while (written > 4 && tokenLengths[static_cast<size_t>(kCodeLengthOrder[written - 1])] == 0) {
        --written;
    }
    bits.put(0, 1);                                       // normal
    bits.put(static_cast<uint32_t>(written - 4), 4);
    for (int i = 0; i < written; ++i) {
        bits.put(tokenLengths[static_cast<size_t>(kCodeLengthOrder[i])], 3);
    }
    bits.put(0, 1);                                       // every symbol's length follows
    for (const Token& t : tokens) {
        tokenCode.write(bits, t.symbol);
        bits.put(static_cast<uint32_t>(t.extra), t.extraBits);
    }
    return canonical(lengths);
}

// ------------------------------------------------------------ the image --

// Lengths and distances share one prefix scheme: small values as themselves,
// larger ones as a prefix saying how many extra bits follow.
void prefixOf(uint32_t value, int* prefix, int* extraBits, uint32_t* extra) {
    const uint32_t v = value - 1;
    if (v < 4) {
        *prefix = static_cast<int>(v);
        *extraBits = 0;
        *extra = 0;
        return;
    }
    int high = 31;
    while (((v >> high) & 1u) == 0) {
        --high;
    }
    const uint32_t second = (v >> (high - 1)) & 1u;
    *prefix = 2 * high + static_cast<int>(second);
    *extraBits = high - 1;
    *extra = v & ((1u << (high - 1)) - 1u);
}

struct Symbol {
    bool     copy = false;
    uint32_t argb = 0;          // a literal
    uint32_t length = 0;        // a copy: how many pixels,
    uint32_t distanceCode = 0;  // and from where
};

constexpr uint32_t kMaxCopy = 4096;

// The pixels as literals and copies from the pixel before (distance code 2)
// or the pixel above (distance code 1).
std::vector<Symbol> tokenise(const std::vector<uint32_t>& pixels, uint32_t width, bool copies) {
    std::vector<Symbol> out;
    out.reserve(pixels.size());
    const size_t n = pixels.size();
    size_t i = 0;
    while (i < n) {
        size_t bestLength = 0;
        uint32_t bestCode = 0;
        if (copies) {
            if (i >= 1) {
                size_t run = 0;
                while (i + run < n && run < kMaxCopy && pixels[i + run] == pixels[i + run - 1]) {
                    ++run;
                }
                bestLength = run;
                bestCode = 2;
            }
            if (i >= width) {
                size_t run = 0;
                while (i + run < n && run < kMaxCopy && pixels[i + run] == pixels[i + run - width]) {
                    ++run;
                }
                if (run > bestLength) {
                    bestLength = run;
                    bestCode = 1;
                }
            }
        }
        if (bestLength >= 3) {
            Symbol s;
            s.copy = true;
            s.length = static_cast<uint32_t>(bestLength);
            s.distanceCode = bestCode;
            out.push_back(s);
            i += bestLength;
        } else {
            Symbol s;
            s.argb = pixels[i];
            out.push_back(s);
            ++i;
        }
    }
    return out;
}

// An image's data: its five prefix codes, then its symbols.
void writeImageData(BitWriter& bits, const std::vector<uint32_t>& pixels, uint32_t width,
                    bool copies) {
    const std::vector<Symbol> symbols = tokenise(pixels, width, copies);
    std::vector<uint32_t> green(256 + 24, 0), red(256, 0), blue(256, 0), alpha(256, 0),
        distance(40, 0);
    for (const Symbol& s : symbols) {
        if (s.copy) {
            int prefix = 0, extraBits = 0;
            uint32_t extra = 0;
            prefixOf(s.length, &prefix, &extraBits, &extra);
            ++green[static_cast<size_t>(256 + prefix)];
            prefixOf(s.distanceCode, &prefix, &extraBits, &extra);
            ++distance[static_cast<size_t>(prefix)];
        } else {
            ++green[(s.argb >> 8) & 0xFF];
            ++red[(s.argb >> 16) & 0xFF];
            ++blue[s.argb & 0xFF];
            ++alpha[(s.argb >> 24) & 0xFF];
        }
    }
    const PrefixCode g = writePrefixCode(bits, green);
    const PrefixCode r = writePrefixCode(bits, red);
    const PrefixCode b = writePrefixCode(bits, blue);
    const PrefixCode a = writePrefixCode(bits, alpha);
    const PrefixCode d = writePrefixCode(bits, distance);
    for (const Symbol& s : symbols) {
        if (s.copy) {
            int prefix = 0, extraBits = 0;
            uint32_t extra = 0;
            prefixOf(s.length, &prefix, &extraBits, &extra);
            g.write(bits, 256 + prefix);
            bits.put(extra, extraBits);
            prefixOf(s.distanceCode, &prefix, &extraBits, &extra);
            d.write(bits, prefix);
            bits.put(extra, extraBits);
        } else {
            g.write(bits, static_cast<int>((s.argb >> 8) & 0xFF));
            r.write(bits, static_cast<int>((s.argb >> 16) & 0xFF));
            b.write(bits, static_cast<int>(s.argb & 0xFF));
            a.write(bits, static_cast<int>((s.argb >> 24) & 0xFF));
        }
    }
}

// The VP8L bitstream for one picture.
bool vp8l(const ls::RasterBuffer& image, std::vector<uint8_t>* out, std::string* error) {
    if (image.empty() || image.width > 16384 || image.height > 16384) {
        if (error != nullptr) {
            *error = "WebP pictures are 1 to 16384 pixels on a side";
        }
        return false;
    }
    const uint32_t w = image.width;
    const uint32_t h = image.height;
    std::vector<uint32_t> argb;
    argb.reserve(static_cast<size_t>(w) * h);
    bool translucent = false;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = row + x * 4;
            argb.push_back((static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[0]) << 16) |
                           (static_cast<uint32_t>(p[1]) << 8) | p[2]);
            translucent = translucent || p[3] != 255;
        }
    }

    BitWriter bits;
    bits.put(0x2F, 8);                       // signature
    bits.put(w - 1, 14);
    bits.put(h - 1, 14);
    bits.put(translucent ? 1u : 0u, 1);
    bits.put(0, 3);                          // version

    // The colours, if there are few enough for the indexing transform.
    std::map<uint32_t, uint32_t> index;
    for (uint32_t c : argb) {
        if (index.size() > 256) {
            break;
        }
        index.emplace(c, 0);
    }
    uint32_t imageWidth = w;
    std::vector<uint32_t> coded;
    if (index.size() <= 256) {
        std::vector<uint32_t> palette;
        palette.reserve(index.size());
        for (auto& [colour, slot] : index) {
            slot = static_cast<uint32_t>(palette.size());
            palette.push_back(colour);
        }
        const size_t colours = palette.size();
        const int xbits = colours <= 2 ? 3 : colours <= 4 ? 2 : colours <= 16 ? 1 : 0;
        const int bitsPerIndex = 8 >> xbits;
        bits.put(1, 1);                      // a transform
        bits.put(3, 2);                      // colour indexing
        bits.put(static_cast<uint32_t>(colours - 1), 8);
        // The table, each entry as its difference from the one before.
        std::vector<uint32_t> table(colours);
        for (size_t i = 0; i < colours; ++i) {
            const uint32_t now = palette[i];
            const uint32_t before = i == 0 ? 0u : palette[i - 1];
            uint32_t delta = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const uint32_t d = (((now >> shift) & 0xFF) - ((before >> shift) & 0xFF)) & 0xFF;
                delta |= d << shift;
            }
            table[i] = delta;
        }
        bits.put(0, 1);                      // no colour cache for the table
        writeImageData(bits, table, static_cast<uint32_t>(colours), false);

        imageWidth = (w + (1u << xbits) - 1u) >> xbits;
        coded.assign(static_cast<size_t>(imageWidth) * h, 0xFF000000u);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t slot = index[argb[static_cast<size_t>(y) * w + x]];
                const uint32_t packedX = x >> xbits;
                const uint32_t shift = (x & ((1u << xbits) - 1u)) * static_cast<uint32_t>(bitsPerIndex);
                coded[static_cast<size_t>(y) * imageWidth + packedX] |= slot << (8 + shift);
            }
        }
    } else {
        coded = std::move(argb);
    }
    bits.put(0, 1);                          // no more transforms
    bits.put(0, 1);                          // no colour cache
    bits.put(0, 1);                          // one set of prefix codes for the whole image
    writeImageData(bits, coded, imageWidth, true);
    *out = bits.finish();
    return true;
}

void put24(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    put24(out, v);
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// A RIFF chunk: its tag, its size, its payload, padded to an even length.
void chunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& payload) {
    out.insert(out.end(), tag, tag + 4);
    put32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    if (payload.size() % 2 != 0) {
        out.push_back(0);
    }
}

std::vector<uint8_t> riff(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.reserve(body.size() + 12);
    out.insert(out.end(), { 'R', 'I', 'F', 'F' });
    put32(out, static_cast<uint32_t>(body.size() + 4));
    out.insert(out.end(), { 'W', 'E', 'B', 'P' });
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

bool encodeWebp(const ls::RasterBuffer& image, std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    std::vector<uint8_t> stream;
    if (!vp8l(image, &stream, error)) {
        return false;
    }
    std::vector<uint8_t> body;
    chunk(body, "VP8L", stream);
    *out = riff(body);
    return true;
}

bool encodeAnimatedWebp(const std::vector<ls::RasterBuffer>& frames,
                        const std::vector<int>& holdsMs, bool loop,
                        std::vector<uint8_t>* out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) { *error = why; }
        return false;
    };
    if (out == nullptr) {
        return false;
    }
    if (frames.empty() || holdsMs.size() != frames.size()) {
        return fail("an animation needs frames, each with a hold");
    }
    const uint32_t w = frames.front().width;
    const uint32_t h = frames.front().height;
    bool translucent = false;
    for (const ls::RasterBuffer& frame : frames) {
        if (frame.width != w || frame.height != h) {
            return fail("every frame of an animation is the same size");
        }
        for (uint32_t y = 0; y < h && !translucent; ++y) {
            const uint8_t* row = frame.row(y);
            for (uint32_t x = 0; x < w; ++x) {
                if (row[x * 4 + 3] != 255) {
                    translucent = true;
                    break;
                }
            }
        }
    }

    std::vector<uint8_t> body;
    std::vector<uint8_t> header;
    header.push_back(static_cast<uint8_t>(0x02 | (translucent ? 0x10 : 0x00)));   // animation, alpha
    header.insert(header.end(), { 0, 0, 0 });
    put24(header, w - 1);
    put24(header, h - 1);
    chunk(body, "VP8X", header);

    std::vector<uint8_t> anim;
    put32(anim, 0);                                   // background: transparent
    anim.push_back(loop ? 0 : 1);                     // loop count: 0 is for ever
    anim.push_back(0);
    chunk(body, "ANIM", anim);

    for (size_t i = 0; i < frames.size(); ++i) {
        std::vector<uint8_t> stream;
        if (!vp8l(frames[i], &stream, error)) {
            return false;
        }
        std::vector<uint8_t> frame;
        put24(frame, 0);                              // x / 2
        put24(frame, 0);                              // y / 2
        put24(frame, w - 1);
        put24(frame, h - 1);
        put24(frame, static_cast<uint32_t>(std::clamp(holdsMs[i], 1, 0xFFFFFF)));
        frame.push_back(0x02);                        // do not blend, do not dispose
        chunk(frame, "VP8L", stream);
        chunk(body, "ANMF", frame);
    }
    *out = riff(body);
    return true;
}

} // namespace fast
