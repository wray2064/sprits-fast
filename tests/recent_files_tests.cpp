// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// recent_files_tests.cpp — the File menu's memory.

#include "app/file_io.h"
#include "app/recent_files.h"

#include <cstdio>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

void testMostRecentComesFirst() {
    fast::RecentFiles recent;
    recent.add("a.lsprite");
    recent.add("b.lsprite");
    recent.add("c.lsprite");

    CHECK(recent.entries().size() == 3);
    CHECK(recent.entries()[0] == "c.lsprite");
    CHECK(recent.entries()[2] == "a.lsprite");
}

// Reopening a file already in the list moves it up rather than duplicating it.
void testReopeningMovesToTheFront() {
    fast::RecentFiles recent;
    recent.add("a.lsprite");
    recent.add("b.lsprite");
    recent.add("a.lsprite");

    CHECK(recent.entries().size() == 2);
    CHECK(recent.entries()[0] == "a.lsprite");
    CHECK(recent.entries()[1] == "b.lsprite");
}

void testTheListIsBounded() {
    fast::RecentFiles recent;
    for (int i = 0; i < 40; ++i) {
        recent.add(std::to_string(i) + ".lsprite");
    }
    CHECK(recent.entries().size() == fast::RecentFiles::kMaxEntries);
    CHECK(recent.entries().front() == "39.lsprite");
}

void testRemoveAndClear() {
    fast::RecentFiles recent;
    recent.add("a.lsprite");
    recent.add("b.lsprite");
    recent.remove("a.lsprite");
    CHECK(recent.entries().size() == 1);
    recent.remove("not there");
    CHECK(recent.entries().size() == 1);
    recent.clear();
    CHECK(recent.empty());
}

void testEmptyPathsAreIgnored() {
    fast::RecentFiles recent;
    recent.add("");
    CHECK(recent.empty());
}

// The list survives a restart, including paths that are not ASCII.
void testItPersists() {
    const std::string accented = "C:/art/sprit\xC2\xB4s_\xE6\x97\xA5\xE6\x9C\xAC.lsprite";
    {
        fast::RecentFiles recent;
        recent.clear();
        recent.add("first.lsprite");
        recent.add(accented);
        recent.save();
    }
    {
        fast::RecentFiles reloaded;
        reloaded.load();
        CHECK(reloaded.entries().size() == 2);
        CHECK(reloaded.entries()[0] == accented);
        CHECK(reloaded.entries()[1] == "first.lsprite");
    }
    {
        fast::RecentFiles cleanup;
        cleanup.save();     // empty, leaving nothing behind for the next run
    }
}

// The file is under the user's control, so it is read defensively.
void testAHandEditedFileIsSurvivable() {
    const std::string path = fast::RecentFiles::storagePath();
    if (path.empty()) {
        return;
    }

    std::string text;
    for (int i = 0; i < 100; ++i) {
        text += "dup.lsprite\n";        // far more than the cap, all identical
    }
    text += "\n\n   \nreal.lsprite\n";  // blank lines and whitespace
    std::string error;
    fast::writeFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()), &error);

    fast::RecentFiles recent;
    recent.load();
    CHECK(recent.entries().size() <= fast::RecentFiles::kMaxEntries);
    CHECK(recent.entries().front() == "dup.lsprite");

    fast::RecentFiles cleanup;
    cleanup.save();
}

} // namespace

int main() {
    testMostRecentComesFirst();
    testReopeningMovesToTheFront();
    testTheListIsBounded();
    testRemoveAndClear();
    testEmptyPathsAreIgnored();
    testItPersists();
    testAHandEditedFileIsSurvivable();

    if (failures == 0) {
        std::printf("fast_recent: all checks passed\n");
        return 0;
    }
    std::printf("fast_recent: %d check(s) failed\n", failures);
    return 1;
}
