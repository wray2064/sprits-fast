// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// guide_tests.cpp — the user guide's Markdown, and the guide itself.
//
// The parser is small and is tested on small things: each marker, and a marker
// with no partner. The guide is tested as a whole: every file under assets/help
// parses, has a title, and links only to topics that exist, so a renamed topic
// cannot leave a broken link in the manual.

#include "app/guide.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef GUIDE_DIR
#error "GUIDE_DIR must name assets/help"
#endif

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

namespace {

using namespace fast;

void testInlineMarkers() {
    const std::vector<Run> runs = parseInline("a **bold** and *slanted* with `Ctrl+Z` and [a link](topic:x).");
    REQUIRE(runs.size() == 9);
    CHECK(runs[0].text == "a " && runs[0].style == RunStyle::Plain);
    CHECK(runs[1].text == "bold" && runs[1].style == RunStyle::Bold);
    CHECK(runs[3].text == "slanted" && runs[3].style == RunStyle::Italic);
    CHECK(runs[5].text == "Ctrl+Z" && runs[5].style == RunStyle::Code);
    CHECK(runs[7].text == "a link" && runs[7].style == RunStyle::Link && runs[7].target == "topic:x");
    CHECK(runs[8].text == ".");
}

void testAMarkerWithoutAPartnerIsACharacter() {
    const auto flat = [](const std::string& text) {
        std::string out;
        for (const Run& run : parseInline(text)) { out += run.text; }
        return out;
    };
    CHECK(flat("2 * 3 = 6") == "2 * 3 = 6");
    CHECK(flat("an *unclosed one") == "an *unclosed one");
    CHECK(flat("a `tick") == "a `tick");
    CHECK(flat("**") == "**");
    CHECK(flat("[no target]") == "[no target]");
    CHECK(flat("[x](") == "[x](");
    CHECK(flat("a \\*star\\* and \\`tick\\`") == "a *star* and `tick`");
    CHECK(flat("").empty());
}

void testBlocks() {
    const std::string text =
        "# Title\n\n## Section\n\nA paragraph\nover two lines.\n\n"
        "- one\n- two\n  continued\n  - nested\n\n1. first\n2. second\n\n"
        "> a note\n\n---\n\n```\ncode  kept\n\n  as is\n```\n\n"
        "| a | b |\n|---|---|\n| 1 | 2 |\n";
    const std::vector<Block> blocks = parseBlocks(text);
    REQUIRE(blocks.size() == 12);
    CHECK(blocks[0].kind == BlockKind::Heading && blocks[0].level == 1);
    CHECK(blocks[1].kind == BlockKind::Heading && blocks[1].level == 2);
    CHECK(blocks[2].kind == BlockKind::Paragraph && blocks[2].runs[0].text == "A paragraph over two lines.");
    CHECK(blocks[3].kind == BlockKind::Bullet && blocks[3].level == 0);
    CHECK(blocks[4].kind == BlockKind::Bullet && blocks[4].runs[0].text == "two continued");
    CHECK(blocks[5].kind == BlockKind::Bullet && blocks[5].level == 1);
    CHECK(blocks[6].kind == BlockKind::Numbered && blocks[6].number == 1);
    CHECK(blocks[7].kind == BlockKind::Numbered && blocks[7].number == 2);
    CHECK(blocks[8].kind == BlockKind::Note);
    CHECK(blocks[9].kind == BlockKind::Rule);
    CHECK(blocks[10].kind == BlockKind::Code && blocks[10].lines.size() == 3 &&
          blocks[10].lines[2] == "  as is");
    CHECK(blocks[11].kind == BlockKind::Table && blocks[11].rows.size() == 2 &&
          blocks[11].rows[0].size() == 2 && blocks[11].rows[1][1][0].text == "2");
}

void testSearch() {
    std::vector<GuideSource> sources = {
        { "palette", "# Colour and palettes\n\nSlots recolour everything.\n" },
        { "drawing", "# Drawing\n\nThe pencil paints with the palette's colours.\n" },
        { "files", "# Files\n\nSaving.\n" },
    };
    const std::vector<GuideTopic> topics = parseGuide(sources);
    REQUIRE(topics.size() == 3);
    CHECK(topics[0].title == "Colour and palettes");
    // The topic about it comes before the one that mentions it.
    const std::vector<GuideHit> hits = searchGuide(topics, "palette");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].topic == 0 && hits[1].topic == 1);
    // Every word must be there.
    CHECK(searchGuide(topics, "palette pencil").size() == 1);
    CHECK(searchGuide(topics, "palette nonsense").empty());
    CHECK(searchGuide(topics, "").empty());
    CHECK(searchGuide(topics, "   ").empty());
    CHECK(searchGuide(topics, "PENCIL").size() == 1);          // case does not matter
    CHECK(topicIdFromFileName("assets/help/02-drawing.md") == "drawing");
    CHECK(topicIdFromFileName("99-licences.txt") == "licences");
    CHECK(topicIdFromFileName("plain.md") == "plain");
}

void testHostileInput() {
    std::string many;
    for (int i = 0; i < 20000; ++i) { many += "- item **x** `y` [z](topic:q)\n"; }
    const std::vector<Block> blocks = parseBlocks(many);
    CHECK(blocks.size() <= kMaxGuideBlocks);
    std::string cells = "|";
    for (int i = 0; i < 5000; ++i) { cells += " c |"; }
    const std::vector<Block> table = parseBlocks(cells + "\n");
    REQUIRE(table.size() == 1);
    size_t total = 0;
    for (const auto& row : table[0].rows) { total += row.size(); }
    CHECK(total <= kMaxTableCells);
    // Nested-looking markers, an unterminated fence, a NUL and a very long line.
    (void)parseBlocks("```\nnever closed\n");
    (void)parseBlocks(std::string("a\0b **\0** [\0](\0)", 17));
    (void)parseBlocks(std::string(100000, '*'));
    (void)parseInline(std::string(100000, '`'));
}

// The guide as shipped.
void testTheGuideIsWhole() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(GUIDE_DIR)) {
        if (entry.path().extension() == ".md") { files.push_back(entry.path()); }
    }
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 10);
    std::vector<GuideSource> sources;
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        GuideSource source;
        source.id = topicIdFromFileName(file.filename().string());
        source.text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(source.text.size() < kMaxGuideBytes);
        sources.push_back(source);
    }
    const std::vector<GuideTopic> topics = parseGuide(sources);
    for (const GuideTopic& topic : topics) {
        // A first line that is a title, and something under it.
        CHECK(topic.title != topic.id);
        CHECK(topic.blocks.size() >= 3);
        for (size_t i = 0; i < topics.size(); ++i) {
            if (&topics[i] != &topic) { CHECK(topics[i].id != topic.id); }
        }
    }
    const std::string broken = firstBrokenLink(topics);
    if (!broken.empty()) { std::printf("  broken link: %s\n", broken.c_str()); }
    CHECK(broken.empty());
    // The topics the program opens by name exist.
    for (const char* id : { "welcome", "getting-started", "drawing", "colour", "animation", "files",
                            "appearance", "shortcuts", "troubleshooting" }) {
        CHECK(std::any_of(topics.begin(), topics.end(),
                          [&](const GuideTopic& topic) { return topic.id == id; }));
    }
    // Searching for what a person would, finds the topic about it first.
    const std::vector<GuideHit> hits = searchGuide(topics, "palette");
    REQUIRE(!hits.empty());
    CHECK(topics[hits[0].topic].id == "colour");
}

} // namespace

int main() {
    testInlineMarkers();
    testAMarkerWithoutAPartnerIsACharacter();
    testBlocks();
    testSearch();
    testHostileInput();
    testTheGuideIsWhole();
    if (failures == 0) {
        std::printf("fast_guide: all checks passed\n");
        return 0;
    }
    std::printf("fast_guide: %d check(s) failed\n", failures);
    return 1;
}
