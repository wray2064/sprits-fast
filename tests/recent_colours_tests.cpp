// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// recent_colours_tests.cpp — the colours just used that the palette lacks.

#include "app/preferences.h"
#include "app/recent_colours.h"

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

ls::Color grey(uint8_t v) { return { v, v, v, 255 }; }

void testMostRecentFirstWithoutDuplicates() {
    fast::RecentColours recent;
    recent.add(grey(10));
    recent.add(grey(20));
    recent.add(grey(30));
    recent.add(grey(10));       // used again: moves up, is not listed twice
    CHECK(recent.entries().size() == 3);
    CHECK(recent.entries()[0] == grey(10));
    CHECK(recent.entries()[1] == grey(30));
    CHECK(recent.entries()[2] == grey(20));
}

void testTheListIsBoundedAndTransparentIsNotKept() {
    fast::RecentColours recent;
    for (int i = 1; i <= 40; ++i) {
        recent.add(grey(static_cast<uint8_t>(i)));
    }
    CHECK(recent.entries().size() == fast::RecentColours::kMaxEntries);
    CHECK(recent.entries().front() == grey(40));
    recent.add({ 5, 5, 5, 0 });
    CHECK(recent.entries().front() == grey(40));
    recent.clear();
    CHECK(recent.entries().empty());
}

void testItRoundTripsThroughText() {
    fast::RecentColours recent;
    recent.add({ 0xE0, 0x8F, 0x40, 0xFF });
    recent.add({ 0x30, 0x60, 0xD0, 0x80 });
    const std::string text = fast::encodeRecentColours(recent.entries());
    CHECK(text == "3060d080 e08f40ff");
    const std::vector<ls::Color> back = fast::decodeRecentColours(text);
    CHECK(back.size() == 2);
    CHECK(back[0] == ls::Color({ 0x30, 0x60, 0xD0, 0x80 }));
    CHECK(back[1] == ls::Color({ 0xE0, 0x8F, 0x40, 0xFF }));
}

// The text is in a file the person can edit, so it is read defensively.
void testAHandEditedValueIsSurvivable() {
    CHECK(fast::decodeRecentColours("").empty());
    CHECK(fast::decodeRecentColours("nonsense 12 zzzzzzzz 1234567").empty());
    CHECK(fast::decodeRecentColours("   ff0000ff     00ff00ff  ").size() == 2);
    std::string many;
    for (int i = 0; i < 200; ++i) {
        many += "ff0000ff ";
    }
    CHECK(fast::decodeRecentColours(many).size() <= fast::RecentColours::kMaxEntries);

    fast::RecentColours recent;
    recent.set(fast::decodeRecentColours("ff0000ff ff0000ff 00ff00ff"));
    CHECK(recent.entries().size() == 2);
    CHECK(recent.entries()[0] == ls::Color({ 255, 0, 0, 255 }));
}

void testThePreferencesKeepThem() {
    fast::Preferences p;
    p.recentColours = "e08f40ff 3060d0ff";
    CHECK(fast::loadPreferences(fast::savePreferences(p)).recentColours == p.recentColours);
    CHECK(fast::loadPreferences("").recentColours.empty());
}

} // namespace

int main() {
    testMostRecentFirstWithoutDuplicates();
    testTheListIsBoundedAndTransparentIsNotKept();
    testItRoundTripsThroughText();
    testAHandEditedValueIsSurvivable();
    testThePreferencesKeepThem();

    if (failures == 0) {
        std::printf("fast_recent_colours: all checks passed\n");
        return 0;
    }
    std::printf("fast_recent_colours: %d check(s) failed\n", failures);
    return 1;
}
