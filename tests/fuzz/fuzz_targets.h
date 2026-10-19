// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// fuzz_targets.h — every place Fast reads data it did not write.
//
// A file a person opens, a theme or a palette they were sent, a settings file
// that was edited by hand or half written when the power went: all of it is
// untrusted, and none of it may crash the program, hang it, or make it allocate
// without bound. Each *target* here takes arbitrary bytes, hands them to one
// reader the way the program does, and then uses what came out. A target
// returns normally for any input -- a bad one is refused, not fatal -- and
// aborts only for a bug: an invariant that must hold of whatever was read.
//
// Two drivers run the same targets. fuzz_driver.cpp mutates a seed corpus with a
// seeded random generator and is quick enough for every test run; the
// libFuzzer entry point (libfuzzer_main.cpp), built with AddressSanitizer by
// build-fuzz.bat, is guided by coverage and is what runs for hours. Inputs that
// ever failed are kept in tests/fuzz/regressions/<target>/ and run every time.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fastfuzz {

using Bytes = std::vector<uint8_t>;

struct Target {
    const char* name;
    const char* about;
    void (*run)(const uint8_t* data, size_t size);
    // Valid inputs of this kind to start from: mutated, they reach further into a
    // reader than random bytes can. May be null.
    std::vector<Bytes> (*seeds)();
};

const std::vector<Target>& targets();
const Target* findTarget(const char* name);

} // namespace fastfuzz
