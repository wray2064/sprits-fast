// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// reference_section.h — showing part of a reference: a cell of a sprite sheet,
// a pose in a strip of poses.
//
// The whole image stays in the document; the reference shows the rectangle
// picked here (Reference::clip*). It is picked on a preview of the image by
// dragging, typed, or -- for a sprite sheet -- by giving the cell size and
// the gap between cells and clicking a cell, and stepped cell by cell with
// the arrows. Every change shows at once and is one undo step. Opened after a
// reference is imported, and from the References panel.

#include <string>

namespace fast {

struct Editor;
class CanvasView;

// Opens the window on a reference. `fit`: a new section is fitted to the
// canvas when picked, as a fresh import is; otherwise it stays where the
// reference was, at its scale.
void openReferenceSection(Editor& editor, const std::string& referenceId, bool fit);

void drawReferenceSectionWindow(Editor& editor, CanvasView& canvas);

// Imports an image file as a reference, picks it, and opens this window on
// it to choose the part shown: what File > Import reference does once a file
// is chosen, and a script's `reference FILE`. False, with why, when it is not
// an image that can be imported.
bool importReferenceToPick(Editor& editor, CanvasView& canvas, const std::string& utf8Path,
                           std::string* error);

} // namespace fast
