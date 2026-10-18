// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// new_document.h — the questions a new document asks, drawn once.
//
// File > New asks them in a window of their own, and the start screen asks
// them beside its templates, as Photoshop's New document does: pick a
// preset, or set every detail yourself. Both draw the same fields over the
// same FileState::NewDocument, so the two can never disagree about what a
// new document can be, and a template is only a way to fill them in.

#include "ui/file_commands.h"

namespace fast {

// Name, size (typed, swapped, or one of the usual), what the canvas starts
// on, and which palette. Edits `spec` in place; returns whether the size is
// one Fast will make, so the caller can hold back its Create button.
bool drawNewDocumentFields(FileState::NewDocument& spec);

} // namespace fast
