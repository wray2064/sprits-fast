// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/recovery.h"

#include "app/animation.h"
#include "app/file_io.h"
#include "app/library.h"

#include <algorithm>
#include <ctime>
#include <functional>

namespace fast {
namespace {

constexpr const char* kCopyExtension   = ".lsprite";
constexpr const char* kOriginExtension = ".from";

// A recovery copy is only useful while it is recent enough to be believed.
// Anything older than this is from a session the person has long forgotten,
// and offering it is worse than silence -- so it is cleared out when the
// folder is read.
constexpr uint64_t kStaleAfterSeconds = 30ull * 24ull * 60ull * 60ull;   // a month

uint64_t now() {
    return static_cast<uint64_t>(std::time(nullptr));
}

} // namespace

std::string recoveryDirectory() {
    const std::string base = preferencesDirectory();
    if (base.empty()) {
        return std::string();
    }
    const std::string directory = joinPath(base, "recovery");
    if (!directoryExists(directory) && !createDirectory(directory)) {
        return std::string();
    }
    return directory;
}

std::vector<RecoveredWork> findRecoveredWork() {
    return findRecoveredWorkIn(recoveryDirectory());
}

std::vector<RecoveredWork> findRecoveredWorkIn(const std::string& directory) {
    std::vector<RecoveredWork> out;
    if (directory.empty()) {
        return out;
    }
    for (const DirectoryEntry& entry : listDirectory(directory)) {
        if (entry.directory || !hasExtension(entry.name, kCopyExtension)) {
            continue;
        }
        RecoveredWork work;
        work.path = entry.path;
        work.savedSeconds = entry.modifiedSeconds;

        const uint64_t age = now() > entry.modifiedSeconds
            ? now() - entry.modifiedSeconds : 0;
        if (age > kStaleAfterSeconds) {
            discardRecoveredWork(work);
            continue;
        }

        // Where it was headed, if it had ever been saved.
        const std::string origin = joinPath(directory, fileStem(entry.name) + kOriginExtension);
        std::vector<uint8_t> bytes;
        std::string error;
        if (readFile(origin, bytes, &error) && !bytes.empty() && bytes.size() < 4096) {
            work.originalPath.assign(bytes.begin(), bytes.end());
            while (!work.originalPath.empty() &&
                   (work.originalPath.back() == '\n' || work.originalPath.back() == '\r')) {
                work.originalPath.pop_back();
            }
        }
        work.name = work.originalPath.empty() ? std::string("untitled")
                                              : fileName(work.originalPath);
        out.push_back(std::move(work));
    }
    // Newest first; two written in the same second go by their names, which carry
    // the session's start (session-<seconds>-<n>), so the order never wobbles.
    std::sort(out.begin(), out.end(), [](const RecoveredWork& a, const RecoveredWork& b) {
        return a.savedSeconds != b.savedSeconds ? a.savedSeconds > b.savedSeconds
                                                : a.path > b.path;
    });
    return out;
}

bool discardRecoveredWork(const RecoveredWork& work) {
    if (work.path.empty()) {
        return false;
    }
    const std::string origin =
        joinPath(directoryOf(work.path), fileStem(work.path) + kOriginExtension);
    deleteFile(origin);
    return deleteFile(work.path);
}

RecoverySummary summariseRecoveredWork(const RecoveredWork& work) {
    RecoverySummary summary;
    Document probe;
    std::string error;
    if (work.path.empty() || !probe.open(work.path, &error)) {
        return summary;
    }
    const auto size = probe.engine().getCanvasSize(probe.id());
    if (size.ok()) {
        summary.width = static_cast<uint32_t>(size.value.x);
        summary.height = static_cast<uint32_t>(size.value.y);
    }
    summary.frames = static_cast<int>(readFrames(probe).size());
    summary.readable = true;
    return summary;
}

namespace {

constexpr size_t kArchivedKept = 10;

// A file moved into a folder: copied, then removed, since there is no rename across
// what file_io offers. False when the original could not be read or the copy written;
// the original is only removed after the copy is safe.
bool moveInto(const std::string& file, const std::string& folder) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(file, bytes, &error) ||
        !writeFileAtomic(joinPath(folder, fileName(file)), bytes, &error)) {
        return false;
    }
    return deleteFile(file);
}

std::string sidecarOf(const std::string& copy) {
    return joinPath(directoryOf(copy), fileStem(copy) + kOriginExtension);
}

} // namespace

std::string archivedRecoveryDirectory(const std::vector<RecoveredWork>& found) {
    return found.empty() ? std::string() : joinPath(directoryOf(found.front().path), "older");
}

int archiveOlderRecoveredWork(const std::vector<RecoveredWork>& found) {
    if (found.size() < 2) {
        return 0;
    }
    const std::string older = archivedRecoveryDirectory(found);
    if (older.empty() || (!directoryExists(older) && !createDirectory(older))) {
        return 0;
    }
    int moved = 0;
    for (size_t i = 1; i < found.size(); ++i) {
        const std::string sidecar = sidecarOf(found[i].path);
        if (moveInto(found[i].path, older)) {
            ++moved;
            if (fileExists(sidecar)) {
                moveInto(sidecar, older);
            }
        }
    }
    // Only the newest few are kept: the names carry the time (session-<seconds>-<n>),
    // so by name is by age, and the oldest go.
    std::vector<std::string> kept;
    for (const DirectoryEntry& entry : listDirectory(older)) {
        if (!entry.directory && hasExtension(entry.name, kCopyExtension)) {
            kept.push_back(entry.path);
        }
    }
    std::sort(kept.begin(), kept.end(), std::greater<std::string>());
    for (size_t i = kArchivedKept; i < kept.size(); ++i) {
        deleteFile(sidecarOf(kept[i]));
        deleteFile(kept[i]);
    }
    return moved;
}

bool RecoverySession::begin() {
    const std::string directory = recoveryDirectory();
    if (directory.empty()) {
        return false;
    }
    // A name nothing else is using. Two copies of the program started in the
    // same second must not write over each other, so the clock is a starting
    // point rather than the answer.
    const uint64_t stamp = now();
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string stem = "session-" + std::to_string(stamp) + "-" +
                                 std::to_string(attempt);
        const std::string candidate = joinPath(directory, stem + kCopyExtension);
        if (fileExists(candidate)) {
            continue;
        }
        path_ = candidate;
        originPath_ = joinPath(directory, stem + kOriginExtension);
        return true;
    }
    return false;
}

void RecoverySession::setIntervalSeconds(uint32_t seconds) {
    intervalSeconds_ = std::clamp(seconds, kMinAutosaveSeconds, kMaxAutosaveSeconds);
}

bool RecoverySession::tick(Document& doc, uint64_t nowSeconds, bool busy) {
    if (!active() || busy || !doc.modified()) {
        return false;
    }
    // The first tick of a modified document starts the clock rather than
    // writing at once: a copy one second into a drawing session is a copy of
    // nothing, and the write would land in the middle of the first stroke.
    if (!started_) {
        started_ = true;
        lastWriteSeconds_ = nowSeconds;
        return false;
    }
    if (nowSeconds < lastWriteSeconds_ + intervalSeconds_) {
        return false;
    }
    return writeNow(doc, nowSeconds);
}

bool RecoverySession::writeNow(Document& doc, uint64_t nowSeconds) {
    if (!active() || !doc.modified()) {
        return false;
    }
    std::string error;
    // A picture goes with the copy, as it does with a save, so the question after a
    // crash can show what the lost work looked like. (The document is changed already:
    // there is nothing to write otherwise.)
    updateThumbnail(doc);
    // saveCopy, not save: the document must not come away believing it lives
    // in the recovery folder or that it has no unsaved changes.
    if (!doc.saveCopy(path_, &error)) {
        return false;
    }
    const std::string origin = doc.path();
    writeFileAtomic(originPath_, std::vector<uint8_t>(origin.begin(), origin.end()), &error);

    started_ = true;
    lastWriteSeconds_ = nowSeconds;
    haveCopy_ = true;
    return true;
}

void RecoverySession::clear() {
    if (!active()) {
        return;
    }
    deleteFile(path_);
    deleteFile(originPath_);
    haveCopy_ = false;
    started_ = false;
    lastWriteSeconds_ = 0;
}

} // namespace fast
