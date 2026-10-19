// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// libfuzzer_main.cpp — the coverage-guided entry point, built by build-fuzz.bat
// with AddressSanitizer and libFuzzer. One executable serves every target: the
// one to run is named in the environment variable FAST_FUZZ_TARGET, so each
// keeps a corpus of its own.
//
//     set FAST_FUZZ_TARGET=aseprite
//     fast_libfuzzer.exe corpus\aseprite -max_total_time=600

#include "fuzz/fuzz_targets.h"

#include <cstdio>
#include <cstdlib>

namespace {
const fastfuzz::Target* gTarget = nullptr;
}

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    const char* name = std::getenv("FAST_FUZZ_TARGET");
    gTarget = name != nullptr ? fastfuzz::findTarget(name) : nullptr;
    if (gTarget == nullptr) {
        std::fprintf(stderr, "Set FAST_FUZZ_TARGET to one of:\n");
        for (const fastfuzz::Target& target : fastfuzz::targets()) {
            std::fprintf(stderr, "  %-16s %s\n", target.name, target.about);
        }
        std::exit(2);
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    gTarget->run(data, size);
    return 0;
}
