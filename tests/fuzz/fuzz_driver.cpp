// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// fuzz_driver.cpp — the quick fuzz run, for every test run.
//
// Mutates each target's seed corpus with a seeded generator and feeds the
// results to the target; runs every input kept in the regressions folder; and
// fails on a crash, an invariant that breaks, or an input that takes too long.
// It does not look at coverage, so it finds what blunt damage finds in a couple
// of seconds; the coverage-guided libFuzzer build (build-fuzz.bat) is what runs
// for hours. Same targets, same seeds.
//
//     fast_fuzz_driver [--target NAME] [--iterations N] [--seed S] [--max-len L]
//                      [--regressions DIR] [--write-seeds DIR] [--list]
//                      [--run FILE --target NAME]
//
// Before every input the driver writes it to `fuzz_last_input.bin` in the
// working directory, so that a crash can be reproduced with --run.

#include "fuzz/fuzz_targets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using fastfuzz::Bytes;

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed != 0 ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : static_cast<uint32_t>(next() % n); }
};

const uint32_t kInteresting32[] = { 0u, 1u, 2u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0x7FFFu, 0x8000u,
                                    0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
                                    0x00FFFFFFu, 0x40000000u };

const char* const kTokens[] = { "=", "|", "\n", "\r\n", "#", "%", "%7C", "\\", "\\n", "\t", " ",
                                "-1", "0", "99999999999", "65536", "1e308", "nan", "\"", "{", "}",
                                "[", "]", ",", ";", ":", "..", "/", "<", ">", "lstheme 1\n",
                                "name = ", "base = ", "fast.", "\xEF\xBB\xBF", "\xFF\xFE" };

// One random change to `data`, within `maxLen`.
void mutateOnce(Bytes& data, Rng& rng, const std::vector<Bytes>& corpus, size_t maxLen) {
    switch (rng.below(11)) {
        case 0:   // flip a bit
            if (!data.empty()) { data[rng.below(data.size())] ^= 1u << rng.below(8); }
            break;
        case 1:   // set a byte
            if (!data.empty()) { data[rng.below(data.size())] = static_cast<uint8_t>(rng.next()); }
            break;
        case 2:   // an interesting 32-bit value over four bytes
            if (data.size() >= 4) {
                const size_t at = rng.below(data.size() - 3);
                const uint32_t v = kInteresting32[rng.below(sizeof(kInteresting32) / 4)];
                std::memcpy(&data[at], &v, 4);
            }
            break;
        case 3: { // insert random bytes
            const size_t at = rng.below(data.size() + 1);
            const size_t n = 1 + rng.below(8);
            Bytes fill(n);
            for (uint8_t& b : fill) { b = static_cast<uint8_t>(rng.next()); }
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), fill.begin(), fill.end());
            break;
        }
        case 4:   // delete a range
            if (!data.empty()) {
                const size_t at = rng.below(data.size());
                const size_t n = 1 + rng.below(std::min<size_t>(16, data.size() - at));
                data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
                           data.begin() + static_cast<std::ptrdiff_t>(at + n));
            }
            break;
        case 5:   // duplicate a range
            if (!data.empty()) {
                const size_t at = rng.below(data.size());
                const size_t n = 1 + rng.below(std::min<size_t>(32, data.size() - at));
                const Bytes piece(data.begin() + static_cast<std::ptrdiff_t>(at),
                                  data.begin() + static_cast<std::ptrdiff_t>(at + n));
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), piece.begin(),
                            piece.end());
            }
            break;
        case 6:   // truncate
            if (!data.empty()) { data.resize(rng.below(data.size())); }
            break;
        case 7:   // swap two bytes
            if (data.size() >= 2) { std::swap(data[rng.below(data.size())], data[rng.below(data.size())]); }
            break;
        case 8: { // a token that means something to a text format
            const char* token = kTokens[rng.below(sizeof(kTokens) / sizeof(kTokens[0]))];
            const size_t at = rng.below(data.size() + 1);
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), token, token + std::strlen(token));
            break;
        }
        case 9: { // a piece of another input
            if (!corpus.empty()) {
                const Bytes& other = corpus[rng.below(corpus.size())];
                if (!other.empty()) {
                    const size_t from = rng.below(other.size());
                    const size_t n = 1 + rng.below(std::min<size_t>(64, other.size() - from));
                    const size_t at = rng.below(data.size() + 1);
                    data.insert(data.begin() + static_cast<std::ptrdiff_t>(at),
                                other.begin() + static_cast<std::ptrdiff_t>(from),
                                other.begin() + static_cast<std::ptrdiff_t>(from + n));
                }
            }
            break;
        }
        default: { // a run of one byte
            const size_t at = rng.below(data.size() + 1);
            const size_t n = 1 + rng.below(32);
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), n,
                        static_cast<uint8_t>(rng.next()));
            break;
        }
    }
    if (data.size() > maxLen) {
        data.resize(maxLen);
    }
}

void remember(const Bytes& data) {
    std::ofstream out("fuzz_last_input.bin", std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Runs one input and says how long it took; a slow input is a failure too.
bool runOne(const fastfuzz::Target& target, const Bytes& data, double slowSeconds, const char* what) {
    remember(data);
    const auto start = std::chrono::steady_clock::now();
    target.run(data.data(), data.size());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (seconds > slowSeconds) {
        std::printf("SLOW  %s took %.1f s on %s (%zu bytes); kept in fuzz_last_input.bin\n",
                    target.name, seconds, what, data.size());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string only;
    std::string regressions;
    std::string writeSeeds;
    std::string runFile;
    int iterations = 1000;
    uint64_t seed = 1;
    size_t maxLen = 8192;
    double slowSeconds = 5.0;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--target") { only = value(); }
        else if (arg == "--iterations") { iterations = std::atoi(value().c_str()); }
        else if (arg == "--seed") { seed = std::strtoull(value().c_str(), nullptr, 10); }
        else if (arg == "--max-len") { maxLen = static_cast<size_t>(std::atoll(value().c_str())); }
        else if (arg == "--slow") { slowSeconds = std::atof(value().c_str()); }
        else if (arg == "--regressions") { regressions = value(); }
        else if (arg == "--write-seeds") { writeSeeds = value(); }
        else if (arg == "--run") { runFile = value(); }
        else if (arg == "--list") { list = true; }
        else { std::printf("unknown argument %s\n", arg.c_str()); return 2; }
    }

    if (list) {
        for (const fastfuzz::Target& target : fastfuzz::targets()) {
            std::printf("%-16s %s\n", target.name, target.about);
        }
        return 0;
    }
    if (!runFile.empty()) {
        const fastfuzz::Target* target = fastfuzz::findTarget(only.c_str());
        if (target == nullptr) { std::printf("--run needs --target NAME\n"); return 2; }
        const Bytes data = readFile(runFile);
        target->run(data.data(), data.size());
        std::printf("%s: %s ran without a failure\n", target->name, runFile.c_str());
        return 0;
    }
    if (!writeSeeds.empty()) {
        int written = 0;
        for (const fastfuzz::Target& target : fastfuzz::targets()) {
            if (target.seeds == nullptr) { continue; }
            const std::filesystem::path folder = std::filesystem::path(writeSeeds) / target.name;
            std::filesystem::create_directories(folder);
            int n = 0;
            for (const Bytes& bytes : target.seeds()) {
                std::ofstream out(folder / ("seed" + std::to_string(n++) + ".bin"), std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                ++written;
            }
        }
        std::printf("wrote %d seeds to %s\n", written, writeSeeds.c_str());
        return 0;
    }

    int failures = 0;
    int ran = 0;
    for (const fastfuzz::Target& target : fastfuzz::targets()) {
        if (!only.empty() && only != target.name) { continue; }
        std::vector<Bytes> corpus = target.seeds != nullptr ? target.seeds() : std::vector<Bytes>();
        corpus.push_back(Bytes());                      // nothing at all
        corpus.push_back(Bytes { 0 });
        // What failed before must never fail again.
        if (!regressions.empty()) {
            const std::filesystem::path folder = std::filesystem::path(regressions) / target.name;
            std::error_code ignored;
            if (std::filesystem::is_directory(folder, ignored)) {
                for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                    if (entry.is_regular_file()) {
                        if (!runOne(target, readFile(entry.path()), slowSeconds,
                                    entry.path().filename().string().c_str())) {
                            ++failures;
                        }
                        ++ran;
                    }
                }
            }
        }
        Rng rng(seed * 1000003ull + std::hash<std::string>()(target.name));
        // The seeds as they are, then the mutations of them.
        for (const Bytes& start : corpus) {
            if (!runOne(target, start, slowSeconds, "a seed")) { ++failures; }
            ++ran;
        }
        for (int n = 0; n < iterations; ++n) {
            Bytes data = corpus[rng.below(static_cast<uint32_t>(corpus.size()))];
            const uint32_t changes = 1 + rng.below(4);
            for (uint32_t c = 0; c < changes; ++c) {
                mutateOnce(data, rng, corpus, maxLen);
            }
            if (!runOne(target, data, slowSeconds, "a mutation")) { ++failures; }
            ++ran;
        }
        std::printf("%-16s done\n", target.name);
        std::fflush(stdout);
    }
    std::printf("fast_fuzz_driver: %d inputs, %d failure(s)\n", ran, failures);
    return failures == 0 ? 0 : 1;
}
