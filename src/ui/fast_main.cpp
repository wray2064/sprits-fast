// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// fast_main.cpp — Sprit's'fast: the editor, with nothing added.

#include "ui/app_extension.h"

int main(int argc, char** argv) {
    return fast::runApp(argc, argv, fast::AppExtension{});
}
