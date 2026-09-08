// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// file_io.h — reading and writing files, correctly.
//
// Two things that sound like details and are not.
//
// **Paths are UTF-8.** Everywhere in Fast, including what a file dialog hands
// back. On Windows, std::ofstream(const char*) interprets that path in the
// active ANSI code page instead, which does not fail -- it silently creates a
// file with a mangled name. Saving to a folder with an accent in it produces a
// file the user cannot find, and opening one they picked from a dialog either
// fails or creates a garbled duplicate. So paths are converted to UTF-16 and
// opened with the wide API.
//
// **Saving is atomic.** A save writes a temporary file beside the target and
// then renames it over the top, so an interruption -- a crash, a full disk, a
// pulled cable -- leaves either the old file or the new one. Writing directly
// into the destination means a failure halfway through destroys the artwork
// that was already there, which is the worst thing a save can do.

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// Reads a whole file. `error` is filled with something a person can act on.
bool readFile(const std::string& utf8Path, std::vector<uint8_t>& out, std::string* error);

// Writes a whole file, atomically: temporary file, then rename over the target.
bool writeFileAtomic(const std::string& utf8Path, const std::vector<uint8_t>& bytes,
                     std::string* error);

bool fileExists(const std::string& utf8Path);
bool deleteFile(const std::string& utf8Path);

// The directory this application may keep its own settings in, created if it is
// not there. Empty if the platform will not say.
std::string preferencesDirectory();

// Path pieces, on UTF-8 paths, accepting either separator.
std::string fileName(const std::string& utf8Path);       // "art/hero.lsprite" -> "hero.lsprite"
std::string fileStem(const std::string& utf8Path);       // -> "hero"
std::string directoryOf(const std::string& utf8Path);    // -> "art"
std::string withExtension(const std::string& utf8Path, const std::string& dottedExtension);
bool        hasExtension(const std::string& utf8Path, const std::string& dottedExtension);

} // namespace fast
