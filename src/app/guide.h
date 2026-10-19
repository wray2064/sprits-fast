// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// guide.h — the user guide: topics written in a small subset of Markdown, read
// into blocks a window can draw, and searched.
//
// The guide is text under assets/help/, one file a topic, compiled into the
// program so it can never be missing from beside it and needs no network. Its
// Markdown is only what a manual needs:
//
//   # Title                 the topic's title (the first line)
//   ## Section, ### Part    headings
//   a paragraph             lines up to a blank one
//   - item                  a bullet (two spaces in: one level down)
//   1. item                 a numbered one
//   > a note                set apart from the text
//   | a | table |           a table; the first row is the header
//   ``` ... ```             lines kept as written
//   ---                     a rule
//   **bold** *italic* `Ctrl+Z`   inline; code is how a key is written
//   [text](topic:drawing)   a link to another topic; [text](https://...) to a page
//
// The parser takes anything: it is read as untrusted like every other text here
// (the guide is ours, but the reader is also fuzzed), bounded, and a marker with
// no partner is just a character.

#include <cstddef>
#include <string>
#include <vector>

namespace fast {

enum class RunStyle { Plain, Bold, Italic, Code, Link };

// A piece of a line in one style. A link's `target` is "topic:id" or a URL.
struct Run {
    std::string text;
    RunStyle    style = RunStyle::Plain;
    std::string target;
};

enum class BlockKind { Heading, Paragraph, Bullet, Numbered, Table, Code, Note, Rule };

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    int       level = 0;       // Heading: 1 to 3. Bullet, Numbered: 0, or 1 when nested.
    int       number = 0;      // Numbered: the number it was written with
    std::vector<Run> runs;     // Heading, Paragraph, Bullet, Numbered, Note
    std::vector<std::vector<std::vector<Run>>> rows;   // Table: rows of cells of runs
    std::vector<std::string> lines;                    // Code
};

struct GuideTopic {
    std::string id;            // "drawing": the file's name without its number and extension
    std::string title;
    std::vector<Block> blocks; // after the title
    std::string flat;          // the text as one line, for searching
};

struct GuideSource {
    std::string id;
    std::string text;
};

// Limits on what one topic may be, so a hostile file cannot make the window
// build without end.
constexpr size_t kMaxGuideBytes = 512 * 1024;
constexpr size_t kMaxGuideBlocks = 4000;
constexpr size_t kMaxTableCells = 2000;

std::vector<Run> parseInline(const std::string& text);
std::vector<Block> parseBlocks(const std::string& markdown);
GuideTopic parseTopic(const GuideSource& source);

// The topics, in the order given. A source with no title line is titled by its id.
std::vector<GuideTopic> parseGuide(const std::vector<GuideSource>& sources);

// The id a file called "02-drawing.md" has: "drawing".
std::string topicIdFromFileName(const std::string& fileName);

// Whether every link to a topic names one that exists. Returns the first that does
// not ("" when all do), for a test to name.
std::string firstBrokenLink(const std::vector<GuideTopic>& topics);

struct GuideHit {
    size_t      topic = 0;
    int         score = 0;
    std::string snippet;       // a little of the text round the first match
};

// The topics every word of `query` appears in, best first: a word in the title
// counts for more. An empty query matches nothing.
std::vector<GuideHit> searchGuide(const std::vector<GuideTopic>& topics, const std::string& query);

} // namespace fast
