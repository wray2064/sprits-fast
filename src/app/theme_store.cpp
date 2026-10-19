// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/theme_store.h"

#include "app/file_io.h"

#include <algorithm>

namespace fast {

namespace {

const char* const kExtension = ".lstheme";

// An id that is a safe file name: what themeIdFromName makes.
bool plainId(const std::string& id) {
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

std::string pathFor(const std::string& folder, const std::string& id) {
    return joinPath(folder, id + kExtension);
}

} // namespace

std::string themesFolder() {
    const std::string settings = preferencesDirectory();
    return settings.empty() ? std::string() : settings + "/themes";
}

std::vector<Theme> loadCustomThemes(const std::string& folder) {
    std::vector<Theme> themes;
    if (folder.empty()) {
        return themes;
    }
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (themes.size() >= kMaxCustomThemes) {
            break;
        }
        if (entry.directory || !hasExtension(entry.name, kExtension) ||
            entry.bytes > kMaxThemeFileBytes) {
            continue;
        }
        std::vector<uint8_t> bytes;
        std::string error;
        if (!readFile(entry.path, bytes, &error) || bytes.size() > kMaxThemeFileBytes) {
            continue;
        }
        Theme theme;
        if (!parseTheme(std::string(bytes.begin(), bytes.end()), &theme, nullptr)) {
            continue;
        }
        // Never a built-in's id, never one already taken.
        bool taken = findBuiltinTheme(theme.id) != nullptr;
        for (const Theme& other : themes) {
            taken = taken || other.id == theme.id;
        }
        if (!taken) {
            themes.push_back(theme);
        }
    }
    return themes;
}

std::string uniqueThemeId(const std::string& folder, const std::string& name) {
    const std::string base = themeIdFromName(name);
    const auto free = [&](const std::string& id) {
        return findBuiltinTheme(id) == nullptr && !fileExists(pathFor(folder, id));
    };
    if (free(base)) {
        return base;
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string suffix = "-" + std::to_string(n);
        const std::string id = base.substr(0, 48 - suffix.size()) + suffix;
        if (free(id)) {
            return id;
        }
    }
    return base + "-x";
}

bool saveCustomTheme(const std::string& folder, const Theme& theme, std::string* savedId,
                     std::string* error) {
    if (folder.empty()) {
        if (error != nullptr) {
            *error = "there is no settings folder to keep it in";
        }
        return false;
    }
    Theme toSave = theme;
    toSave.id = themeIdFromName(toSave.name);
    if (findBuiltinTheme(toSave.id) != nullptr) {
        if (error != nullptr) {
            *error = "that name is a built-in theme's; choose another";
        }
        return false;
    }
    if (!createDirectory(folder)) {
        if (error != nullptr) {
            *error = "could not make the themes folder";
        }
        return false;
    }
    const std::string text = serializeTheme(toSave);
    if (!writeFileAtomic(pathFor(folder, toSave.id),
                         std::vector<uint8_t>(text.begin(), text.end()), error)) {
        return false;
    }
    if (savedId != nullptr) {
        *savedId = toSave.id;
    }
    return true;
}

bool deleteCustomTheme(const std::string& folder, const std::string& id) {
    if (folder.empty() || !plainId(id) || findBuiltinTheme(id) != nullptr) {
        return false;
    }
    const std::string path = pathFor(folder, id);
    return fileExists(path) && deleteFile(path);
}

bool findTheme(const std::string& folder, const std::string& id, Theme* out) {
    if (const Theme* builtin = findBuiltinTheme(id)) {
        if (out != nullptr) {
            *out = *builtin;
        }
        return true;
    }
    if (!plainId(id)) {
        return false;
    }
    for (const Theme& theme : loadCustomThemes(folder)) {
        if (theme.id == id) {
            if (out != nullptr) {
                *out = theme;
            }
            return true;
        }
    }
    return false;
}

} // namespace fast
