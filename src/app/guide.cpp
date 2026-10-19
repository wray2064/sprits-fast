// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/guide.h"

#include "app/text_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fast {

namespace {

constexpr size_t kMaxRuns = 4000;        // in one line
constexpr size_t kMaxLineLength = 4000;

void flush(std::string* plain, std::vector<Run>* runs) {
    if (!plain->empty()) {
        Run run;
        run.text = *plain;
        runs->push_back(run);
        plain->clear();
    }
}

bool isRule(const std::string& line) {
    if (line.size() < 3) {
        return false;
    }
    for (char c : line) {
        if (c != '-') {
            return false;
        }
    }
    return true;
}

bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

size_t indentOf(const std::string& line) {
    size_t n = 0;
    while (n < line.size() && line[n] == ' ') {
        ++n;
    }
    return n;
}

// "- item" or "* item": the text of a bullet, or false.
bool bulletText(const std::string& body, std::string* out) {
    if (body.size() >= 2 && (body[0] == '-' || body[0] == '*') && body[1] == ' ') {
        *out = trim(body.substr(2));
        return true;
    }
    return false;
}

// "12. item": the number and the text, or false.
bool numberedText(const std::string& body, int* number, std::string* out) {
    size_t digits = 0;
    while (digits < body.size() && digits < 6 && std::isdigit(static_cast<unsigned char>(body[digits]))) {
        ++digits;
    }
    if (digits == 0 || digits + 1 >= body.size() || body[digits] != '.' || body[digits + 1] != ' ') {
        return false;
    }
    *number = std::atoi(body.substr(0, digits).c_str());
    *out = trim(body.substr(digits + 2));
    return true;
}

bool isTableRow(const std::string& line) {
    const std::string t = trim(line);
    return t.size() >= 2 && t[0] == '|';
}

bool isSeparatorRow(const std::string& line) {
    const std::string t = trim(line);
    bool dash = false;
    for (char c : t) {
        if (c == '-') {
            dash = true;
        } else if (c != '|' && c != ':' && c != ' ') {
            return false;
        }
    }
    return dash;
}

std::vector<std::string> cellsOf(const std::string& line) {
    std::string t = trim(line);
    if (!t.empty() && t.front() == '|') {
        t.erase(0, 1);
    }
    if (!t.empty() && t.back() == '|') {
        t.pop_back();
    }
    std::vector<std::string> cells;
    std::string current;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            current += '|';
            ++i;
        } else if (t[i] == '|') {
            cells.push_back(trim(current));
            current.clear();
        } else {
            current += t[i];
        }
    }
    cells.push_back(trim(current));
    return cells;
}

std::string flatten(const std::vector<Run>& runs) {
    std::string out;
    for (const Run& run : runs) {
        out += run.text;
    }
    return out;
}

} // namespace

std::vector<Run> parseInline(const std::string& textIn) {
    const std::string text = textIn.size() > kMaxLineLength ? textIn.substr(0, kMaxLineLength) : textIn;
    std::vector<Run> runs;
    std::string plain;
    size_t i = 0;
    while (i < text.size() && runs.size() < kMaxRuns) {
        const char c = text[i];
        // A backslash makes the next marker an ordinary character.
        if (c == '\\' && i + 1 < text.size() && std::string("\\`*[]()|").find(text[i + 1]) != std::string::npos) {
            plain += text[i + 1];
            i += 2;
            continue;
        }
        if (c == '`') {
            const size_t end = text.find('`', i + 1);
            if (end != std::string::npos && end > i + 1) {
                flush(&plain, &runs);
                Run run;
                run.text = text.substr(i + 1, end - i - 1);
                run.style = RunStyle::Code;
                runs.push_back(run);
                i = end + 1;
                continue;
            }
        }
        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const size_t end = text.find("**", i + 2);
            if (end != std::string::npos && end > i + 2) {
                flush(&plain, &runs);
                Run run;
                run.text = text.substr(i + 2, end - i - 2);
                run.style = RunStyle::Bold;
                runs.push_back(run);
                i = end + 2;
                continue;
            }
        }
        if (c == '*' && i + 1 < text.size() && text[i + 1] != ' ' && text[i + 1] != '*') {
            const size_t end = text.find('*', i + 1);
            if (end != std::string::npos && end > i + 1 && text[end - 1] != ' ') {
                flush(&plain, &runs);
                Run run;
                run.text = text.substr(i + 1, end - i - 1);
                run.style = RunStyle::Italic;
                runs.push_back(run);
                i = end + 1;
                continue;
            }
        }
        if (c == '[') {
            const size_t close = text.find(']', i + 1);
            if (close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(') {
                const size_t end = text.find(')', close + 2);
                if (end != std::string::npos && close > i + 1 && end > close + 2) {
                    flush(&plain, &runs);
                    Run run;
                    run.text = text.substr(i + 1, close - i - 1);
                    run.style = RunStyle::Link;
                    run.target = text.substr(close + 2, end - close - 2);
                    runs.push_back(run);
                    i = end + 1;
                    continue;
                }
            }
        }
        plain += c;
        ++i;
    }
    // Whatever was left over when the run limit stopped it is kept as text.
    if (i < text.size()) {
        plain += text.substr(i);
    }
    flush(&plain, &runs);
    return runs;
}

std::vector<Block> parseBlocks(const std::string& markdownIn) {
    const std::string markdown = markdownIn.size() > kMaxGuideBytes ? markdownIn.substr(0, kMaxGuideBytes)
                                                                     : markdownIn;
    std::vector<std::string> lines;
    for (std::string line : split(markdown, '\n')) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() > kMaxLineLength) {
            line.resize(kMaxLineLength);
        }
        lines.push_back(line);
    }

    std::vector<Block> blocks;
    std::string paragraph;          // the lines of the paragraph being read, joined
    size_t cells = 0;
    const auto endParagraph = [&]() {
        if (!paragraph.empty()) {
            Block block;
            block.kind = BlockKind::Paragraph;
            block.runs = parseInline(paragraph);
            blocks.push_back(block);
            paragraph.clear();
        }
    };
    const auto join = [](std::string* into, const std::string& more) {
        if (!into->empty()) {
            *into += ' ';
        }
        *into += more;
    };

    for (size_t at = 0; at < lines.size() && blocks.size() < kMaxGuideBlocks; ++at) {
        const std::string& line = lines[at];
        const std::string body = trim(line);

        if (startsWith(body, "```")) {
            endParagraph();
            Block block;
            block.kind = BlockKind::Code;
            for (++at; at < lines.size() && !startsWith(trim(lines[at]), "```"); ++at) {
                if (block.lines.size() < 500) {
                    block.lines.push_back(lines[at]);
                }
            }
            blocks.push_back(block);
            continue;
        }
        if (body.empty()) {
            endParagraph();
            continue;
        }
        if (isRule(body)) {
            endParagraph();
            Block block;
            block.kind = BlockKind::Rule;
            blocks.push_back(block);
            continue;
        }
        if (body[0] == '#') {
            size_t level = 0;
            while (level < body.size() && body[level] == '#') {
                ++level;
            }
            if (level <= 3 && level < body.size() && body[level] == ' ') {
                endParagraph();
                Block block;
                block.kind = BlockKind::Heading;
                block.level = static_cast<int>(level);
                block.runs = parseInline(trim(body.substr(level + 1)));
                blocks.push_back(block);
                continue;
            }
        }
        if (isTableRow(line)) {
            endParagraph();
            Block block;
            block.kind = BlockKind::Table;
            for (; at < lines.size() && isTableRow(lines[at]); ++at) {
                if (isSeparatorRow(lines[at])) {
                    continue;
                }
                std::vector<std::vector<Run>> row;
                for (const std::string& cell : cellsOf(lines[at])) {
                    if (++cells > kMaxTableCells) {
                        break;
                    }
                    row.push_back(parseInline(cell));
                }
                block.rows.push_back(row);
            }
            --at;
            blocks.push_back(block);
            continue;
        }
        if (body[0] == '>') {
            endParagraph();
            Block block;
            block.kind = BlockKind::Note;
            std::string text = trim(body.substr(1));
            while (at + 1 < lines.size() && trim(lines[at + 1]).rfind(">", 0) == 0) {
                join(&text, trim(trim(lines[++at]).substr(1)));
            }
            block.runs = parseInline(text);
            blocks.push_back(block);
            continue;
        }
        std::string item;
        int number = 0;
        const size_t indent = indentOf(line);
        if (bulletText(body, &item) || numberedText(body, &number, &item)) {
            endParagraph();
            Block block;
            block.kind = number > 0 ? BlockKind::Numbered : BlockKind::Bullet;
            block.number = number;
            block.level = indent >= 2 ? 1 : 0;
            // Indented lines after it, which are not a new item, continue it.
            while (at + 1 < lines.size()) {
                const std::string& next = lines[at + 1];
                const std::string nextBody = trim(next);
                std::string ignored;
                int ignoredNumber = 0;
                if (nextBody.empty() || indentOf(next) < 2 || bulletText(nextBody, &ignored) ||
                    numberedText(nextBody, &ignoredNumber, &ignored) || isTableRow(next)) {
                    break;
                }
                join(&item, nextBody);
                ++at;
            }
            block.runs = parseInline(item);
            blocks.push_back(block);
            continue;
        }
        join(&paragraph, body);
    }
    endParagraph();
    return blocks;
}

std::string topicIdFromFileName(const std::string& fileName) {
    std::string name = fileName;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
        name.erase(dot);
    }
    // A leading number only orders the files.
    size_t digits = 0;
    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))) {
        ++digits;
    }
    if (digits > 0 && digits < name.size() && (name[digits] == '-' || name[digits] == '_')) {
        name.erase(0, digits + 1);
    }
    return name;
}

GuideTopic parseTopic(const GuideSource& source) {
    GuideTopic topic;
    topic.id = source.id;
    std::vector<Block> blocks = parseBlocks(source.text);
    // The title is the first heading of level one, if the topic opens with it.
    if (!blocks.empty() && blocks.front().kind == BlockKind::Heading && blocks.front().level == 1) {
        topic.title = flatten(blocks.front().runs);
        blocks.erase(blocks.begin());
    } else {
        topic.title = source.id;
    }
    topic.blocks = std::move(blocks);
    std::string flat = topic.title;
    for (const Block& block : topic.blocks) {
        flat += ' ';
        flat += flatten(block.runs);
        for (const auto& row : block.rows) {
            for (const auto& cell : row) {
                flat += ' ';
                flat += flatten(cell);
            }
        }
        for (const std::string& line : block.lines) {
            flat += ' ';
            flat += line;
        }
    }
    topic.flat = flat;
    return topic;
}

std::vector<GuideTopic> parseGuide(const std::vector<GuideSource>& sources) {
    std::vector<GuideTopic> topics;
    for (const GuideSource& source : sources) {
        topics.push_back(parseTopic(source));
    }
    return topics;
}

std::string firstBrokenLink(const std::vector<GuideTopic>& topics) {
    const auto exists = [&](const std::string& id) {
        return std::any_of(topics.begin(), topics.end(),
                           [&](const GuideTopic& topic) { return topic.id == id; });
    };
    const auto check = [&](const std::vector<Run>& runs, std::string* broken) {
        for (const Run& run : runs) {
            if (run.style == RunStyle::Link && run.target.rfind("topic:", 0) == 0 &&
                !exists(run.target.substr(6))) {
                *broken = run.target;
                return false;
            }
        }
        return true;
    };
    for (const GuideTopic& topic : topics) {
        for (const Block& block : topic.blocks) {
            std::string broken;
            if (!check(block.runs, &broken)) {
                return broken;
            }
            for (const auto& row : block.rows) {
                for (const auto& cell : row) {
                    if (!check(cell, &broken)) {
                        return broken;
                    }
                }
            }
        }
    }
    return "";
}

std::vector<GuideHit> searchGuide(const std::vector<GuideTopic>& topics, const std::string& query) {
    std::vector<std::string> words;
    for (const std::string& word : split(lower(trim(query)), ' ')) {
        if (!word.empty() && words.size() < 8) {
            words.push_back(word);
        }
    }
    std::vector<GuideHit> hits;
    if (words.empty()) {
        return hits;
    }
    for (size_t i = 0; i < topics.size(); ++i) {
        const std::string text = lower(topics[i].flat);
        const std::string title = lower(topics[i].title);
        int score = 0;
        size_t firstAt = std::string::npos;
        bool all = true;
        for (const std::string& word : words) {
            const size_t at = text.find(word);
            if (at == std::string::npos) {
                all = false;
                break;
            }
            firstAt = std::min(firstAt, at);
            score += title.find(word) != std::string::npos ? 20 : 0;
            // How often, up to a few: a topic about it beats a passing mention.
            int count = 0;
            for (size_t from = at; from != std::string::npos && count < 8;
                 from = text.find(word, from + word.size())) {
                ++count;
            }
            score += count;
        }
        if (!all) {
            continue;
        }
        GuideHit hit;
        hit.topic = i;
        hit.score = score;
        // A snippet from the text, not the title the topic already shows.
        const size_t body = topics[i].title.size() + 1;
        const size_t from = std::max(firstAt > 40 ? firstAt - 40 : size_t(0),
                                     std::min(body, firstAt));
        hit.snippet = (from > 0 ? "..." : "") + topics[i].flat.substr(from, 110) +
                      (from + 110 < topics[i].flat.size() ? "..." : "");
        hits.push_back(hit);
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const GuideHit& a, const GuideHit& b) { return a.score > b.score; });
    return hits;
}

} // namespace fast
