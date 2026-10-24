// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// webp_predict.h — the pixel arithmetic lossless WebP's predictor transform is
// made of, once, for the reader and the writer: whatever one adds the other
// subtracts, so they must be the same functions. Pixels are 0xAARRGGBB.

#include <cstdint>
#include <cstdlib>

namespace fast {
namespace webp_detail {

inline uint32_t addPixels(uint32_t a, uint32_t b) {
    const uint32_t ag = (a & 0xFF00FF00u) + (b & 0xFF00FF00u);
    const uint32_t rb = (a & 0x00FF00FFu) + (b & 0x00FF00FFu);
    return (ag & 0xFF00FF00u) | (rb & 0x00FF00FFu);
}

inline uint32_t subPixels(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        out |= ((((a >> shift) & 0xFFu) - ((b >> shift) & 0xFFu)) & 0xFFu) << shift;
    }
    return out;
}

inline uint32_t average2(uint32_t a, uint32_t b) {
    return (((a ^ b) & 0xFEFEFEFEu) >> 1) + (a & b);
}

inline int channel(uint32_t argb, int shift) {
    return static_cast<int>((argb >> shift) & 0xFFu);
}

inline uint32_t select(uint32_t left, uint32_t top, uint32_t topLeft) {
    int toLeft = 0;
    int toTop = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int l = channel(left, shift);
        const int t = channel(top, shift);
        const int tl = channel(topLeft, shift);
        toLeft += std::abs(t - tl);
        toTop += std::abs(l - tl);
    }
    return toLeft < toTop ? left : top;
}

inline int clamp255(int v) {
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

inline uint32_t clampAddSubtractFull(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        out |= static_cast<uint32_t>(clamp255(channel(a, shift) + channel(b, shift) -
                                              channel(c, shift))) << shift;
    }
    return out;
}

inline uint32_t clampAddSubtractHalf(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int av = channel(a, shift);
        out |= static_cast<uint32_t>(clamp255(av + (av - channel(b, shift)) / 2)) << shift;
    }
    return out;
}

// The guess for the predictor mode 0 to 13 (14 and 15 are read as 0).
inline uint32_t predict(int mode, uint32_t left, uint32_t top, uint32_t topRight,
                        uint32_t topLeft) {
    switch (mode) {
        case 1:  return left;
        case 2:  return top;
        case 3:  return topRight;
        case 4:  return topLeft;
        case 5:  return average2(average2(left, topRight), top);
        case 6:  return average2(left, topLeft);
        case 7:  return average2(left, top);
        case 8:  return average2(topLeft, top);
        case 9:  return average2(top, topRight);
        case 10: return average2(average2(left, topLeft), average2(top, topRight));
        case 11: return select(left, top, topLeft);
        case 12: return clampAddSubtractFull(left, top, topLeft);
        case 13: return clampAddSubtractHalf(average2(left, top), topLeft);
        default: return 0xFF000000u;
    }
}

} // namespace webp_detail
} // namespace fast
