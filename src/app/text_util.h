// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// text_util.h — the small string helpers every parser here needs, once.
//
// Fast reads a good many little text formats (keys, preferences, themes,
// palettes, references, animations, slices, translations). They each need to
// trim a line, split it, lower-case a word, or write a name that may hold the
// separator; those were written again in every file, and are here instead so
// that a fix to one is a fix to all.

#include <string>
#include <vector>

namespace fast {

// `text` without the white space at both ends (space, tab, CR, LF, VT, FF).
std::string trim(const std::string& text);

// `text` with its ASCII letters in lower case.
std::string lower(std::string text);

// Every field of `text` between `by`s, the empty ones too: "a||b" is three, and
// "" is one empty field.
std::vector<std::string> split(const std::string& text, char by);

// The value of a hex digit, 0 to 15, or -1 for anything else.
int hexDigit(char c);

// Whether `id` is safe as a file name and as a key in a file: lower-case
// letters, digits and dashes, one to 48 of them. What a theme or a saved layout
// is called on disk, so an id read from a file can never name a path.
bool isPlainId(const std::string& id);

// A name written into a line of fields separated by `|`: the `|`, a newline, a
// carriage return and `%` itself are written as %7C, %0A, %0D and %25, so the
// name can hold any of them and the line still splits where it should.
// unescapeField reads that back; a `%` that does not start an escape (cut off, or
// not hex) is kept as it is rather than read past the end of the string.
std::string escapeField(const std::string& text);
std::string unescapeField(const std::string& text);

} // namespace fast
