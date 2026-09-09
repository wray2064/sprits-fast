// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// ui_state_tests.cpp — the view that rides along in the file.
//
// This is parsed from data other people send, so most of what is here is about
// what happens when it is wrong: hostile numbers, truncated text, fields from a
// version that does not exist yet.

#include "app/ui_state.h"

#include <cmath>
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

void testRoundTrip() {
    fast::UiState written;
    written.zoom = 12.f;
    written.panX = -40.5f;
    written.panY = 17.25f;
    written.activeLayer = 3;
    written.previewScale = 3;
    written.previewTransparent = 0;
    written.previewColor = 0x123456;

    fast::UiState read;
    CHECK(fast::fromJson(fast::toJson(written), &read));
    CHECK(read.zoom == 12.f);
    CHECK(read.panX == -40.5f);
    CHECK(read.panY == 17.25f);
    CHECK(read.activeLayer == 3);
    CHECK(read.previewScale == 3);
    CHECK(read.previewTransparent == 0);
    CHECK(read.previewColor == 0x123456);
}

// The preview settings come out of a file like everything else here, so they are
// clamped rather than trusted.
void testPreviewSettingsAreClamped() {
    fast::UiState absurd;
    CHECK(fast::fromJson("{\"previewScale\":9999,\"previewTransparent\":7,"
                         "\"previewColor\":-1}", &absurd));
    absurd.clamp(1);
    CHECK(absurd.previewScale >= 1 && absurd.previewScale <= 4);
    CHECK(absurd.previewTransparent == 1);
    CHECK((absurd.previewColor & ~0xFFFFFF) == 0);

    fast::UiState negative;
    negative.previewScale = -3;
    negative.clamp(1);
    CHECK(negative.previewScale == 1);
}

void testDefaultsWhenSilent() {
    fast::UiState read;
    CHECK(fast::fromJson("{}", &read));
    CHECK(read.zoom == 8.f);
    CHECK(read.activeLayer == 0);

    // A field this build does not know is ignored, not refused: a newer Fast
    // must be able to write one without breaking an older one.
    fast::UiState newer;
    CHECK(fast::fromJson("{\"zoom\":4,\"onionSkin\":3,\"panX\":2}", &newer));
    CHECK(newer.zoom == 4.f);
    CHECK(newer.panX == 2.f);
}

void testRubbishIsRefused() {
    fast::UiState read;
    CHECK(!fast::fromJson("", &read));
    CHECK(!fast::fromJson("not json at all", &read));
    CHECK(!fast::fromJson("{", &read));
    CHECK(!fast::fromJson("{\"zoom\"}", &read));
    CHECK(!fast::fromJson("{\"zoom\":}", &read));
    CHECK(!fast::fromJson("{\"zoom\":abc}", &read));
    CHECK(!fast::fromJson("{\"zoom\":4", &read));

    // Refused means untouched, not half filled.
    CHECK(read.zoom == 8.f);
    CHECK(read.panX == 0.f);
}

// Values that parse perfectly well and would still wreck the canvas.
void testHostileNumbersAreClamped() {
    fast::UiState absurd;
    CHECK(fast::fromJson("{\"zoom\":100000,\"panX\":-99999999,\"activeLayer\":2000000}",
                         &absurd));
    absurd.clamp(3);
    CHECK(absurd.zoom <= 64.f);
    CHECK(absurd.panX >= -20000.f);
    CHECK(absurd.activeLayer == 2);

    // Infinity and NaN are refused at the parser rather than clamped later,
    // because they poison arithmetic before anyone gets to check them.
    fast::UiState infinite;
    CHECK(!fast::fromJson("{\"zoom\":1e999}", &infinite));

    // And if one ever reaches clamp by another route, it is replaced.
    fast::UiState nan;
    nan.zoom = std::nanf("");
    nan.panX = std::nanf("");
    nan.clamp(1);
    CHECK(std::isfinite(nan.zoom));
    CHECK(std::isfinite(nan.panX));

    fast::UiState negative;
    negative.activeLayer = -5;
    negative.clamp(2);
    CHECK(negative.activeLayer == 0);

    // A file recording a layer that no longer exists, because someone deleted it
    // in another program.
    fast::UiState stale;
    stale.activeLayer = 9;
    stale.clamp(0);
    CHECK(stale.activeLayer == 0);
}

void testWhitespaceIsTolerated() {
    fast::UiState read;
    CHECK(fast::fromJson("  {\n  \"zoom\" : 6 ,\n  \"panY\" : -3\n}  ", &read));
    CHECK(read.zoom == 6.f);
    CHECK(read.panY == -3.f);
}

} // namespace

int main() {
    testRoundTrip();
    testPreviewSettingsAreClamped();
    testDefaultsWhenSilent();
    testRubbishIsRefused();
    testHostileNumbersAreClamped();
    testWhitespaceIsTolerated();

    if (failures == 0) {
        std::printf("fast_ui_state: all checks passed\n");
        return 0;
    }
    std::printf("fast_ui_state: %d check(s) failed\n", failures);
    return 1;
}
