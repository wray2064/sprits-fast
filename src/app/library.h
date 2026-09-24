// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// library.h — the work beside the work.
//
// Two libraries, both of them a folder:
//
// **The project library** is the folder the document is in. A character is
// rarely one file -- a walk, an idle, a portrait, three enemies -- and the way
// to move between them should not be the Open dialog every time. So: the
// sprites in one folder, each with the picture it holds, one click to open.
//
// **The reference library** is a folder of images to draw from, which the
// person points at once. The same pose sheet gets used across a dozen sprites,
// and importing it should be picking it off a shelf rather than walking a
// dialog to wherever it lives.
//
// A project is deliberately *just a folder*. No project file, no manifest, no
// database: nothing to corrupt, nothing to keep in step with the filesystem,
// and a folder a person made in Explorer is already a project. What Fast
// remembers is only which folder -- in its own settings, not in the artwork.
//
// **A thumbnail comes from the package.** Every save writes a small picture of
// the first frame into the document under kThumbnailEntry, so a listing can
// show what a file holds by reading one entry rather than opening, compiling
// and throwing away a whole document.

#include "app/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// One sprite file in a project folder.
struct LibraryDocument {
    std::string path;
    std::string name;            // the stem: "hero", not "hero.lsprite"
    uint64_t    bytes = 0;
    uint64_t    modifiedSeconds = 0;
};

// One image in a reference folder.
struct LibraryImage {
    std::string path;
    std::string name;
    uint64_t    bytes = 0;
};

// The .lsprite files directly in `folder`, in the order listDirectory gives.
std::vector<LibraryDocument> listDocuments(const std::string& folder);

// The images directly in `folder`.
std::vector<LibraryImage> listImages(const std::string& folder);

// The folders directly in `folder`, for walking into one.
std::vector<std::string> listFolders(const std::string& folder);

// Builds the thumbnail for a document and stores it under kThumbnailEntry.
// Called on save. Failure is not an error worth stopping a save for -- a file
// without a thumbnail lists with a blank square, which is worse than nothing
// and much better than a save that refused.
bool updateThumbnail(Document& doc);

// Reads a document's thumbnail without opening the document: the package is
// walked for the one entry, and the rest of the file is not parsed. Returns
// the PNG bytes, which the caller decodes.
//
// This is untrusted input twice over -- a package somebody else wrote, holding
// an image somebody else made -- so it goes through the engine's container
// reader and then through image_io's bounds like any other import.
bool readThumbnail(const std::string& utf8Path, std::vector<uint8_t>* out);

// Where a library panel remembers its folders, in the user's own settings
// beside the recent list. Empty when the platform will not name one.
struct LibraryFolders {
    std::string project;         // the sprites
    std::string references;      // the images

    void load();
    void save() const;
    static std::string storagePath();
};

} // namespace fast
