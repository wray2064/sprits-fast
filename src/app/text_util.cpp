// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/text_util.h"

#include <cctype>

namespace fast {

std::string trim(const std::string& text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) { ++a; }
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) { --b; }
    return text.substr(a, b - a);
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t at = text.find(by, start);
        parts.push_back(text.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            return parts;
        }
        start = at + 1;
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

bool isPlainId(const std::string& id) {
    if (id.empty() || id.size() > 48) {
        return false;
    }
    for (char ch : id) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
            return false;
        }
    }
    return true;
}

std::string escapeField(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '|':  out += "%7C"; break;
            case '\n': out += "%0A"; break;
            case '\r': out += "%0D"; break;
            case '%':  out += "%25"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string unescapeField(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 2 >= text.size()) {
            // A trailing, truncated escape is kept as the literal character it
            // is rather than reading past the end of the string.
            out.push_back(text[i]);
            continue;
        }
        const int high = hexDigit(text[i + 1]);
        const int low  = hexDigit(text[i + 2]);
        if (high < 0 || low < 0) {
            out.push_back(text[i]);
            continue;
        }
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
    }
    return out;
}

} // namespace fast
