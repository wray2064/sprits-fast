// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// tips.h — one line a person might not know, on the start screen.
//
// Each tip is something Fast really does and does not announce: a click, a
// key, a menu. They are the shortcuts and gestures a person finds by accident
// or never, written to be read in a few seconds. The one shown at a start
// moves on by one each time the program starts, so the same tip is not the
// greeting twice in a row; the person can step through them from the screen.

#include <string>
#include <vector>

namespace fast {

// Every tip, in English (the interface translates each through tr()).
const std::vector<std::string>& tips();

// The tip at `index`, which wraps: any number is a valid place in the list.
const std::string& tipAt(unsigned index);

} // namespace fast
