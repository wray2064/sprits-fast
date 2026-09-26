// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/i18n.h"

#include "app/file_io.h"

#include <algorithm>
#include <unordered_map>

namespace fast {

namespace {

std::unordered_map<std::string, std::string>& catalogue() {
    static std::unordered_map<std::string, std::string> table;
    return table;
}

std::string trim(const std::string& text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r')) { ++a; }
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r')) { --b; }
    return text.substr(a, b - a);
}

std::string unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[i + 1];
            if (next == 'n') { out += '\n'; ++i; continue; }
            if (next == '=') { out += '='; ++i; continue; }
            if (next == '\\') { out += '\\'; ++i; continue; }
        }
        out += text[i];
    }
    return out;
}

// The first "=" that is not escaped.
size_t separator(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
            continue;
        }
        if (line[i] == '=') {
            return i;
        }
    }
    return std::string::npos;
}

} // namespace

size_t loadCatalogue(const std::string& text) {
    std::unordered_map<std::string, std::string> table;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t at = separator(line);
        if (at == std::string::npos) {
            continue;
        }
        const std::string english = unescape(trim(line.substr(0, at)));
        const std::string translated = unescape(trim(line.substr(at + 1)));
        if (english.empty() || translated.empty() || english.size() > 1024 ||
            translated.size() > 1024) {
            continue;
        }
        table[english] = translated;
    }
    catalogue() = std::move(table);
    return catalogue().size();
}

void clearCatalogue() {
    catalogue().clear();
}

const char* tr(const char* english) {
    if (english == nullptr || catalogue().empty()) {
        return english;
    }
    const auto it = catalogue().find(english);
    return it == catalogue().end() ? english : it->second.c_str();
}

std::vector<std::string> languagesIn(const std::string& folder) {
    std::vector<std::string> out;
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (!entry.directory && hasExtension(entry.name, ".txt")) {
            out.push_back(fileStem(entry.name));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace fast
