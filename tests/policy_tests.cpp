// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// policy_tests.cpp — what the privacy policy and the Store listing promise, held
// by a test rather than by memory.
//
// PRIVACY.md says the program contains no network code and sends nothing; the
// listing says it needs no account. A promise like that is only as good as the
// next commit, so this scans the source for what would break it: a network API,
// a networking library, a download. The one outbound action allowed is asking the
// operating system to open a link the person clicked (SDL_OpenURL), and only in
// the two places that draw links.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#ifndef SRC_DIR
#error "SRC_DIR must name the project's src folder"
#endif

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

struct Source {
    std::string name;       // the file's name
    std::string path;
    std::string text;
};

std::vector<Source> sources() {
    std::vector<Source> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(SRC_DIR)) {
        const std::string ext = entry.path().extension().string();
        if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".mm")) {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        Source source;
        source.name = entry.path().filename().string();
        source.path = entry.path().string();
        source.text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        out.push_back(source);
    }
    return out;
}

void testNoNetworkCode() {
    // What a program uses to talk to the network, on Windows, POSIX and in the
    // libraries people reach for.
    const std::vector<std::string> forbidden = {
        "WinHttp", "InternetOpen", "InternetConnect", "URLDownloadToFile", "URLOpenStream",
        "WSAStartup", "getaddrinfo", "gethostbyname", "inet_pton", "<winsock", "<sys/socket.h>",
        "<netdb.h>", "<arpa/inet.h>", "curl_easy", "<curl/", "SDL_net", "SDLNet_", "asio::",
        "boost/asio", "cpr/cpr.h", "httplib", "libsoup", "NSURLSession", "BITS", "IBackgroundCopy",
    };
    for (const Source& source : sources()) {
        for (const std::string& word : forbidden) {
            // "BITS" is Windows' background transfer service; as a word of its own.
            if (word == "BITS") {
                continue;
            }
            if (source.text.find(word) != std::string::npos) {
                std::printf("  %s mentions %s\n", source.name.c_str(), word.c_str());
                CHECK(false);
            }
        }
    }
}

void testLinksOpenOnlyWhereLinksAreDrawn() {
    const std::set<std::string> allowed = { "help.cpp", "user_guide.cpp" };
    for (const Source& source : sources()) {
        if (source.text.find("SDL_OpenURL") != std::string::npos && !allowed.count(source.name)) {
            std::printf("  %s opens a URL\n", source.name.c_str());
            CHECK(false);
        }
    }
    // And a link that is followed is https only (the guide checks; so does About).
    for (const Source& source : sources()) {
        if (source.name == "user_guide.cpp") {
            CHECK(source.text.find("https://") != std::string::npos);
        }
    }
}

void testNothingRunsOtherPrograms() {
    // Launching another program is how a downloader or an updater would start.
    const std::vector<std::string> forbidden = { "ShellExecute", "CreateProcess", "WinExec",
                                                 "std::system(", " system(", "popen(", "_popen(" };
    for (const Source& source : sources()) {
        for (const std::string& word : forbidden) {
            if (source.text.find(word) != std::string::npos) {
                std::printf("  %s mentions %s\n", source.name.c_str(), word.c_str());
                CHECK(false);
            }
        }
    }
}

} // namespace

int main() {
    testNoNetworkCode();
    testLinksOpenOnlyWhereLinksAreDrawn();
    testNothingRunsOtherPrograms();
    if (failures == 0) {
        std::printf("fast_policy: all checks passed\n");
        return 0;
    }
    std::printf("fast_policy: %d check(s) failed\n", failures);
    return 1;
}
