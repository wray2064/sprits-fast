// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// text_util_tests.cpp — the string helpers every parser in Fast leans on.
//
// They are small, which is why they were once written again in each file, and why
// a slip in one used to be a slip in a single format. Held here so that a change to
// one is a change to all, with the edge cases a hand-edited file will find: nothing
// at all, only white space, a field that holds the separator, an escape cut off.

#include "app/text_util.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

using namespace fast;

namespace {

void trimming() {
    CHECK(trim("") == "");
    CHECK(trim("   ") == "");
    CHECK(trim(" \t\r\n\v\f") == "");
    CHECK(trim("word") == "word");
    CHECK(trim("  two words \r\n") == "two words");
    CHECK(trim("\tx") == "x");
    CHECK(trim("x\t") == "x");
    // Only the ends: the inside is left as it is.
    CHECK(trim(" a  b ") == "a  b");
    // A byte above 127 is not white space, whatever the locale thinks.
    CHECK(trim("\xA0x\xA0") == "\xA0x\xA0");
}

void lowering() {
    CHECK(lower("") == "");
    CHECK(lower("ABC def 123") == "abc def 123");
    CHECK(lower("already") == "already");
    // ASCII only: an accented capital stays as its bytes are.
    CHECK(lower("\xC3\x89") == "\xC3\x89");
}

void splitting() {
    CHECK(split("", '|').size() == 1);
    CHECK(split("", '|')[0].empty());
    CHECK(split("a", '|').size() == 1);
    CHECK(split("a|b|c", '|').size() == 3);
    CHECK(split("a|b|c", '|')[2] == "c");
    // The empty fields count: "a||b" has three, "|" has two, "||" three.
    CHECK(split("a||b", '|').size() == 3);
    CHECK(split("a||b", '|')[1].empty());
    CHECK(split("|", '|').size() == 2);
    CHECK(split("||", '|').size() == 3);
    CHECK(split("a|", '|').size() == 2);
    CHECK(split("|a", '|')[0].empty());
}

void hexDigits() {
    for (char c = '0'; c <= '9'; ++c) { CHECK(hexDigit(c) == c - '0'); }
    for (char c = 'a'; c <= 'f'; ++c) { CHECK(hexDigit(c) == 10 + c - 'a'); }
    for (char c = 'A'; c <= 'F'; ++c) { CHECK(hexDigit(c) == 10 + c - 'A'); }
    CHECK(hexDigit('g') == -1);
    CHECK(hexDigit('G') == -1);
    CHECK(hexDigit(' ') == -1);
    CHECK(hexDigit('\0') == -1);
    CHECK(hexDigit('/') == -1);          // just before '0'
    CHECK(hexDigit(':') == -1);          // just after '9'
    CHECK(hexDigit(static_cast<char>(0xFF)) == -1);
}

void plainIds() {
    CHECK(isPlainId("midnight"));
    CHECK(isPlainId("a"));
    CHECK(isPlainId("my-night-2"));
    CHECK(isPlainId("0"));
    CHECK(!isPlainId(""));
    CHECK(!isPlainId("Midnight"));       // upper case
    CHECK(!isPlainId("my night"));       // a space
    CHECK(!isPlainId("my_night"));       // an underscore
    CHECK(!isPlainId("../etc"));         // a path
    CHECK(!isPlainId("a/b"));
    CHECK(!isPlainId("a\\b"));
    CHECK(!isPlainId("a.b"));
    CHECK(!isPlainId(std::string("a\0b", 3)));
    // The limit is 48.
    CHECK(isPlainId(std::string(48, 'a')));
    CHECK(!isPlainId(std::string(49, 'a')));
}

void fields() {
    CHECK(escapeField("") == "");
    CHECK(escapeField("plain") == "plain");
    CHECK(escapeField("a|b") == "a%7Cb");
    CHECK(escapeField("a\nb") == "a%0Ab");
    CHECK(escapeField("a\rb") == "a%0Db");
    CHECK(escapeField("100%") == "100%25");
    // What it writes has none of the characters it escapes, except the % of an escape.
    const std::string nasty = "|\n\r%|%7C";
    const std::string written = escapeField(nasty);
    CHECK(written.find('|') == std::string::npos);
    CHECK(written.find('\n') == std::string::npos);
    CHECK(written.find('\r') == std::string::npos);
    CHECK(split(written, '|').size() == 1);
    // And reads back as it was.
    CHECK(unescapeField(written) == nasty);
    for (const char* name : { "", "a", "a|b", "|", "%", "%%", "%7C", "100% sure|really\n", "\xC3\xA9|\xC3\xA9" }) {
        CHECK(unescapeField(escapeField(name)) == name);
    }

    // Reading what a person (or a fuzzer) wrote: an escape that is cut off or is not
    // hex is kept as the characters it is.
    CHECK(unescapeField("%") == "%");
    CHECK(unescapeField("%7") == "%7");
    CHECK(unescapeField("a%") == "a%");
    CHECK(unescapeField("a%4") == "a%4");
    CHECK(unescapeField("%zz") == "%zz");
    CHECK(unescapeField("%4z") == "%4z");
    CHECK(unescapeField("%41") == "A");
    CHECK(unescapeField("%41%42") == "AB");
    CHECK(unescapeField("%7c") == "|");          // lower-case hex reads too
    CHECK(unescapeField("%2541") == "%41");      // %25 is a literal %, not the start of another escape
}

} // namespace

int main() {
    trimming();
    lowering();
    splitting();
    hexDigits();
    plainIds();
    fields();
    if (failures == 0) {
        std::printf("text_util: all checks passed\n");
        return 0;
    }
    std::printf("text_util: %d check(s) failed\n", failures);
    return 1;
}
