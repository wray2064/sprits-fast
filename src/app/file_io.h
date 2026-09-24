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
bool directoryExists(const std::string& utf8Path);

// Creates one directory, not a path of them. True when it exists afterwards,
// whether this call made it or it was already there.
bool createDirectory(const std::string& utf8Path);

// One entry of a directory listing.
struct DirectoryEntry {
    std::string path;            // the full UTF-8 path
    std::string name;            // the last component
    bool        directory = false;
    uint64_t    bytes = 0;       // 0 for a directory
    uint64_t    modifiedSeconds = 0;   // since the epoch, for sorting by age
};

// What is directly inside a directory, sorted: directories first, then files,
// each by name, case-insensitively for ASCII. Never recurses -- a library
// showing one folder is a library a person can reason about, and walking a
// tree they pointed at by accident is how a file browser hangs.
//
// Hidden entries and anything the platform will not stat are skipped. A
// directory that cannot be read is an empty list, not an error: a library
// panel pointed at an unplugged drive should say "nothing here", not fail.
std::vector<DirectoryEntry> listDirectory(const std::string& utf8Path);

// The parent of a directory, or an empty string at the root. Accepts and
// returns UTF-8 paths with either separator.
std::string parentDirectory(const std::string& utf8Path);

// `directory` and `name` joined with the platform's separator.
std::string joinPath(const std::string& directory, const std::string& name);

// A directory a person is likely to want a file library to start in.
std::string documentsDirectory();

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
