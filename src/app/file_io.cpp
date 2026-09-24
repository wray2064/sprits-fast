// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/file_io.h"

#include <cstdio>
#include <algorithm>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <cctype>
#include <cstdlib>

namespace fast {
namespace {

#if defined(_WIN32)

// UTF-8 to UTF-16. The whole reason this file exists: the narrow Windows API
// would read these bytes as the active code page and quietly mangle the name.
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), needed);
    return wide;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

std::FILE* openFile(const std::string& utf8Path, const wchar_t* mode) {
    const std::wstring wide = widen(utf8Path);
    if (wide.empty()) {
        return nullptr;
    }
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, wide.c_str(), mode) != 0) {
        return nullptr;
    }
    return file;
}

#else

std::FILE* openFile(const std::string& utf8Path, const char* mode) {
    // POSIX paths are bytes, and those bytes are already UTF-8.
    return std::fopen(utf8Path.c_str(), mode);
}

#endif

std::FILE* openForRead(const std::string& path) {
#if defined(_WIN32)
    return openFile(path, L"rb");
#else
    return openFile(path, "rb");
#endif
}

std::FILE* openForWrite(const std::string& path) {
#if defined(_WIN32)
    return openFile(path, L"wb");
#else
    return openFile(path, "wb");
#endif
}

size_t lastSeparator(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t back = path.find_last_of('\\');
    if (slash == std::string::npos) { return back; }
    if (back == std::string::npos)  { return slash; }
    return std::max(slash, back);
}

char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

bool readFile(const std::string& utf8Path, std::vector<uint8_t>& out, std::string* error) {
    std::FILE* file = openForRead(utf8Path);
    if (file == nullptr) {
        if (error) { *error = "could not open " + utf8Path; }
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        if (error) { *error = "could not measure " + utf8Path; }
        return false;
    }
    const long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        if (error) { *error = "could not measure " + utf8Path; }
        return false;
    }
    std::rewind(file);

    out.resize(static_cast<size_t>(size));
    const size_t read = size > 0 ? std::fread(out.data(), 1, static_cast<size_t>(size), file) : 0;
    std::fclose(file);

    if (read != static_cast<size_t>(size)) {
        if (error) { *error = "could not read all of " + utf8Path; }
        return false;
    }
    return true;
}

bool writeFileAtomic(const std::string& utf8Path, const std::vector<uint8_t>& bytes,
                     std::string* error) {
    // Beside the target rather than in a temp directory, so the rename stays on
    // one filesystem and is therefore atomic.
    const std::string temporary = utf8Path + ".saving";

    std::FILE* file = openForWrite(temporary);
    if (file == nullptr) {
        if (error) { *error = "could not open " + temporary + " for writing"; }
        return false;
    }

    const size_t written = bytes.empty() ? 0
                         : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool flushed = std::fflush(file) == 0;
    std::fclose(file);

    if (written != bytes.size() || !flushed) {
        deleteFile(temporary);
        if (error) { *error = "could not write " + utf8Path + " (disk full?)"; }
        return false;
    }

    // The swap. Everything above can fail without touching what is already
    // saved; this is the only step that replaces it.
#if defined(_WIN32)
    const std::wstring wideTemp = widen(temporary);
    const std::wstring wideTarget = widen(utf8Path);
    if (!MoveFileExW(wideTemp.c_str(), wideTarget.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        deleteFile(temporary);
        if (error) { *error = "could not replace " + utf8Path; }
        return false;
    }
#else
    if (std::rename(temporary.c_str(), utf8Path.c_str()) != 0) {
        deleteFile(temporary);
        if (error) { *error = "could not replace " + utf8Path; }
        return false;
    }
#endif
    return true;
}

bool fileExists(const std::string& utf8Path) {
#if defined(_WIN32)
    const std::wstring wide = widen(utf8Path);
    if (wide.empty()) { return false; }
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    return stat(utf8Path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

bool deleteFile(const std::string& utf8Path) {
#if defined(_WIN32)
    const std::wstring wide = widen(utf8Path);
    return !wide.empty() && DeleteFileW(wide.c_str()) != 0;
#else
    return unlink(utf8Path.c_str()) == 0;
#endif
}

std::string preferencesDirectory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    const std::string base = narrow(std::wstring(buffer, length));
    const std::string directory = base + "\\SpritsFast";
    CreateDirectoryW(widen(directory).c_str(), nullptr);
    return directory;
#else
    const char* home = std::getenv("XDG_CONFIG_HOME");
    std::string base = home != nullptr ? std::string(home) : std::string();
    if (base.empty()) {
        const char* fallback = std::getenv("HOME");
        if (fallback == nullptr) { return std::string(); }
        base = std::string(fallback) + "/.config";
    }
    const std::string directory = base + "/sprits-fast";
    mkdir(base.c_str(), 0755);
    mkdir(directory.c_str(), 0755);
    return directory;
#endif
}

std::string fileName(const std::string& utf8Path) {
    const size_t at = lastSeparator(utf8Path);
    return at == std::string::npos ? utf8Path : utf8Path.substr(at + 1);
}

std::string fileStem(const std::string& utf8Path) {
    const std::string name = fileName(utf8Path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string directoryOf(const std::string& utf8Path) {
    const size_t at = lastSeparator(utf8Path);
    return at == std::string::npos ? std::string() : utf8Path.substr(0, at);
}

bool hasExtension(const std::string& utf8Path, const std::string& dottedExtension) {
    const std::string name = fileName(utf8Path);
    if (name.size() < dottedExtension.size()) {
        return false;
    }
    // Extensions are compared case-insensitively in ASCII only. A UTF-8 tail is
    // compared byte for byte, which is right: lowercasing beyond ASCII needs a
    // locale, and an extension is not the place to take one on.
    size_t at = name.size() - dottedExtension.size();
    for (size_t i = 0; i < dottedExtension.size(); ++i) {
        if (lowerAscii(name[at + i]) != lowerAscii(dottedExtension[i])) {
            return false;
        }
    }
    return true;
}

std::string withExtension(const std::string& utf8Path, const std::string& dottedExtension) {
    return hasExtension(utf8Path, dottedExtension) ? utf8Path : utf8Path + dottedExtension;
}

bool directoryExists(const std::string& utf8Path) {
    if (utf8Path.empty()) {
        return false;
    }
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(widen(utf8Path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info {};
    return stat(utf8Path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return name;
    }
    const char last = directory.back();
    if (last == '/' || last == '\\') {
        return directory + name;
    }
#if defined(_WIN32)
    return directory + '\\' + name;
#else
    return directory + '/' + name;
#endif
}

std::string parentDirectory(const std::string& utf8Path) {
    std::string path = utf8Path;
    // A trailing separator is not a level of its own.
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    const size_t cut = path.find_last_of("/\\");
    if (cut == std::string::npos) {
        return std::string();
    }
    // "C:\" and "/" are roots: keep the separator, or the result names a
    // drive rather than its top.
    if (cut == 0) {
        return path.substr(0, 1);
    }
    if (cut == 2 && path.size() > 2 && path[1] == ':') {
        return path.substr(0, 3);
    }
    return path.substr(0, cut);
}

std::vector<DirectoryEntry> listDirectory(const std::string& utf8Path) {
    std::vector<DirectoryEntry> out;
    if (utf8Path.empty()) {
        return out;
    }
#if defined(_WIN32)
    WIN32_FIND_DATAW found {};
    const std::wstring pattern = widen(joinPath(utf8Path, "*"));
    HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        const std::string name = narrow(found.cFileName);
        if (name == "." || name == ".." || name.empty() || name[0] == '.') {
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0) {
            continue;
        }
        DirectoryEntry entry;
        entry.name = name;
        entry.path = joinPath(utf8Path, name);
        entry.directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!entry.directory) {
            entry.bytes = (static_cast<uint64_t>(found.nFileSizeHigh) << 32) |
                          found.nFileSizeLow;
        }
        // FILETIME counts 100ns ticks from 1601; the offset to 1970 is fixed.
        const uint64_t ticks = (static_cast<uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) |
                               found.ftLastWriteTime.dwLowDateTime;
        entry.modifiedSeconds = ticks > 116444736000000000ull
            ? (ticks - 116444736000000000ull) / 10000000ull : 0;
        out.push_back(std::move(entry));
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
#else
    DIR* directory = opendir(utf8Path.c_str());
    if (directory == nullptr) {
        return out;
    }
    while (const struct dirent* item = readdir(directory)) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name.empty() || name[0] == '.') {
            continue;
        }
        DirectoryEntry entry;
        entry.name = name;
        entry.path = joinPath(utf8Path, name);
        struct stat info {};
        if (stat(entry.path.c_str(), &info) != 0) {
            continue;                 // vanished, or not ours to look at
        }
        entry.directory = S_ISDIR(info.st_mode);
        if (!entry.directory) {
            if (!S_ISREG(info.st_mode)) {
                continue;             // a socket or device is not a file to list
            }
            entry.bytes = static_cast<uint64_t>(info.st_size);
        }
        entry.modifiedSeconds = static_cast<uint64_t>(info.st_mtime);
        out.push_back(std::move(entry));
    }
    closedir(directory);
#endif

    std::sort(out.begin(), out.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        if (a.directory != b.directory) {
            return a.directory;       // folders first
        }
        // Case-insensitive for ASCII, which is what a listing needs to read
        // alphabetically; anything else falls back to byte order rather than
        // pretending to know the user's collation.
        const size_t shared = std::min(a.name.size(), b.name.size());
        for (size_t i = 0; i < shared; ++i) {
            const unsigned char left = static_cast<unsigned char>(a.name[i]);
            const unsigned char right = static_cast<unsigned char>(b.name[i]);
            const int lowerLeft = std::tolower(left);
            const int lowerRight = std::tolower(right);
            if (lowerLeft != lowerRight) {
                return lowerLeft < lowerRight;
            }
        }
        return a.name.size() < b.name.size();
    });
    return out;
}

std::string documentsDirectory() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path)) &&
        path != nullptr) {
        const std::string out = narrow(path);
        CoTaskMemFree(path);
        return out;
    }
    return std::string();
#else
    if (const char* home = std::getenv("HOME")) {
        const std::string documents = joinPath(home, "Documents");
        if (directoryExists(documents)) {
            return documents;
        }
        return home;
    }
    return std::string();
#endif
}

} // namespace fast
