// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// reference.h — the picture you are drawing from.
//
// A reference is not artwork. It is a photograph, a sketch, a frame of
// somebody else's animation, a screenshot of a pose: something to look at
// while drawing, placed over or under the canvas, faded, and never exported.
// No operation names it, no layer holds it, and compiling the sprite does not
// know it exists. It is the one thing in the document that is *for the person*
// rather than for the picture.
//
// **A reference lives in the document.** The bytes are stored in the package,
// not linked from disk. A linked reference is a smaller file and a broken
// promise: move the work to another machine, or tidy the folder it came from,
// and the reference is gone -- which is exactly when a half-finished drawing
// still needs it. So it travels with the file, re-encoded as PNG so the
// package carries one format, and capped so a photograph cannot quietly turn a
// 40 KB sprite into a 30 MB one.
//
// **Placement is in canvas pixels**, the same coordinates the artwork uses, so
// a reference lines up with the drawing at every zoom and stays lined up when
// the view moves. Scale is a float rather than a whole number: a reference is
// not pixel art being magnified, it is a picture being matched to a size.

#include "app/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// References are stored as "fast/ref/<id>.png", with their placement in
// "fast/references.txt" beside them -- the same line format the cycles use,
// for the same reason: it holds names, and a hand-written parser for one line
// of fields is something a person can check, which a JSON parser reading files
// other people send is not.
constexpr const char* kReferenceListEntry = "fast/references.txt";
constexpr const char* kReferencePrefix    = "fast/ref/";

// Eight megabytes of PNG per reference and sixteen across the document. A
// package holds far more, but a reference that doubles the size of the work is
// a surprise, and the person who wants that can scale the image down first.
constexpr size_t kMaxReferenceBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxReferenceTotalBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxReferences = 16;

// The longest side a reference is kept at. Above this it is scaled down on
// import -- nearest, so pixel-art references stay crisp -- because a 6000-pixel
// photograph is being looked at on a 64-pixel canvas.
constexpr uint32_t kMaxReferenceSide = 2048;

struct Reference {
    std::string id;            // the package entry's stem: "1", "2", ...
    std::string name;          // what it was called when imported
    uint32_t    width = 0;     // as stored, in its own pixels
    uint32_t    height = 0;

    // Where it sits, in canvas pixels, and how large.
    float x = 0.f;
    float y = 0.f;
    float scale = 1.f;

    float opacity = 0.5f;      // what a reference is for: seeing through it
    bool  visible = true;
    bool  behind = true;       // under the artwork, or over it
    bool  locked = false;      // dragging the canvas will not move it

    std::string entryName() const { return std::string(kReferencePrefix) + id + ".png"; }
};

// The document's references, in the order they were imported.
std::vector<Reference> readReferences(Document& doc);

// The PNG bytes of one reference, or null when the document has no such entry.
const std::vector<uint8_t>* referenceBytes(Document& doc, const Reference& reference);

// Imports an image file as a reference: reads it, decodes it to check it is an
// image and to learn its size, scales it down if it is enormous, re-encodes it
// as PNG, and stores it in the package. Placed centred on the canvas at a
// scale that fits, which is almost always what was wanted and is one drag from
// anything else.
//
// Brackets its own undo action, so an import taken back is one Ctrl+Z.
bool importReference(Document& doc, const std::string& utf8Path, Reference* out,
                     std::string* error);

// The same, from bytes already in hand -- a drop, or a file the library
// already read. `name` is what to call it.
bool addReference(Document& doc, const std::string& name,
                  const std::vector<uint8_t>& imageBytes, Reference* out,
                  std::string* error);

// Writes back the placement of every reference. The picture is not touched.
bool writeReferences(Document& doc, const std::vector<Reference>& references);

// Changes one reference's placement, leaving the others alone.
bool updateReference(Document& doc, const Reference& reference);

// Removes a reference and its image.
bool removeReference(Document& doc, const Reference& reference);

// How many bytes the references are costing the document.
size_t referenceBytesUsed(Document& doc);

// Placement that fits `reference` inside a canvas of this size, centred.
void fitReference(Reference& reference, uint32_t canvasWidth, uint32_t canvasHeight);

// The text in kReferenceListEntry, exposed for the tests: a list a person
// could read, and untrusted input on the way back in.
std::string encodeReferences(const std::vector<Reference>& references);
bool decodeReferences(const std::string& text, std::vector<Reference>* out);

} // namespace fast
