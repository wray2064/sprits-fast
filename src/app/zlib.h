// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// zlib.h — compressed streams, from the two stb headers already vendored.
//
// Aseprite keeps each cel's pixels as a zlib stream. stb has both halves --
// the inflate its PNG decoder uses and the deflate its PNG writer uses -- so
// these are thin, bounded wrappers rather than a third library.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fast {

// Inflates a zlib stream that must come to exactly `expected` bytes -- the
// caller knows how large a cel is from its header, so a stream that decodes
// to anything else is refused rather than trusted, and a stream claiming to
// be enormous cannot make it allocate more than that.
bool zlibInflate(const uint8_t* data, size_t size, size_t expected, std::vector<uint8_t>* out);

// Deflates, with a zlib header.
bool zlibDeflate(const std::vector<uint8_t>& data, std::vector<uint8_t>* out);

// PNG's CRC, and one chunk -- length, type, data, CRC -- appended to `out`.
// Shared by the writers that go beyond what stb writes: animated PNG and
// indexed PNG.
uint32_t pngCrc(const uint8_t* data, size_t size);
void appendPngChunk(std::vector<uint8_t>& out, const char type[4],
                    const std::vector<uint8_t>& data);

} // namespace fast
