// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// export_anim.h — an animation, out of the program as one file.
//
// A sheet is what a game engine wants; a GIF is what a person posts. Both have
// to come out of a pixel editor, and a GIF is the first thing anyone asks for.
// Three forms:
//
//   GIF           everywhere, and limited: 256 colours a frame, one of them
//                 transparent, and no partial alpha. A sprite with more
//                 colours than that is given a palette per frame, and one
//                 with more than that in a single frame is reduced to a fixed
//                 cube of colours -- said out loud, never silently.
//   Animated PNG  every colour and every level of alpha, exactly; what a GIF
//                 would be if it were designed now. Browsers play it.
//   PNG sequence  one numbered file per step, for tools that assemble their
//                 own.
//   WebP          lossless and animated: every colour and level of alpha, like
//                 the APNG, usually smaller, and what the web and chat apps
//                 prefer now. Its encoder is app/webp.
//
// Every frame is compiled at export quality and scaled by whole-number pixel
// duplication, exactly as a single-frame PNG export is, so a frame of the
// animation is byte-for-byte what exporting that frame alone would give.
//
// The encoders are written here rather than pulled in. A GIF encoder is LZW and
// some headers; an APNG is ordinary PNG chunks with three new ones, and the
// compression is stb's, already vendored. Neither reads untrusted input.

#include "app/animation.h"
#include "app/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

enum class AnimationFormat { Gif, Apng, PngSequence, Webp };

struct AnimationSettings {
    AnimationFormat format = AnimationFormat::Gif;
    uint32_t        scale = 1;
};

// What an encode did that the person should hear about.
struct AnimationReport {
    size_t frames = 0;
    bool   reducedColours = false;     // a GIF frame had more than 255 colours
    bool   droppedAlpha = false;       // a GIF had partial alpha, made all-or-nothing
};

// The steps a loop mode plays, in order: every step once, or there and back
// again without repeating either end.
std::vector<int> stepsToPlay(const Cycle& cycle);

// Encoders over frames already compiled and scaled. Each frame is held for
// `holdsMs`; `loop` is whether the animation repeats.
bool encodeGif(const std::vector<ls::RasterBuffer>& frames, const std::vector<int>& holdsMs,
               bool loop, std::vector<uint8_t>* out, AnimationReport* report,
               std::string* error);
bool encodeApng(const std::vector<ls::RasterBuffer>& frames, const std::vector<int>& holdsMs,
                bool loop, std::vector<uint8_t>* out, std::string* error);

// Compiles the cycle's frames and writes them in the chosen form. For a PNG
// sequence, `path` names the first file's pattern: "walk.png" writes
// "walk-01.png", "walk-02.png" and so on.
bool exportAnimation(Document& doc, const std::vector<Frame>& frames, const Cycle& cycle,
                     const std::string& path, const AnimationSettings& settings,
                     AnimationReport* report, std::string* error);

// The file extension a format is written with, for the save dialog.
const char* animationExtension(AnimationFormat format);

} // namespace fast
