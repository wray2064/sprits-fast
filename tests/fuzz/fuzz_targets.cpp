// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "fuzz/fuzz_targets.h"

#include "app/animation.h"
#include "app/batch.h"
#include "app/document.h"
#include "app/element.h"
#include "app/guide.h"
#include "app/guides.h"
#include "app/i18n.h"
#include "app/image_io.h"
#include "app/webp.h"
#include "app/import_aseprite.h"
#include "app/keymap.h"
#include "app/layers.h"
#include "app/palette_io.h"
#include "app/preferences.h"
#include "app/puppet.h"
#include "app/recent_colours.h"
#include "app/reference.h"
#include "app/slices.h"
#include "app/text_util.h"
#include "app/theme_tokens.h"
#include "app/tilemap.h"
#include "app/tracks.h"
#include "app/transform.h"
#include "app/ui_state.h"
#include "app/vector_icon.h"
#include "ase_builder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define FUZZ_PID _getpid()
#else
#include <unistd.h>
#define FUZZ_PID getpid()
#endif

namespace fastfuzz {

namespace {

using namespace fast;

// An invariant that must hold of whatever was read. A fuzzer finds a failure of
// this the way it finds a crash.
#define INVARIANT(...)                                                              \
    do {                                                                            \
        if (!(__VA_ARGS__)) {                                                       \
            std::fprintf(stderr, "INVARIANT BROKEN %s:%d  %s\n", __FILE__, __LINE__, \
                         #__VA_ARGS__);                                             \
            std::abort();                                                           \
        }                                                                           \
    } while (0)

std::string asText(const uint8_t* data, size_t size) {
    return std::string(reinterpret_cast<const char*>(data), size);
}

Bytes asBytes(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

Bytes bytesOf(const std::string& text) { return asBytes(text); }

// --- text readers ------------------------------------------------------------------

void themeFile(const uint8_t* data, size_t size) {
    Theme theme;
    ThemeReport report;
    if (!parseTheme(asText(data, size), &theme, &report)) {
        return;
    }
    // What was read can be checked, repaired and written, and the writing reads
    // back as the same colours.
    const std::vector<ContrastIssue> issues = checkTheme(theme);
    (void)issues;
    Theme repaired = theme;
    fixContrast(&repaired);
    const std::string again = serializeTheme(theme);
    Theme second;
    INVARIANT(parseTheme(again, &second, nullptr));
    INVARIANT(sameColours(theme, second));
    INVARIANT(theme.id == themeIdFromName(theme.name) || theme.id == "custom");
}

std::vector<Bytes> themeSeeds() {
    std::vector<Bytes> seeds;
    for (const Theme& theme : builtinThemes()) {
        seeds.push_back(bytesOf(serializeTheme(theme)));
    }
    seeds.push_back(bytesOf("lstheme 1\nname = Mine\nbase = midnight\naccent = #28c8b4\n"
                            "pract.track = #ff0000\n"));
    return seeds;
}

void preferencesFile(const uint8_t* data, size_t size) {
    const Preferences first = loadPreferences(asText(data, size));
    const std::string saved = savePreferences(first);
    const Preferences second = loadPreferences(saved);
    // Written, read and written again is a fixed point: nothing drifts.
    INVARIANT(savePreferences(second) == saved);
}

std::vector<Bytes> preferencesSeeds() {
    return { bytesOf(savePreferences(Preferences{})) };
}

void keymapFile(const uint8_t* data, size_t size) {
    Keymap first;
    first.load(asText(data, size));
    const std::string saved = first.save();
    Keymap second;
    second.load(saved);
    INVARIANT(second.save() == saved);
    Chord chord;
    if (parseChord(asText(data, size), &chord)) {
        Chord again;
        INVARIANT(parseChord(chordText(chord), &again));
        INVARIANT(again == chord);
    }
}

std::vector<Bytes> keymapSeeds() {
    Keymap map;
    Chord chord;
    chord.key = "N";
    chord.shift = true;
    map.setChords("tool.pencil", { chord });
    return { bytesOf(map.save()), bytesOf("Ctrl+Shift+Z"), bytesOf("tool.pencil = N | Shift+N\n") };
}

void catalogueFile(const uint8_t* data, size_t size) {
    clearCatalogue();
    loadCatalogue(asText(data, size));
    clearCatalogue();
}

std::vector<Bytes> catalogueSeeds() {
    return { bytesOf("# a comment\nHello = Hola\nA\\=B = C\nTwo\\nlines = Dos\\nlineas\n"),
             bytesOf("Open = Abrir\nSave = Guardar\n") };
}

void paletteText(const uint8_t* data, size_t size) {
    PaletteFile file;
    std::string error;
    if (parsePalette(asText(data, size), &file, &error)) {
        // Whatever was read can be written in every form.
        (void)toGpl(file.name, file.entries);
        (void)toHex(file.entries);
        (void)toJasc(file.entries);
        (void)toAct(file.entries);
    }
}

std::vector<Bytes> paletteTextSeeds() {
    return { bytesOf("GIMP Palette\nName: Sample\nColumns: 4\n#\n255 0 0 red\n0 255 0 green\n"
                     "  0   0 255\n"),
             bytesOf("JASC-PAL\n0100\n3\n255 0 0\n0 255 0\n0 0 255\n"),
             bytesOf("ff0000\n00ff00\n0000ff\n"),
             bytesOf("#ff0000\n#00ff0080\n") };
}

void paletteAct(const uint8_t* data, size_t size) {
    PaletteFile file;
    std::string error;
    (void)parseAct(Bytes(data, data + size), &file, &error);
    PaletteFile image;
    (void)paletteFromImage(Bytes(data, data + size), &image, &error);
}

std::vector<Bytes> paletteActSeeds() {
    Bytes act(768 + 4, 0);
    for (size_t i = 0; i < 16; ++i) { act[i * 3] = static_cast<uint8_t>(i * 16); }
    act[768] = 0; act[769] = 16; act[770] = 0xFF; act[771] = 0xFF;
    return { act };
}

void cyclesText(const uint8_t* data, size_t size) {
    std::vector<Cycle> first;
    if (!decodeCycles(asText(data, size), &first)) {
        return;
    }
    const std::string text = encodeCycles(first);
    std::vector<Cycle> second;
    INVARIANT(decodeCycles(text, &second));
    // One more pass settles it: what is read is already what would be written.
    INVARIANT(encodeCycles(second) == text);
}

std::vector<Bytes> cyclesSeeds() {
    Cycle walk;
    walk.name = "walk | one";
    walk.frames = { 0, 1, 2, 1 };
    walk.loop = LoopMode::PingPong;
    Cycle idle;
    idle.name = "idle";
    idle.frames = { 0 };
    return { bytesOf(encodeCycles({ walk, idle })) };
}

void guidesText(const uint8_t* data, size_t size) {
    const std::vector<Guide> first = decodeGuides(asText(data, size));
    const std::string text = encodeGuides(first);
    INVARIANT(encodeGuides(decodeGuides(text)) == text);
}

std::vector<Bytes> guidesSeeds() {
    Guide a; a.vertical = true; a.at = 4;
    Guide b; b.vertical = false; b.at = -7;
    return { bytesOf(encodeGuides({ a, b })) };
}

void slicesText(const uint8_t* data, size_t size) {
    const std::vector<Slice> first = decodeSlices(asText(data, size));
    const std::string text = encodeSlices(first);
    INVARIANT(encodeSlices(decodeSlices(text)) == text);
}

std::vector<Bytes> slicesSeeds() {
    Slice slice;
    slice.name = "button";
    slice.bounds = ls::Rect2i{ { 1, 2 }, { 9, 12 } };
    slice.nine = true;
    slice.centre = ls::Rect2i{ { 2, 2 }, { 6, 6 } };
    slice.hasPivot = true;
    slice.pivot = ls::Vec2i{ 3, 4 };
    return { bytesOf(encodeSlices({ slice })) };
}

void referencesText(const uint8_t* data, size_t size) {
    std::vector<Reference> first;
    if (!decodeReferences(asText(data, size), &first)) {
        return;
    }
    const std::string text = encodeReferences(first);
    std::vector<Reference> second;
    INVARIANT(decodeReferences(text, &second));
    INVARIANT(encodeReferences(second) == text);
}

std::vector<Bytes> referencesSeeds() {
    Reference ref;
    ref.id = "1";
    ref.name = "pose | one";
    ref.width = 64;
    ref.height = 48;
    ref.x = 3.5f;
    ref.opacity = 0.25f;
    ref.clipX = 8; ref.clipY = 8; ref.clipWidth = 16; ref.clipHeight = 16;
    return { bytesOf(encodeReferences({ ref })) };
}

void recentColoursText(const uint8_t* data, size_t size) {
    const std::vector<ls::Color> first = decodeRecentColours(asText(data, size));
    const std::string text = encodeRecentColours(first);
    INVARIANT(encodeRecentColours(decodeRecentColours(text)) == text);
}

std::vector<Bytes> recentColoursSeeds() {
    return { bytesOf(encodeRecentColours({ ls::Color{ 1, 2, 3, 255 }, ls::Color{ 250, 10, 20, 255 } })) };
}

void uiStateJson(const uint8_t* data, size_t size) {
    UiState state;
    if (!fromJson(asText(data, size), &state)) {
        return;
    }
    state.clamp(3, 2, 1);
    const std::string text = toJson(state);
    UiState second;
    INVARIANT(fromJson(text, &second));
    second.clamp(3, 2, 1);
    INVARIANT(toJson(second) == text);
}

std::vector<Bytes> uiStateSeeds() {
    return { bytesOf(toJson(UiState{})) };
}

void vectorIconSvg(const uint8_t* data, size_t size) {
    VectorIcon icon;
    std::string error;
    (void)parseVectorIcon(asText(data, size), &icon, &error);
}

std::vector<Bytes> vectorIconSeeds() {
    return { bytesOf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
                     "viewBox=\"0 0 24 24\"><path d=\"M3 3l18 18M3 21L21 3\"/>"
                     "<circle cx=\"12\" cy=\"12\" r=\"5\"/><rect x=\"3\" y=\"3\" width=\"18\" "
                     "height=\"18\" rx=\"2\"/><polyline points=\"4 4 20 4 20 20\"/>"
                     "<path d=\"M4 12a8 8 0 1 0 16 0a8 8 0 1 0-16 0\"/></svg>") };
}

void batchArguments(const uint8_t* data, size_t size) {
    // One argument a line, as a command line would give them.
    std::vector<std::string> args;
    std::string current;
    for (size_t i = 0; i < size && args.size() < 64; ++i) {
        if (data[i] == '\n') {
            args.push_back(current);
            current.clear();
        } else {
            current.push_back(static_cast<char>(data[i]));
        }
    }
    args.push_back(current);
    BatchJob job;
    std::string error;
    (void)parseBatch(args, &job, &error);
}

std::vector<Bytes> batchSeeds() {
    return { bytesOf("--export\nhero.lsprite\n--sheet\n--out\nhero.png\n--scale\n2\n--trim\n"),
             bytesOf("--export\nhero.lsprite\n--cycle\nwalk\n--animated\n--out\nwalk.gif\n") };
}

// The user guide's Markdown, and a search through it by whatever was typed.
void guideMarkdown(const uint8_t* data, size_t size) {
    const std::string text = asText(data, size);
    GuideSource source;
    source.id = "fuzz";
    source.text = text;
    GuideSource other;
    other.id = "other";
    other.text = "# Other\n\nSome words about a palette and a [link](topic:fuzz).\n";
    const std::vector<GuideTopic> topics = parseGuide({ source, other });
    INVARIANT(topics.size() == 2);
    (void)firstBrokenLink(topics);
    const std::vector<GuideHit> hits = searchGuide(topics, text.substr(0, 200));
    for (const GuideHit& hit : hits) {
        INVARIANT(hit.topic < topics.size());
    }
    (void)searchGuide(topics, "palette");
}

std::vector<Bytes> guideSeeds() {
    return { bytesOf("# Title\n\n## Section\n\nA **bold** and `Ctrl+Z` and [link](topic:other).\n\n"
                     "- one\n  - nested\n1. first\n\n> note\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
                     "```\ncode\n```\n\n---\n") };
}

// The small helpers every parser leans on, against whatever they are given.
void textHelpers(const uint8_t* data, size_t size) {
    const std::string text = asText(data, size);
    INVARIANT(unescapeField(escapeField(text)) == text);
    const std::string once = trim(text);
    INVARIANT(trim(once) == once);
    INVARIANT(lower(lower(text)) == lower(text));
    const std::vector<std::string> parts = split(text, '|');
    INVARIANT(!parts.empty());
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        joined += (i == 0 ? "" : "|") + parts[i];
    }
    INVARIANT(joined == text);
    (void)isPlainId(text);
    (void)themeIdFromName(text);
    Rgba colour;
    if (parseColour(text, &colour)) {
        Rgba again;
        INVARIANT(parseColour(colourText(colour), &again));
        INVARIANT(again == colour);
    }
}

std::vector<Bytes> textHelperSeeds() {
    return { bytesOf("name | with %pipe and\nnewline"), bytesOf("#ff9e26"), bytesOf("  spaced  ") };
}

// --- binary readers ----------------------------------------------------------------

void asepriteFile(const uint8_t* data, size_t size) {
    AseFile file;
    std::string error;
    (void)parseAseprite(Bytes(data, data + size), &file, &error);
}

std::vector<Bytes> asepriteSeeds() {
    return { asebuild::indexedSprite(), asebuild::rgbaSprite() };
}

// The whole import: parsed, and then built into a document.
void asepriteImport(const uint8_t* data, size_t size) {
    AseFile file;
    std::string error;
    if (!parseAseprite(Bytes(data, data + size), &file, &error)) {
        return;
    }
    // A file may claim any canvas; the document refuses what it should, but the
    // fuzzer is not here to wait for a sixteen-thousand-pixel one to be drawn.
    if (static_cast<uint64_t>(file.width) * file.height > 1u << 20) {
        return;
    }
    Document doc;
    AsepriteReport report;
    (void)documentFromAseprite(doc, file, "fuzz", &report, &error);
}

void imageFile(const uint8_t* data, size_t size) {
    const Bytes bytes(data, data + size);
    ImageInfo info;
    std::string error;
    (void)imageInfo(bytes, &info, &error);
    ls::RasterBuffer raster;
    if (decodeImage(bytes, &raster, &error)) {
        INVARIANT(raster.pixels.size() >= static_cast<size_t>(raster.stride) * raster.height);
    }
    std::vector<ls::RasterBuffer> frames;
    std::vector<int> holds;
    if (decodeFrames(bytes, &frames, &holds, &error)) {
        INVARIANT(frames.size() == holds.size() || holds.empty());
    }
    (void)countGifFrames(bytes);
}

std::vector<Bytes> webpSeeds();

std::vector<Bytes> imageSeeds() {
    std::vector<Bytes> seeds;
    ls::RasterBuffer raster = ls::makeRaster(5, 4);
    for (size_t i = 0; i < raster.pixels.size(); ++i) {
        raster.pixels[i] = static_cast<uint8_t>(i * 7 + (i % 4 == 3 ? 255 : 0));
    }
    Bytes png;
    std::string error;
    if (encodeImageAsPng(raster, &png, &error)) {
        seeds.push_back(png);
    }
    for (Bytes& webp : webpSeeds()) {
        seeds.push_back(std::move(webp));
    }
    return seeds;
}

// Lossless WebP through its own reader: container, prefix codes, the four
// transforms, copies. What comes back must be as large as the header said.
void webpFile(const uint8_t* data, size_t size) {
    const Bytes bytes(data, data + size);
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error;
    const bool sized = webpSize(bytes, &width, &height, &error);
    ls::RasterBuffer raster;
    if (decodeWebp(bytes, &raster, &error)) {
        INVARIANT(sized && raster.width == width && raster.height == height);
        INVARIANT(raster.pixels.size() >= static_cast<size_t>(raster.stride) * raster.height);
    }
}

std::vector<Bytes> webpSeeds() {
    std::vector<Bytes> seeds;
    std::string error;
    // Indexed (a few colours), and predicted (a gradient, too many to index).
    for (int style = 0; style < 3; ++style) {
        const uint32_t side = style == 0 ? 9u : 40u;
        ls::RasterBuffer raster = ls::makeRaster(side, side);
        for (uint32_t y = 0; y < side; ++y) {
            for (uint32_t x = 0; x < side; ++x) {
                const uint8_t v = style == 0 ? static_cast<uint8_t>(((x + y) % 3) * 90)
                                  : style == 1 ? static_cast<uint8_t>(x * 5 + y)
                                               : static_cast<uint8_t>((x * y) ^ (x << 2));
                ls::writePixel(raster, static_cast<int32_t>(x), static_cast<int32_t>(y),
                               ls::Color{ v, static_cast<uint8_t>(y * 6), static_cast<uint8_t>(255 - v),
                                          static_cast<uint8_t>(style == 0 && x == 0 ? 0 : 255) });
            }
        }
        Bytes webp;
        if (encodeWebp(raster, &webp, &error)) {
            seeds.push_back(webp);
        }
    }
    return seeds;
}

// --- documents ---------------------------------------------------------------------

// Everything the program reads out of a document, the way it does when one is
// opened and drawn, so that what a file says about itself is used.
void readEverything(Document& doc) {
    const std::vector<Frame> frames = readFrames(doc);
    (void)readCycles(doc, static_cast<int>(frames.size()));
    (void)readGuides(doc);
    (void)readSlices(doc);
    (void)readReferences(doc);
    (void)partsOf(doc);
    (void)tilesetsOf(doc);
    (void)tracksOn(doc);
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return;
    }
    for (ls::SpriteId sprite : info.value.sprites) {
        CapturedPose pose;
        (void)readCapturedPose(doc, sprite, &pose);
        (void)isPart(doc, sprite);
        (void)isTileset(doc, sprite);
        for (ls::LayerId layer : layerOrder(doc, sprite)) {
            LayerProps props;
            (void)readLayerProps(doc, layer, &props);
            (void)elementsOf(doc, layer);
            TilemapLayer tilemap;
            (void)readTilemapLayer(doc, layer, &tilemap);
            FreeScale scale;
            (void)readFreeScale(doc, layer, &scale);
            (void)trackKey(doc, layer);
            (void)linkOf(doc, layer);
        }
    }
}

std::filesystem::path scratchFile() {
    static const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("fast_fuzz_" + std::to_string(FUZZ_PID) + ".lsprite");
    return path;
}

// A package opened from bytes: the container, the engine's own reader, and then
// everything Fast reads out of what it holds.
void packageFile(const uint8_t* data, size_t size) {
    const std::filesystem::path path = scratchFile();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    // One document for every input: making an engine costs more than reading a
    // small package, and open() replaces everything the document held.
    static Document doc;
    std::string error;
    if (doc.open(path.string(), &error)) {
        readEverything(doc);
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

std::vector<Bytes> packageSeeds() {
    std::vector<Bytes> seeds;
    Document doc;
    if (!doc.create("seed", 8, 8)) {
        return seeds;
    }
    (void)duplicateFrame(doc, 0);
    const std::filesystem::path path = scratchFile();
    std::string error;
    if (doc.save(path.string(), &error)) {
        std::ifstream in(path, std::ios::binary);
        seeds.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return seeds;
}

// The metadata Fast keeps on a document's entities, set to whatever the input
// says and then read the way the program reads it. A package carries these as
// text, so a crafted one can put anything in them. The keys are those the
// readers use (see animation.h, layers.h, puppet.h and the .cpp files that
// own the rest); a key renamed there must be renamed here.
void metadataText(const uint8_t* data, size_t size) {
    static const char* const kKeys[] = {
        kCyclesKey, kFrameDurationKey, kFrameNameKey, kLayerLockedKey, kLayerTagKey,
        kLayerNotesKey, kPartKey, kPoseKey, "fast.guides", "fast.slices", "fast.tracks",
        "fast.track", "fast.track.from", "fast.link", "fast.tileset", "fast.text",
        "fast.text.at", "fast.text.scale",
    };
    constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

    Document doc;
    if (!doc.create("fuzz", 8, 8)) {
        return;
    }
    (void)duplicateFrame(doc, 0);
    std::vector<uint64_t> entities { doc.id().value };
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.ok()) {
        for (ls::SpriteId sprite : info.value.sprites) {
            entities.push_back(sprite.value);
            for (ls::LayerId layer : layerOrder(doc, sprite)) {
                entities.push_back(layer.value);
            }
        }
    }
    // Records of: entity, key, length, then that many bytes of value.
    size_t at = 0;
    while (at + 3 <= size) {
        const uint64_t entity = entities[data[at] % entities.size()];
        const char* key = kKeys[data[at + 1] % kKeyCount];
        const size_t length = std::min<size_t>(data[at + 2], size - at - 3);
        (void)doc.engine().setMetadata(
            entity, key, std::string_view(reinterpret_cast<const char*>(data + at + 3), length));
        at += 3 + length;
    }
    readEverything(doc);
}

std::vector<Bytes> metadataSeeds() {
    Bytes seed;
    const auto put = [&](uint8_t entity, uint8_t key, const std::string& value) {
        seed.push_back(entity);
        seed.push_back(key);
        seed.push_back(static_cast<uint8_t>(value.size()));
        seed.insert(seed.end(), value.begin(), value.end());
    };
    put(0, 0, "walk|0,1|loop\nidle|0|loop");           // cycles
    put(1, 1, "120");                                  // a frame's hold
    put(1, 2, "first");                                // its name
    put(0, 8, "v,4\nh,2");                             // guides
    put(2, 7, "pose-text");                            // a captured pose
    put(0, 9, "btn\t1\t2\t9\t12");                     // slices
    return { seed };
}

} // namespace

const std::vector<Target>& targets() {
    static const std::vector<Target> all = {
        { "theme", "a .lstheme file: parse, check, repair, write, read back", themeFile, themeSeeds },
        { "preferences", "the settings file: read, write, read: a fixed point", preferencesFile,
          preferencesSeeds },
        { "keymap", "the key bindings file and chords", keymapFile, keymapSeeds },
        { "catalogue", "a translation catalogue", catalogueFile, catalogueSeeds },
        { "palette-text", "GPL, JASC and hex palettes", paletteText, paletteTextSeeds },
        { "palette-act", "Photoshop .act palettes, and palettes taken from an image",
          paletteAct, paletteActSeeds },
        { "cycles", "the animation cycles stored in a document", cyclesText, cyclesSeeds },
        { "guides", "the guides stored in a document", guidesText, guidesSeeds },
        { "slices", "the slices stored in a document", slicesText, slicesSeeds },
        { "references", "the references list stored in a document", referencesText,
          referencesSeeds },
        { "recent-colours", "the recent colours setting", recentColoursText,
          recentColoursSeeds },
        { "ui-state", "the view state stored beside a document", uiStateJson, uiStateSeeds },
        { "vector-icon", "an SVG icon", vectorIconSvg, vectorIconSeeds },
        { "batch", "the command line of a batch export", batchArguments, batchSeeds },
        { "guide", "the user guide's Markdown and its search", guideMarkdown, guideSeeds },
        { "text-helpers", "trim, split, escape and the other helpers every parser uses",
          textHelpers, textHelperSeeds },
        { "aseprite", "an Aseprite file: parse", asepriteFile, asepriteSeeds },
        { "aseprite-import", "an Aseprite file: parse and build a document",
          asepriteImport, asepriteSeeds },
        { "image", "PNG, JPEG, BMP, GIF and WebP: header, decode, frames", imageFile, imageSeeds },
        { "webp", "lossless WebP: container, prefix codes, transforms, copies", webpFile,
          webpSeeds },
        { "package", "a .lsprite package: open, then read everything Fast keeps in it",
          packageFile, packageSeeds },
        { "metadata", "the metadata Fast keeps on a document, as hostile text", metadataText,
          metadataSeeds },
    };
    return all;
}

const Target* findTarget(const char* name) {
    for (const Target& target : targets()) {
        if (std::string(target.name) == name) {
            return &target;
        }
    }
    return nullptr;
}

} // namespace fastfuzz
