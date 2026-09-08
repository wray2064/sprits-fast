// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/document.h"
#include "app/file_io.h"

namespace fast {
namespace {

std::string engineError(ls::LSError error) {
    return std::string(ls::lsErrorString(error));
}

} // namespace

Document::Document() : engine_(ls::LSContext::create()) {}
Document::~Document() = default;

// Lets go of the document this one is replacing.
//
// An editor that opens ten files should be holding one document, not ten. The
// context owns every entity handed out through it, so a document that is no
// longer on screen is not merely idle: it keeps its sprites, layers, regions and
// operations alive, and keeps them in the dependency graph, for as long as the
// window is open.
//
// Snapshots are dropped first. They hold engine state for the document that is
// going away, and an undo history for something no longer on screen is not
// something any interface can offer.
void Document::releasePrevious(ls::DocumentId replacement) {
    undoStack_.clear();
    redoStack_.clear();
    pending_ = ls::DocumentSnapshot{};
    pendingLabel_.clear();
    actionDepth_ = 0;

    if (id_.valid() && id_ != replacement) {
        engine_->deleteDocument(id_);
    }
}

// ------------------------------------------------------------------- life --

bool Document::create(const std::string& name, uint32_t width, uint32_t height) {
    ls::DocumentDesc desc;
    desc.name = name;
    desc.canvasWidth = width;
    desc.canvasHeight = height;

    auto created = engine_->createDocument(desc);
    if (created.fail()) {
        return false;
    }
    // The new document is built before the old one is let go, so a failure
    // leaves the editor holding what it had.
    releasePrevious(created.value);
    id_ = created.value;
    name_ = name;
    path_.clear();
    modified_ = false;
    undoStack_.clear();
    redoStack_.clear();
    foreignEntries_.clear();
    uiState_.clear();
    return true;
}

bool Document::open(const std::string& path, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!fast::readFile(path, bytes, error)) {
        return false;
    }

    // Everything in a file that arrived from elsewhere is untrusted input. The
    // engine bounds-checks the container and verifies every entry; what it hands
    // back is still only a claim about what those entries contain.
    ls::SerializedData package;
    package.bytes = std::move(bytes);

    std::vector<ls::PackageEntry> entries;
    auto loaded = engine_->loadPackage(package, &entries);
    if (loaded.fail()) {
        if (error) { *error = "could not open " + path + ": " + engineError(loaded.error); }
        return false;
    }

    releasePrevious(loaded.value);
    id_ = loaded.value;
    path_ = path;
    modified_ = false;
    undoStack_.clear();
    redoStack_.clear();
    uiState_.clear();
    foreignEntries_.clear();

    // Keep everything that is not ours, so saving does not destroy another
    // application's data. This is the whole reason the engine owns the container
    // and the apps own the entries.
    for (ls::PackageEntry& entry : entries) {
        if (entry.name == kUiStateEntry) {
            uiState_.assign(entry.data.begin(), entry.data.end());
        } else {
            foreignEntries_.push_back(std::move(entry));
        }
    }

    name_ = path;
    return true;
}

bool Document::save(const std::string& path, std::string* error) {
    std::vector<ls::PackageEntry> entries = foreignEntries_;

    if (!uiState_.empty()) {
        ls::PackageEntry ui;
        ui.name = kUiStateEntry;
        ui.contentType = "application/json";
        ui.data.assign(uiState_.begin(), uiState_.end());
        entries.push_back(std::move(ui));
    }

    auto written = engine_->writePackage(id_, entries);
    if (written.fail()) {
        if (error) { *error = "could not build the package: " + engineError(written.error); }
        return false;
    }

    if (!writeFileAtomic(path, written.value.bytes, error)) {
        return false;
    }

    path_ = path;
    modified_ = false;
    return true;
}

bool Document::saveInPlace(std::string* error) {
    if (path_.empty()) {
        if (error) { *error = "this document has never been saved"; }
        return false;
    }
    return save(path_, error);
}

// ------------------------------------------------------------------- undo --

void Document::beginAction(const std::string& label) {
    // Nesting is counted so a compound tool produces one history entry.
    if (actionDepth_++ > 0) {
        return;
    }
    auto captured = engine_->snapshotDocumentState(id_);
    if (captured.fail()) {
        // Nothing to roll back to. Recording the action anyway would give the
        // user an undo entry that silently does nothing, which is worse than
        // having no entry at all.
        actionDepth_ = 0;
        return;
    }
    pending_ = std::move(captured.value);
    pendingLabel_ = label;
}

void Document::endAction() {
    if (actionDepth_ == 0 || --actionDepth_ > 0) {
        return;
    }
    if (!pending_.valid()) {
        return;
    }

    HistoryEntry entry;
    entry.snapshot = std::move(pending_);
    entry.label = std::move(pendingLabel_);
    pending_ = ls::DocumentSnapshot{};
    pendingLabel_.clear();

    pushHistory(std::move(entry));
    redoStack_.clear();     // a new action makes the redo branch unreachable
    modified_ = true;
}

void Document::abandonAction() {
    if (actionDepth_ == 0 || --actionDepth_ > 0) {
        return;
    }
    if (pending_.valid()) {
        engine_->restoreDocumentState(id_, pending_);
    }
    pending_ = ls::DocumentSnapshot{};
    pendingLabel_.clear();
}

bool Document::undo() {
    if (undoStack_.empty()) {
        return false;
    }

    // Capture where we are before stepping back, so redo has somewhere to
    // return to. Snapshots restore ids exactly, which is what makes this safe
    // while the interface is holding handles to layers and operations.
    auto current = engine_->snapshotDocumentState(id_);
    if (current.fail()) {
        return false;
    }

    HistoryEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    if (engine_->restoreDocumentState(id_, entry.snapshot).fail()) {
        undoStack_.push_back(std::move(entry));
        return false;
    }

    HistoryEntry forward;
    forward.snapshot = std::move(current.value);
    forward.label = entry.label;
    redoStack_.push_back(std::move(forward));

    modified_ = true;
    return true;
}

bool Document::redo() {
    if (redoStack_.empty()) {
        return false;
    }

    auto current = engine_->snapshotDocumentState(id_);
    if (current.fail()) {
        return false;
    }

    HistoryEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    if (engine_->restoreDocumentState(id_, entry.snapshot).fail()) {
        redoStack_.push_back(std::move(entry));
        return false;
    }

    HistoryEntry back;
    back.snapshot = std::move(current.value);
    back.label = entry.label;
    undoStack_.push_back(std::move(back));

    modified_ = true;
    return true;
}

std::string Document::undoLabel() const {
    return undoStack_.empty() ? std::string() : undoStack_.back().label;
}

std::string Document::redoLabel() const {
    return redoStack_.empty() ? std::string() : redoStack_.back().label;
}

void Document::setHistoryLimit(size_t limit) {
    historyLimit_ = limit;
    trimHistory();
}

void Document::pushHistory(HistoryEntry entry) {
    undoStack_.push_back(std::move(entry));
    trimHistory();
}

void Document::trimHistory() {
    if (historyLimit_ == 0 || undoStack_.size() <= historyLimit_) {
        return;
    }
    const size_t excess = undoStack_.size() - historyLimit_;
    undoStack_.erase(undoStack_.begin(), undoStack_.begin() + static_cast<long>(excess));
}

} // namespace fast
