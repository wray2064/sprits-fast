// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// theme_store.h — the themes a person has made, kept as files.
//
// One `*.lstheme` file (app/theme_tokens.h) per theme, in a folder of the
// person's own settings. The files are the person's to copy, edit, send and
// delete outside the program, so reading them is as defensive as reading any
// other untrusted file: only so many, only so large, a bad one skipped and not
// fatal, and an id that collides with a built-in theme never replaces it.
//
// Every function takes the folder, so tests use a folder of their own and a
// run that did not read the person's settings can be pointed away from theirs.

#include "app/theme_tokens.h"

#include <string>
#include <vector>

namespace fast {

constexpr size_t kMaxCustomThemes = 64;
constexpr size_t kMaxThemeFileBytes = 16 * 1024;

// "<settings>/themes", or empty when the platform will not name a settings
// folder.
std::string themesFolder();

// Every theme in `folder`, by file name. A file that is too large, is not a
// theme, or whose id is a built-in's or one already seen is left out. Never
// more than kMaxCustomThemes.
std::vector<Theme> loadCustomThemes(const std::string& folder);

// An id for `name` that no built-in theme and no file in `folder` has: the
// name's own, or with -2, -3 ... after it.
std::string uniqueThemeId(const std::string& folder, const std::string& name);

// Writes `theme` as `<id>.lstheme` in `folder` (made if it is not there),
// atomically, replacing a file of that id. The theme's id is brought into line
// with its name first, and a built-in's id is never used: pass what the caller
// meant to overwrite as the theme's id, or call uniqueThemeId for a new one.
// `savedId` receives the id written.
bool saveCustomTheme(const std::string& folder, const Theme& theme, std::string* savedId,
                     std::string* error);

// Removes `<id>.lstheme`. False when there is none, or the id is a built-in's
// or is not a plain id (nothing outside the folder is ever touched).
bool deleteCustomTheme(const std::string& folder, const std::string& id);

// The theme with this id: a built-in's, or a custom one in `folder`. Null when
// there is none.
bool findTheme(const std::string& folder, const std::string& id, Theme* out);

} // namespace fast
