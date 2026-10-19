// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// recent_files.h — the File menu's memory.
//
// A short list of paths, most recent first, kept in the user's own settings
// directory rather than beside the program. Deliberately dull, with two
// decisions worth stating:
//
// Entries are not verified when the list is read. A file on a network share or
// an unplugged drive is missing this minute and back the next, and dropping it
// from the menu would be wrong. It is removed when opening it actually fails.
//
// The file is written atomically, like everything else here. A recent list is
// worth little, but a truncated one that fails to parse is worth less than the
// one it replaced.

#include <cstddef>
#include <string>
#include <vector>

namespace fast {

class RecentFiles {
public:
    static constexpr size_t kMaxEntries = 10;

    // Reads the list from the user's settings directory. Missing or unreadable
    // is simply an empty list, not an error: this is a convenience.
    void load();
    void save() const;

    // A run that is not the person's -- a script, a capture -- keeps its list in
    // memory: it neither reads theirs nor writes over it.
    void keepInMemory() { inMemory_ = true; }

    // Moves `path` to the front, removing any earlier mention of it, and drops
    // whatever falls off the end.
    void add(const std::string& path);
    void remove(const std::string& path);
    void clear();

    const std::vector<std::string>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

    // Where the list is kept. Empty if the platform will not name a settings
    // directory, in which case load and save do nothing.
    static std::string storagePath();

private:
    std::vector<std::string> entries_;
    bool inMemory_ = false;
};

// What the start screen shows about one recent entry, looked up on demand
// (when the screen opens), never when the list is read: see the note at the
// top about files that come and go.
struct RecentInfo {
    std::string path;
    bool        exists = false;
    uint64_t    modifiedSeconds = 0;     // since the epoch; 0 when unknown
};

// One RecentInfo per entry, in the same order. A file the platform will not
// stat is reported as missing, not as an error.
std::vector<RecentInfo> describeRecent(const std::vector<std::string>& entries);

// "just now", "5 minutes ago", "yesterday", "3 days ago", "2 months ago": how
// long ago `modifiedSeconds` was, as of `nowSeconds`. A time in the future, or
// an unknown one (0), is an empty string rather than a wrong claim.
std::string ageText(uint64_t modifiedSeconds, uint64_t nowSeconds);

} // namespace fast
