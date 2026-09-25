// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/os_clipboard.h"

#include "app/clip_image.h"
#include "app/image_io.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fast {

namespace {

std::vector<uint8_t> pngOf(const ls::RasterBuffer& image) {
    std::vector<uint8_t> png;
    std::string error;
    if (!encodeImageAsPng(image, &png, &error)) {
        png.clear();
    }
    return png;
}

#if defined(_WIN32)

HWND  owner = nullptr;
DWORD seenSequence = 0;

UINT pngFormat() {
    static const UINT format = RegisterClipboardFormatW(L"PNG");
    return format;
}

// Another program can hold the clipboard open for a moment; wait briefly
// rather than fail the copy.
bool openClipboard() {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

bool offer(UINT format, const uint8_t* data, size_t size) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory == nullptr) {
        return false;
    }
    void* at = GlobalLock(memory);
    if (at == nullptr) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(at, data, size);
    GlobalUnlock(memory);
    if (SetClipboardData(format, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;           // the clipboard owns it now
}

std::vector<uint8_t> take(UINT format) {
    std::vector<uint8_t> bytes;
    HANDLE memory = GetClipboardData(format);
    if (memory == nullptr) {
        return bytes;
    }
    const size_t size = GlobalSize(memory);
    const void* at = GlobalLock(memory);
    if (at != nullptr && size > 0) {
        bytes.assign(static_cast<const uint8_t*>(at), static_cast<const uint8_t*>(at) + size);
    }
    GlobalUnlock(memory);
    return bytes;
}

// A DIB as a BMP file: the file header in front, pointing past the header
// and whatever colour table follows it. The masks of a BITFIELDS bitmap sit
// after a plain header but inside a V4 or V5 one, which is the case SDL gets
// wrong.
std::vector<uint8_t> bmpOfDib(const std::vector<uint8_t>& dib) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) {
        return {};
    }
    BITMAPINFOHEADER header;
    std::memcpy(&header, dib.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > dib.size()) {
        return {};
    }
    size_t table = 0;
    if (header.biBitCount <= 8) {
        table = sizeof(RGBQUAD) * (header.biClrUsed != 0 ? header.biClrUsed
                                                         : (1u << header.biBitCount));
    } else if (header.biSize == sizeof(BITMAPINFOHEADER)) {
        if (header.biCompression == BI_BITFIELDS) {
            table = 3 * sizeof(DWORD);
        } else if (header.biCompression == 6) {             // BI_ALPHABITFIELDS
            table = 4 * sizeof(DWORD);
        }
        table += sizeof(RGBQUAD) * header.biClrUsed;
    } else {
        table = sizeof(RGBQUAD) * header.biClrUsed;
    }
    const size_t offset = 14 + header.biSize + table;
    if (offset - 14 > dib.size()) {
        return {};
    }
    std::vector<uint8_t> bmp;
    bmp.reserve(14 + dib.size());
    const uint32_t total = static_cast<uint32_t>(14 + dib.size());
    const uint32_t off = static_cast<uint32_t>(offset);
    const uint8_t file[14] = {
        'B', 'M',
        uint8_t(total), uint8_t(total >> 8), uint8_t(total >> 16), uint8_t(total >> 24),
        0, 0, 0, 0,
        uint8_t(off), uint8_t(off >> 8), uint8_t(off >> 16), uint8_t(off >> 24),
    };
    bmp.insert(bmp.end(), file, file + 14);
    bmp.insert(bmp.end(), dib.begin(), dib.end());
    return bmp;
}

#endif

} // namespace

void initSystemClipboard(SDL_Window* window) {
#if defined(_WIN32)
    if (window != nullptr) {
        owner = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }
#else
    (void)window;
#endif
}

#if defined(_WIN32)

bool putImageOnClipboard(const ls::RasterBuffer& image) {
    if (image.empty() || !openClipboard()) {
        return false;
    }
    EmptyClipboard();
    const std::vector<uint8_t> png = pngOf(image);
    const std::vector<uint8_t> bmp = encodeBmp(image);
    bool any = false;
    // PNG first: the programs that look for it look for it first, and it is
    // the one that keeps alpha everywhere.
    if (!png.empty()) {
        any = offer(pngFormat(), png.data(), png.size()) || any;
    }
    if (bmp.size() > 14) {
        any = offer(CF_DIBV5, bmp.data() + 14, bmp.size() - 14) || any;
    }
    CloseClipboard();
    seenSequence = GetClipboardSequenceNumber();
    return any;
}

bool clipboardHasImage() {
    return IsClipboardFormatAvailable(pngFormat()) || IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB);
}

bool clipboardChangedElsewhere() {
    return GetClipboardSequenceNumber() != seenSequence;
}

void markClipboardSeen() {
    seenSequence = GetClipboardSequenceNumber();
}

void noteClipboardEvent(bool) {}

bool imageFromClipboard(ls::RasterBuffer* out, std::string* error) {
    if (!openClipboard()) {
        if (error != nullptr) {
            *error = "Another program is holding the clipboard";
        }
        return false;
    }
    std::vector<uint8_t> png;
    std::vector<uint8_t> bmp;
    if (IsClipboardFormatAvailable(pngFormat())) {
        png = take(pngFormat());
    }
    // V5 first: it is the one with alpha. Windows makes either from the other.
    if (IsClipboardFormatAvailable(CF_DIBV5)) {
        bmp = bmpOfDib(take(CF_DIBV5));
    } else if (IsClipboardFormatAvailable(CF_DIB)) {
        bmp = bmpOfDib(take(CF_DIB));
    }
    CloseClipboard();
    std::string reason;
    if (!png.empty() && decodeImage(png, out, &reason)) {
        return true;
    }
    if (!bmp.empty() && decodeImage(bmp, out, &reason)) {
        return true;
    }
    if (error != nullptr) {
        *error = reason.empty() ? "There is no image on the clipboard" : reason;
    }
    return false;
}

#else

namespace {

const char* const kPng = "image/png";
const char* const kBmp = "image/bmp";

// Whether another program changed the clipboard since Fast last touched it,
// as SDL's events tell it. Windows asks the clipboard's own counter instead.
bool changedElsewhere = true;

// What is on offer, held until SDL says the clipboard has moved on.
struct Offer {
    std::vector<uint8_t> png;
    std::vector<uint8_t> bmp;
};

const void* SDLCALL offerData(void* userdata, const char* mimeType, size_t* size) {
    const Offer* held = static_cast<const Offer*>(userdata);
    const std::vector<uint8_t>* bytes = nullptr;
    if (mimeType != nullptr && std::strcmp(mimeType, kPng) == 0) {
        bytes = &held->png;
    } else if (mimeType != nullptr && std::strcmp(mimeType, kBmp) == 0) {
        bytes = &held->bmp;
    }
    if (bytes == nullptr || bytes->empty()) {
        *size = 0;
        return nullptr;
    }
    *size = bytes->size();
    return bytes->data();
}

void SDLCALL dropOffer(void* userdata) {
    delete static_cast<Offer*>(userdata);
}

} // namespace

bool putImageOnClipboard(const ls::RasterBuffer& image) {
    if (image.empty()) {
        return false;
    }
    Offer* held = new Offer;
    held->png = pngOf(image);
    held->bmp = encodeBmp(image);
    const char* types[] = { kPng, kBmp };
    // SDL owns the offer from here, and frees it through dropOffer.
    const bool put = SDL_SetClipboardData(offerData, dropOffer, held, types, 2);
    changedElsewhere = false;
    return put;
}

bool clipboardHasImage() {
    return SDL_HasClipboardData(kPng) || SDL_HasClipboardData(kBmp);
}

bool clipboardChangedElsewhere() {
    return changedElsewhere;
}

void markClipboardSeen() {
    changedElsewhere = false;
}

void noteClipboardEvent(bool owner) {
    if (!owner) {
        changedElsewhere = true;
    }
}

bool imageFromClipboard(ls::RasterBuffer* out, std::string* error) {
    std::string reason;
    for (const char* type : { kPng, kBmp }) {
        if (!SDL_HasClipboardData(type)) {
            continue;
        }
        size_t size = 0;
        void* data = SDL_GetClipboardData(type, &size);
        if (data == nullptr || size == 0) {
            SDL_free(data);
            continue;
        }
        const std::vector<uint8_t> bytes(static_cast<uint8_t*>(data),
                                         static_cast<uint8_t*>(data) + size);
        SDL_free(data);
        if (decodeImage(bytes, out, &reason)) {
            return true;
        }
    }
    if (error != nullptr) {
        *error = reason.empty() ? "There is no image on the clipboard" : reason;
    }
    return false;
}

#endif

} // namespace fast
