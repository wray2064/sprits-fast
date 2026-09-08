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
};

} // namespace fast
