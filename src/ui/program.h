// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// program.h — what the program calls itself, and how it is run.
// Read by the window, the start screen and Help > About.

namespace fast {

// Runs the editor: its command line, its window, until it is closed. What
// main() does; the exit code is main's.
int runApp(int argc, char** argv);

namespace program {

constexpr const char* kTitle = "Sprit's'fast";
constexpr const char* kVersion = "1.0.0";     // when the build does not say
constexpr const char* kHomepage = "https://github.com/wray2064/sprits-fast";
constexpr const char* kIssues = "https://github.com/wray2064/sprits-fast/issues";
constexpr const char* kLicence = "Apache License 2.0";

} // namespace program
} // namespace fast
