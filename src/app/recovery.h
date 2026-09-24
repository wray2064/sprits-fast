// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// recovery.h — what survives the program not closing properly.
//
// Autosave, with one rule that decides everything else: **it never writes to
// the file you are editing.** Writing the current state over the artwork is
// the version of this feature that loses work rather than saving it -- it
// destroys the last deliberate save, so "close without saving" stops meaning
// anything and a mistake made at 11:59 is on disk at 12:00 with no way back.
//
// So a copy goes somewhere else, in the user's own settings directory, and
// stays there only as long as it is useful:
//
//   * it is written while there are unsaved changes and nothing is mid-drag;
//   * it is deleted when the document is saved properly, because the work is
//     then on disk where the person put it and a stale copy would offer to
//     "recover" something older than what they have;
//   * it is deleted when the program closes normally.
//
// Which means: **a recovery file that exists at startup is proof the last
// session ended badly.** Nothing has to record a crash, and nothing has to be
// believed -- the file's presence is the signal, and it is the one signal a
// crashed process is still able to leave.
//
// One file per running program, not per document, so two windows do not write
// over each other. The path the copy came from is kept beside it, because a
// package does not know where it was going to be saved.

#include "app/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// Two minutes. Long enough that a person never waits on it, short enough that
// what a crash costs is a couple of minutes of drawing rather than an
// afternoon. Aseprite and Photoshop both land near here.
constexpr uint32_t kDefaultAutosaveSeconds = 120;
constexpr uint32_t kMinAutosaveSeconds = 15;
constexpr uint32_t kMaxAutosaveSeconds = 1800;

// What was found waiting at startup.
struct RecoveredWork {
    std::string path;          // the recovery copy itself
    std::string originalPath;  // where it was being saved, or empty if never saved
    std::string name;          // something to show: the file's name, or "untitled"
    uint64_t    savedSeconds = 0;   // when the copy was written
};

// The folder recovery copies live in, created if it is not there. Empty when
// the platform will not name a settings directory, in which case autosave is
// off rather than writing somewhere unexpected.
std::string recoveryDirectory();

// Anything left by a session that did not end normally. Sorted newest first.
// Reading this does not delete anything: the person decides.
std::vector<RecoveredWork> findRecoveredWork();

bool discardRecoveredWork(const RecoveredWork& work);

// One program's autosave. Constructed once, claims a file name of its own,
// and is responsible for removing it on the way out.
class RecoverySession {
public:
    // Claims a name nothing else is using. False when there is no settings
    // directory to write into -- autosave is then simply off.
    bool begin();

    // Writes the document, if it is worth writing: there are unsaved changes
    // and `busy` is false. `nowSeconds` is the clock, passed in so this is
    // testable without waiting two minutes.
    //
    // Returns true when a copy was actually written.
    bool tick(Document& doc, uint64_t nowSeconds, bool busy);

    // Writes now, whatever the clock says, if there is anything to write.
    bool writeNow(Document& doc, uint64_t nowSeconds);

    // The work is safe on disk: the copy is removed. Called after a save and
    // on a clean exit.
    void clear();

    void setIntervalSeconds(uint32_t seconds);
    uint32_t intervalSeconds() const { return intervalSeconds_; }

    bool active() const { return !path_.empty(); }
    const std::string& path() const { return path_; }

    // When the last copy was written, or 0. For telling the person that the
    // safety net is actually there.
    uint64_t lastWriteSeconds() const { return lastWriteSeconds_; }
    bool haveCopy() const { return haveCopy_; }

private:
    std::string path_;
    std::string originPath_;      // the sidecar naming where it came from
    uint32_t    intervalSeconds_ = kDefaultAutosaveSeconds;
    // Whether the clock has been started is its own flag rather than
    // lastWriteSeconds_ being zero. The clock passed in is milliseconds since
    // the program started, divided down -- so it really is 0 for the first
    // second, and a zero-means-never sentinel armed itself again on every
    // frame and never wrote anything.
    bool        started_ = false;
    uint64_t    lastWriteSeconds_ = 0;
    bool        haveCopy_ = false;
};

} // namespace fast
