// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// fast_main.cpp — Sprit's'fast: the editor, with nothing added.

#include "ui/app_extension.h"

// A windowed program on Windows starts at WinMain, not main; SDL's header
// supplies the one and calls the other, on every platform the same.
#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
    return fast::runApp(argc, argv, fast::AppExtension{});
}
