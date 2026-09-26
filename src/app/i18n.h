// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// i18n.h — the interface in other languages.
//
// The interface is written in English, and every string a person reads in a
// menu, a section heading or the toolbar goes through tr(), which looks it up
// in the catalogue for the language chosen. A catalogue is a plain text file,
// one "English = Translation" a line, so translating Fast is editing text,
// and a string the catalogue lacks simply stays English. Catalogues sit in a
// "lang" folder beside the program, named by language: es.txt, fr.txt, ...

#include <string>
#include <vector>

namespace fast {

// Replaces the catalogue with the one in `text`. Lines that do not parse are
// skipped; "\n" in either side is a line break. Returns how many it read.
size_t loadCatalogue(const std::string& text);

// Back to English.
void clearCatalogue();

// The translation of `english`, or `english` itself. The pointer stays good
// until the catalogue changes.
const char* tr(const char* english);

// The languages there are catalogues for in `folder`: the file names without
// ".txt", sorted.
std::vector<std::string> languagesIn(const std::string& folder);

} // namespace fast
