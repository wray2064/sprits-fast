// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// templates_tests.cpp — the sizes a new file usually starts at, and the tips
// shown beside them on the start screen.

#include "app/document.h"
#include "app/palette_tools.h"
#include "app/templates.h"
#include "app/tips.h"

#include <cstdio>
#include <set>
#include <string>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

// Every template has to be something the New window would accept, or a card
// would make a document the program then refuses.
void testEveryTemplateIsMakeable() {
    CHECK(!fast::newTemplates().empty());
    for (const fast::NewTemplate& t : fast::newTemplates()) {
        CHECK(!t.name.empty());
        CHECK(t.width >= 1 && t.width <= fast::kMaxCanvasDimension);
        CHECK(t.height >= 1 && t.height <= fast::kMaxCanvasDimension);
        CHECK(static_cast<uint64_t>(t.width) * t.height <= fast::kMaxCanvasPixels);
        CHECK(t.background >= 0 && t.background <= 3);
        CHECK(t.preset >= -1 && t.preset < static_cast<int>(fast::palettePresets().size()));

        fast::Document doc;
        CHECK(doc.create("template", t.width, t.height));
    }
}

// A card is found by its label, so two cards may not share one.
void testLabelsAreUniqueAndSayTheSize() {
    std::set<std::string> seen;
    for (const fast::NewTemplate& t : fast::newTemplates()) {
        const std::string label = fast::templateLabel(t);
        CHECK(seen.insert(label).second);
        CHECK(label.find(std::to_string(t.width) + " x " + std::to_string(t.height)) !=
              std::string::npos);
    }
    fast::NewTemplate t;
    t.name = "Character";
    t.width = 64;
    t.height = 64;
    CHECK(fast::templateLabel(t) == "Character 64 x 64");
}

// The name typed for a new document becomes a suggested file name, so it has to
// be one a file can have, whatever was typed.
void testDocumentNamesAreFileNames() {
    CHECK(fast::documentNameFrom("hero") == "hero");
    CHECK(fast::documentNameFrom("  hero walk  ") == "hero walk");
    CHECK(fast::documentNameFrom("") == "untitled");
    CHECK(fast::documentNameFrom("   ") == "untitled");
    CHECK(fast::documentNameFrom("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");
    CHECK(fast::documentNameFrom("../../etc") == "etc");
    CHECK(fast::documentNameFrom("name...") == "name");
    CHECK(fast::documentNameFrom("///") == "untitled");
    CHECK(fast::documentNameFrom(std::string("tab\there\x01")) == "tabhere");
    // Bounded, and never cut in the middle of a character.
    CHECK(fast::documentNameFrom(std::string(200, 'x')).size() == 48);
    std::string accented;
    for (int i = 0; i < 40; ++i) {
        accented += "\xC3\xA9";                      // e acute, two bytes
    }
    const std::string cut = fast::documentNameFrom(accented);
    CHECK(cut.size() <= 48 && cut.size() % 2 == 0);
}

// Any number is a place in the list, and every tip is a sentence that fits a
// line of the start screen.
void testTipsWrapAndAreShort() {
    CHECK(!fast::tips().empty());
    std::set<std::string> seen;
    for (const std::string& tip : fast::tips()) {
        CHECK(!tip.empty());
        CHECK(tip.size() <= 120);
        CHECK(seen.insert(tip).second);      // none twice
    }
    const size_t count = fast::tips().size();
    CHECK(&fast::tipAt(0) == &fast::tipAt(static_cast<unsigned>(count)));
    CHECK(&fast::tipAt(1) == &fast::tipAt(static_cast<unsigned>(count) + 1));
    CHECK(&fast::tipAt(0xFFFFFFFFu) == &fast::tips()[0xFFFFFFFFu % count]);
}

} // namespace

int main() {
    testEveryTemplateIsMakeable();
    testLabelsAreUniqueAndSayTheSize();
    testTipsWrapAndAreShort();
    testDocumentNamesAreFileNames();

    if (failures == 0) {
        std::printf("fast_templates: all checks passed\n");
        return 0;
    }
    std::printf("fast_templates: %d check(s) failed\n", failures);
    return 1;
}
