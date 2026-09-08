// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/recent_files.h"
#include "app/file_io.h"

#include <algorithm>

namespace fast {
namespace {

// Paths are stored one per line. A path may contain almost anything except a
// newline, which is what makes this format safe rather than merely simple.
std::vector<std::string> splitLines(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> lines;
    std::string current;
    for (uint8_t byte : bytes) {
        if (byte == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            if (!current.empty()) {
                lines.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(static_cast<char>(byte));
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

} // namespace

std::string RecentFiles::storagePath() {
    const std::string directory = preferencesDirectory();
    if (directory.empty()) {
        return std::string();
    }
    return directory + "/recent.txt";
}

void RecentFiles::load() {
    entries_.clear();

    const std::string path = storagePath();
    if (path.empty()) {
        return;
    }

    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(path, bytes, &error)) {
        return;     // no list yet, which is not a problem worth reporting
    }

    // This file is under the user's control and may have been edited by hand or
    // left half-written by an older build, so it is read defensively: anything
    // past the limit is dropped rather than trusted.
    for (const std::string& line : splitLines(bytes)) {
        if (entries_.size() >= kMaxEntries) {
            break;
        }
        if (std::find(entries_.begin(), entries_.end(), line) == entries_.end()) {
            entries_.push_back(line);
        }
    }
}

void RecentFiles::save() const {
    const std::string path = storagePath();
    if (path.empty()) {
        return;
    }

    std::string text;
    for (const std::string& entry : entries_) {
        // A path containing a newline would corrupt the list on the way back in.
        // Nothing can produce one on the platforms Fast runs on, and if
        // something ever does, dropping the entry is better than writing a file
        // that reads back as two.
        if (entry.find('\n') != std::string::npos ||
            entry.find('\r') != std::string::npos) {
            continue;
        }
        text += entry;
        text += '\n';
    }

    std::string error;
    writeFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()), &error);
}

void RecentFiles::add(const std::string& path) {
    if (path.empty()) {
        return;
    }
    remove(path);
    entries_.insert(entries_.begin(), path);
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
}

void RecentFiles::remove(const std::string& path) {
    entries_.erase(std::remove(entries_.begin(), entries_.end(), path), entries_.end());
}

void RecentFiles::clear() {
    entries_.clear();
}

} // namespace fast
