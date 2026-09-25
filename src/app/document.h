// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// document.h — one open document, and the history of what has been done to it.
//
// This is the part of Fast that has nothing to do with drawing windows. A
// document owns an engine context, the document inside it, an undo history and
// the file it came from. Every tool, panel and menu item goes through here.
//
// It is a separate library from the interface on purpose: it can be tested
// without a window, and the choice of toolkit stays replaceable.

#include <livesprite/livesprite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// A document is saved as a LiveSprite package: the engine's own document plus
// whatever files an app keeps beside it.
constexpr const char* kFileExtension = ".lsprite";

// Fast's own entries inside that package are namespaced, so another app
// annotating the same file cannot collide with them.
constexpr const char* kUiStateEntry = "fast/ui-state.json";

// Everything Fast keeps in the package lives under this prefix: the view
// state, the thumbnail, the reference images. An entry outside it belongs to
// another application and is written back untouched -- which is the whole
// reason the engine owns the container and the applications own the entries.
constexpr const char* kFastEntryPrefix = "fast/";

// A picture of the first frame, kept in the package so a library or a recent
// list can show what a file holds without opening it.
constexpr const char* kThumbnailEntry = "fast/thumbnail.png";
constexpr uint32_t    kThumbnailSize = 128;

// How large a canvas Fast is willing to work with.
//
// This is Fast's decision, not the engine's. The engine refuses only what it
// cannot safely represent, which is far larger; everything below that is a
// judgement about cost, and the cost is steep -- a compile is area times layers,
// so 4096x4096 with eight layers takes seconds. Pract, which is willing to wait,
// sets its own.
//
// 16384 on a side leaves room for a long sprite sheet, which is cheap. The area
// bound is what keeps a raster to 64 MB whatever the aspect ratio.
constexpr uint32_t kMaxCanvasDimension = 16384;
constexpr uint64_t kMaxCanvasPixels    = 4096ull * 4096ull;

// The profile every compile in Fast uses: the canvas, the strip, the
// thumbnails, the bucket's view of the picture, and every file written.
//
// One place, because they must agree -- a bucket that floods a picture the
// canvas does not show, or an export that differs from the screen in anything
// but quality, is a bug nobody can see. Alpha is preserved: the engine's
// default thresholds it, which is right for a sprite drawn in hard pixels and
// wrong for an editor, where a layer at 40% over nothing has to look like 40%
// and a half-transparent colour in an opened PNG has to stay one.
ls::CompileProfile compileProfile(ls::CompileProfileType type, uint32_t width,
                                  uint32_t height);

class Document {
public:
    Document();
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Trades everything -- engine, history, path, package -- with another
    // document. How a tab is put away and brought back.
    void swap(Document& other) noexcept;

    // ---------------------------------------------------------------- life --

    // A new, empty document. Returns false only if the engine refuses the size.
    bool create(const std::string& name, uint32_t width, uint32_t height);

    // Opens a .lsprite package. On failure, `error` says why in words a person
    // can act on, and this document is left untouched.
    bool open(const std::string& path, std::string* error);

    // Writes a .lsprite package. Entries this build does not understand are
    // written back out unchanged -- see foreignEntryCount().
    bool save(const std::string& path, std::string* error);

    bool saveInPlace(std::string* error);

    // Writes the same package somewhere else without the document taking any
    // notice: its path does not change and its unsaved changes stay unsaved.
    // What autosave needs -- a copy that is not "the file" -- and what a
    // "save a copy" command would want too.
    bool saveCopy(const std::string& path, std::string* error) const;

    // ------------------------------------------------------------- access --

    ls::LSContext&   engine()      { return *engine_; }
    ls::DocumentId   id()    const { return id_; }

    // The document's first sprite -- which, once frames exist, is its first
    // frame. A document always has one: create() makes it, and the timeline
    // refuses to delete the last one. Null only if the document failed to open.
    ls::SpriteId     sprite() const;
    const std::string& name() const { return name_; }
    const std::string& path() const { return path_; }
    bool             modified() const { return modified_; }

    // Declares the current state to be the baseline, without writing anything.
    //
    // Building a new document takes real actions -- creating its first layer is
    // one -- so it arrives already marked as changed, and File > New would ask
    // whether to save a document that has never been touched. This says: this
    // is what empty looks like.
    void markUnmodified() { modified_ = false; }

    // The opposite, for a document that arrived already needing a home: a
    // recovered copy is opened from the recovery folder, and must not look
    // like a saved file sitting there -- the person still has to say where
    // it goes.
    void markModified() { modified_ = true; }

    // Forgets where this document came from, so Save has to ask. Used when a
    // document was opened from somewhere it must not be written back to.
    void forgetPath() { path_.clear(); }

    // Forgets the history, so the current state is where undo stops. For the
    // same reason as markUnmodified: setting a document up takes real actions,
    // and the first layer arriving as an undo entry means Ctrl+Z in a new
    // document removes the only thing there was to draw on.
    void clearHistory() { undoStack_.clear(); redoStack_.clear(); }

    // ---------------------------------------------------------------- undo --
    //
    // Every change to the document is bracketed:
    //
    //     doc.beginAction("Draw");
    //     ... engine calls ...
    //     doc.endAction();
    //
    // beginAction captures the state before the change; endAction commits it to
    // the history. A tool that is cancelled mid-drag calls abandonAction
    // instead, which puts the document back and leaves no history entry.
    //
    // Nesting is counted, so a compound tool that calls into helpers which also
    // bracket their work produces one history entry rather than several.

    void beginAction(const std::string& label);
    void endAction();
    void abandonAction();

    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // What undo/redo would do next, for a menu label. Empty when there is
    // nothing to do.
    std::string undoLabel() const;
    std::string redoLabel() const;

    // Every step, for a history panel: what undo would take back, oldest
    // first, and what redo would bring back, nearest first.
    std::vector<std::string> undoLabels() const;
    std::vector<std::string> redoLabels() const;

    // How many entries the history may hold. Snapshots are in-memory state
    // copies rather than serialized bytes, so this can be generous.
    void setHistoryLimit(size_t limit);

    // ---------------------------------------------------------- companions --

    // Entries carried in the opened package that Fast does not understand --
    // Pract's data, another tool's data. Fast writes them back untouched, so
    // saving here does not destroy someone else's work. This count is how a
    // test proves that.
    size_t foreignEntryCount() const { return foreignEntries_.size(); }

    // Fast's own entries in the package, other than the view state: the
    // thumbnail and the reference images. They are not engine state, so the
    // engine's snapshot will not carry them -- the history entries here do,
    // which is what makes an imported reference undo like anything else.
    //
    // setCompanion refuses a name outside kFastEntryPrefix rather than
    // letting Fast overwrite another application's entry.
    bool setCompanion(const std::string& name, const std::string& contentType,
                      std::vector<uint8_t> data);
    const std::vector<uint8_t>* companion(const std::string& name) const;
    bool clearCompanion(const std::string& name);
    std::vector<std::string> companionNames() const;

    // Fast's own UI state, stored in the package under kUiStateEntry. Opaque to
    // the engine; Fast decides what is in it.
    void setUiState(const std::string& json) { uiState_ = json; }
    const std::string& uiState() const { return uiState_; }

private:
    struct HistoryEntry {
        ls::DocumentSnapshot snapshot;
        std::string          label;
        // Fast's own package entries, which the engine's snapshot knows
        // nothing about. Without these, undoing an import would restore the
        // artwork and leave the reference image behind -- and the list naming
        // it would come back while the picture stayed, or the reverse.
        std::vector<ls::PackageEntry> companions;
    };

    void releasePrevious(ls::DocumentId replacement);
    void pushHistory(HistoryEntry entry);
    void trimHistory();

    std::unique_ptr<ls::LSContext> engine_;
    ls::DocumentId                 id_;
    std::string                    name_;
    std::string                    path_;
    bool                           modified_ = false;

    // The action currently being recorded, if any.
    int                  actionDepth_ = 0;
    ls::DocumentSnapshot pending_;
    std::vector<ls::PackageEntry> pendingCompanions_;
    std::string          pendingLabel_;

    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    size_t                    historyLimit_ = 200;

    std::vector<ls::PackageEntry> foreignEntries_;
    std::vector<ls::PackageEntry> companions_;
    std::string                   uiState_;
};

} // namespace fast
