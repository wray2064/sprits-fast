// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/preferences.h"

#include "app/document.h"
#include "app/file_io.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fast {

namespace {

std::string trim(const std::string& text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) { ++a; }
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) { --b; }
    return text.substr(a, b - a);
}

long long number(const std::string& text, long long low, long long high, long long fallback) {
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return fallback;
    }
    return std::clamp(value, low, high);
}

std::string hexColour(uint32_t rgb) {
    static const char digits[] = "0123456789abcdef";
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4) {
        out += digits[(rgb >> shift) & 0xF];
    }
    return out;
}

uint32_t colour(const std::string& text, uint32_t fallback) {
    if (text.size() != 7 || text[0] != '#') {
        return fallback;
    }
    uint32_t rgb = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<uint32_t>(c - 'a' + 10);
        } else {
            return fallback;
        }
        rgb = (rgb << 4) | digit;
    }
    return rgb;
}

} // namespace

std::string savePreferences(const Preferences& p) {
    std::string out = "# Sprit's'fast preferences\n";
    out += "new.width = " + std::to_string(p.newWidth) + "\n";
    out += "new.height = " + std::to_string(p.newHeight) + "\n";
    out += "new.background = " + std::to_string(p.newBackground) + "\n";
    out += "new.palette = " + std::to_string(p.newPreset) + "\n";
    out += "autosave.on = " + std::string(p.autosaveOn ? "1" : "0") + "\n";
    out += "autosave.seconds = " + std::to_string(p.autosaveSeconds) + "\n";
    out += "view.pixel-grid = " + std::string(p.pixelGrid ? "1" : "0") + "\n";
    out += "history.limit = " + std::to_string(p.historyLimit) + "\n";
    out += "view.checker-light = " + hexColour(p.checkerLight) + "\n";
    out += "view.checker-dark = " + hexColour(p.checkerDark) + "\n";
    out += "view.checker-size = " + std::to_string(p.checkerSize) + "\n";
    out += "view.grid-colour = " + hexColour(p.gridColour) + "\n";
    out += "view.grid-opacity = " + std::to_string(p.gridOpacity) + "\n";
    return out;
}

Preferences loadPreferences(const std::string& text) {
    Preferences p;
    size_t start = 0;
    size_t lines = 0;
    while (start < text.size() && lines < 256) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        ++lines;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos) {
            continue;
        }
        const std::string name = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (name == "new.width") {
            p.newWidth = static_cast<uint32_t>(number(value, 1, kMaxCanvasDimension, 32));
        } else if (name == "new.height") {
            p.newHeight = static_cast<uint32_t>(number(value, 1, kMaxCanvasDimension, 32));
        } else if (name == "new.background") {
            p.newBackground = static_cast<int>(number(value, 0, 3, 0));
        } else if (name == "new.palette") {
            p.newPreset = static_cast<int>(number(value, -1, 64, -1));
        } else if (name == "autosave.on") {
            p.autosaveOn = number(value, 0, 1, 1) == 1;
        } else if (name == "autosave.seconds") {
            p.autosaveSeconds = static_cast<uint32_t>(number(value, 30, 3600, 120));
        } else if (name == "view.pixel-grid") {
            p.pixelGrid = number(value, 0, 1, 1) == 1;
        } else if (name == "history.limit") {
            p.historyLimit = static_cast<uint32_t>(number(value, 10, 2000, 200));
        } else if (name == "view.checker-light") {
            p.checkerLight = colour(value, p.checkerLight);
        } else if (name == "view.checker-dark") {
            p.checkerDark = colour(value, p.checkerDark);
        } else if (name == "view.checker-size") {
            p.checkerSize = static_cast<int>(number(value, 2, 64, 8));
        } else if (name == "view.grid-colour") {
            p.gridColour = colour(value, p.gridColour);
        } else if (name == "view.grid-opacity") {
            p.gridOpacity = static_cast<int>(number(value, 0, 255, 16));
        }
    }
    // A new canvas has to be one Fast works on, whatever the two sides say.
    if (static_cast<uint64_t>(p.newWidth) * p.newHeight > kMaxCanvasPixels) {
        p.newWidth = 32;
        p.newHeight = 32;
    }
    return p;
}

std::string preferencesPath() {
    const std::string folder = preferencesDirectory();
    return folder.empty() ? std::string() : joinPath(folder, "preferences.txt");
}

std::string keymapPath() {
    const std::string folder = preferencesDirectory();
    return folder.empty() ? std::string() : joinPath(folder, "keys.txt");
}

} // namespace fast
