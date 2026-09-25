// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// import_aseprite.h — opening Aseprite's own files.
//
// Most of the people Fast is for have their work in .aseprite files. Opening
// one has to give back the work, not a picture of it: the layers with their
// names, opacity, blend and visibility; the frames with their holds; the tags
// as cycles; the palette as the palette. And the pixels as inks -- one element
// per colour per layer -- so an indexed sprite's indices become palette slots,
// and recolouring a swatch recolours what Aseprite painted with that index.
//
// Read from the published format description (aseprite/docs/ase-file-specs.md),
// not from Aseprite's code. What does not carry over is said in the report
// rather than dropped silently: tilemap layers, and the blend modes the engine
// does not have, which are brought to the nearest one it does.
//
// The file is untrusted input like every other. Every chunk is bounded by the
// frame that holds it and every frame by the file; sizes are checked before
// anything is allocated; a compressed cel must inflate to exactly the size its
// header says or it is refused.

#include "app/document.h"
#include "app/import_image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

struct AsepriteReport : ImportReport {
    size_t layers = 0;
    size_t tags = 0;
    bool   indexed = false;
    bool   approximatedBlends = false;   // a blend mode brought to the nearest the engine has
    bool   skippedTilemaps = false;
};

// What the file holds, before anything is built from it.
struct AseFile {
    struct Layer {
        std::string name;
        uint16_t    flags = 0;         // 1 visible
        uint16_t    type = 0;          // 0 image, 1 group, 2 tilemap
        uint16_t    childLevel = 0;
        uint16_t    blend = 0;
        uint8_t     opacity = 255;
    };
    struct Cel {
        int      layer = 0;
        int      x = 0;
        int      y = 0;
        uint8_t  opacity = 255;
        int      linkedFrame = -1;     // a linked cel: the frame whose cel it shows
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;   // depth/8 bytes a pixel, as stored
    };
    struct Frame {
        int              durationMs = 100;
        std::vector<Cel> cels;
    };
    struct Tag {
        int         from = 0;
        int         to = 0;
        int         direction = 0;     // 0 forward, 1 reverse, 2 ping-pong, 3 reverse ping-pong
        int         repeat = 0;        // 0 forever
        std::string name;
    };

    uint32_t width = 0;
    uint32_t height = 0;
    int      depth = 32;               // 32 RGBA, 16 greyscale, 8 indexed
    int      transparentIndex = 0;
    bool     layerOpacityValid = false;
    std::vector<ls::Color>   palette;
    std::vector<std::string> paletteNames;
    std::vector<Layer>       layers;   // bottom first, as the file lists them
    std::vector<Frame>       frames;
    std::vector<Tag>         tags;
};

bool parseAseprite(const std::vector<uint8_t>& bytes, AseFile* out, std::string* error);

// Builds `doc` afresh from a parsed file.
bool documentFromAseprite(Document& doc, const AseFile& file, const std::string& name,
                          AsepriteReport* report, std::string* error);

// Reads a .aseprite or .ase file and builds `doc` from it. The document has no
// path afterwards, as with an opened image: saving asks where the .lsprite goes.
bool openAsepriteAsDocument(Document& doc, const std::string& path, AsepriteReport* report,
                            std::string* error);

bool looksLikeAsepriteName(const std::string& path);

} // namespace fast
