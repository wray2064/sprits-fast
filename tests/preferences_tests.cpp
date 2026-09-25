// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// preferences_tests.cpp — a person's settings, read as untrusted.
//
// The promises: preferences round-trip; unknown names are ignored; every value
// is clamped rather than trusted; and a new canvas the policy refuses falls
// back to the default.

#include "app/preferences.h"

#include <cstdio>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

int main() {
    fast::Preferences p;
    p.newWidth = 64;
    p.newHeight = 24;
    p.newBackground = 2;
    p.newPreset = 3;
    p.autosaveOn = false;
    p.autosaveSeconds = 300;
    p.pixelGrid = false;
    p.historyLimit = 500;
    p.checkerLight = 0xa0b0c0;
    p.checkerDark = 0x010203;
    p.checkerSize = 16;
    p.gridColour = 0xff0000;
    p.gridOpacity = 80;
    const fast::Preferences back = fast::loadPreferences(fast::savePreferences(p));
    CHECK(back.newWidth == 64 && back.newHeight == 24 && back.newBackground == 2 &&
          back.newPreset == 3 && !back.autosaveOn && back.autosaveSeconds == 300 &&
          !back.pixelGrid && back.historyLimit == 500);
    CHECK(back.checkerLight == 0xa0b0c0 && back.checkerDark == 0x010203 &&
          back.checkerSize == 16 && back.gridColour == 0xff0000 && back.gridOpacity == 80);

    const fast::Preferences hostile = fast::loadPreferences(
        "new.width = 99999999\nnew.height = -4\nautosave.seconds = 1\n"
        "history.limit = abc\nsomething.else = 7\nnew.background = 9\n");
    CHECK(hostile.newWidth <= 16384 && hostile.newHeight >= 1);
    CHECK(hostile.autosaveSeconds == 30);
    CHECK(hostile.historyLimit == 200);
    CHECK(hostile.newBackground == 3);

    // A colour that is not #rrggbb keeps the default; an upper-case one reads.
    const fast::Preferences colours = fast::loadPreferences(
        "view.checker-light = red\nview.checker-dark = #12345\nview.grid-colour = #ABCDEF\n"
        "view.checker-size = 1000\nview.grid-opacity = -3\n");
    CHECK(colours.checkerLight == fast::Preferences().checkerLight);
    CHECK(colours.checkerDark == fast::Preferences().checkerDark);
    CHECK(colours.gridColour == 0xabcdef);
    CHECK(colours.checkerSize == 64 && colours.gridOpacity == 0);

    const fast::Preferences huge = fast::loadPreferences("new.width = 16384\nnew.height = 16384\n");
    CHECK(huge.newWidth == 32 && huge.newHeight == 32);

    if (failures == 0) {
        std::printf("preferences: all passed\n");
        return 0;
    }
    std::printf("preferences: %d failure(s)\n", failures);
    return 1;
}
