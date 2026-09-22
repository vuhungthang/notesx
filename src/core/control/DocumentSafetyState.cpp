#include "DocumentSafetyState.h"

#include <algorithm>  // for find, remove
#include <utility>    // for move

#include <glib.h>  // for g_warning

using namespace xoj::safety;

void DocumentSafetyState::addListener(Listener* listener) {
    if (listener == nullptr || std::find(this->listeners.begin(), this->listeners.end(), listener) !=
                                       this->listeners.end()) {
        return;
    }
    this->listeners.emplace_back(listener);
}

void DocumentSafetyState::removeListener(Listener* listener) {
    this->listeners.erase(std::remove(this->listeners.begin(), this->listeners.end(), listener),
                          this->listeners.end());
}

void DocumentSafetyState::notify() {
    // A listener may unregister itself while it is being told, so the list is walked by index
    // against its own size rather than through an iterator.
    for (std::size_t i = 0; i < this->listeners.size(); i++) {
        this->listeners[i]->safetyStateChanged();
    }
}

void DocumentSafetyState::documentStateChanged(bool modified) {
    if (modified) {
        this->modificationSerial++;
    }
    this->modified = modified;
    this->notify();
}

void DocumentSafetyState::resetDocument() {
    this->documentEpoch++;

    this->modified = false;
    this->modificationSerial++;

    this->saveInFlight = false;
    this->autosaveInFlight = false;
    this->exportInFlight = false;

    this->recoveryAvailable = false;
    this->recoveryPath.clear();
    this->recoveryTime.reset();
    this->recoverySerial = this->modificationSerial;

    this->savePath.clear();
    this->saveTime.reset();
    this->exportPath.clear();
    this->exportTime.reset();

    this->lastResult.reset();

    this->notify();
}

void DocumentSafetyState::saveStarted() {
    this->saveInFlight = true;
    this->saveStartSerial = this->modificationSerial;
    this->saveEpoch = this->documentEpoch;
    this->lastResult.reset();
    this->notify();
}

void DocumentSafetyState::saveSucceeded(const fs::path& target) {
    if (!this->belongsToCurrentDocument(this->saveEpoch)) {
        return;
    }

    this->saveInFlight = false;
    this->savePath = target;
    this->saveTime = std::chrono::system_clock::now();

    /*
     * The file is the best recovery there is, so the old recovery copy is no longer the answer to
     * "is my work safe?". It is not deleted here: Control owns the recovery file's lifetime.
     */
    this->recoveryAvailable = false;
    this->recoveryPath.clear();
    this->recoveryTime.reset();

    /*
     * Only clean if nothing was edited while the file was being written. Otherwise the document
     * really does differ from what is on disk, and saying "Saved" would be a lie the user pays for
     * when they close the window.
     */
    if (this->modificationSerial == this->saveStartSerial) {
        this->modified = false;
    }

    this->lastResult = Result{SafetyOperation::Save, true, std::string(), this->saveTime.value()};
    this->notify();
}

void DocumentSafetyState::saveFailed(std::string error) {
    if (!this->belongsToCurrentDocument(this->saveEpoch)) {
        return;
    }

    this->saveInFlight = false;
    this->lastResult = Result{SafetyOperation::Save, false, std::move(error), std::chrono::system_clock::now()};
    this->notify();
}

void DocumentSafetyState::autosaveStarted() {
    this->autosaveInFlight = true;
    this->autosaveStartedAt = std::chrono::system_clock::now();
    this->autosaveStartSerial = this->modificationSerial;
    this->autosaveEpoch = this->documentEpoch;
    this->lastResult.reset();
    this->notify();
}

void DocumentSafetyState::autosaveSucceeded(const fs::path& recoveryFile) {
    if (!this->belongsToCurrentDocument(this->autosaveEpoch)) {
        return;
    }

    this->autosaveInFlight = false;
    this->recoveryAvailable = true;
    this->recoveryPath = recoveryFile;
    this->recoveryTime = std::chrono::system_clock::now();
    this->recoverySerial = this->autosaveStartSerial;

    this->lastResult = Result{SafetyOperation::Autosave, true, std::string(), this->recoveryTime.value()};
    this->notify();
}

void DocumentSafetyState::autosaveFailed(std::string error) {
    if (!this->belongsToCurrentDocument(this->autosaveEpoch)) {
        return;
    }

    this->autosaveInFlight = false;
    this->lastResult = Result{SafetyOperation::Autosave, false, std::move(error), std::chrono::system_clock::now()};
    this->notify();
}

void DocumentSafetyState::exportStarted() {
    this->exportInFlight = true;
    this->exportEpoch = this->documentEpoch;
    this->lastResult.reset();
    this->notify();
}

void DocumentSafetyState::exportSucceeded(const fs::path& target) {
    if (!this->belongsToCurrentDocument(this->exportEpoch)) {
        return;
    }

    // An export writes a copy somewhere else. It says nothing about whether the document itself
    // has been saved, so `modified` is deliberately left alone.
    this->exportInFlight = false;
    this->exportPath = target;
    this->exportTime = std::chrono::system_clock::now();

    this->lastResult = Result{SafetyOperation::Export, true, std::string(), this->exportTime.value()};
    this->notify();
}

void DocumentSafetyState::exportFailed(std::string error) {
    if (!this->belongsToCurrentDocument(this->exportEpoch)) {
        return;
    }

    this->exportInFlight = false;
    this->lastResult = Result{SafetyOperation::Export, false, std::move(error), std::chrono::system_clock::now()};
    this->notify();
}

void DocumentSafetyState::clearError() {
    if (!this->lastResult || this->lastResult->succeeded) {
        return;
    }
    this->lastResult.reset();
    this->notify();
}

auto DocumentSafetyState::belongsToCurrentDocument(unsigned long epoch) const -> bool {
    if (epoch == this->documentEpoch) {
        return true;
    }
    g_warning("Ignoring the completion of a save, autosave or export started for a previous document");
    return false;
}

auto DocumentSafetyState::idleState() const -> SafetyState {
    if (!this->modified) {
        return SafetyState::Clean;
    }
    return this->recoveryAvailable ? SafetyState::Autosaved : SafetyState::Modified;
}

auto DocumentSafetyState::getSnapshot(TimePoint now) const -> SafetySnapshot {
    SafetySnapshot snapshot;
    snapshot.documentModified = this->modified;
    snapshot.recoveryCopyExists = this->recoveryAvailable;
    snapshot.recoveryCopyIsCurrent = this->recoveryAvailable && this->recoverySerial == this->modificationSerial;

    snapshot.lastSavePath = this->savePath;
    snapshot.lastSaveTime = this->saveTime;
    snapshot.recoveryPath = this->recoveryPath;
    snapshot.recoveryTime = this->recoveryTime;
    snapshot.lastExportPath = this->exportPath;
    snapshot.lastExportTime = this->exportTime;

    // A running operation is what the user is waiting for, so it wins over anything else.
    if (this->saveInFlight) {
        snapshot.state = SafetyState::Saving;
        return snapshot;
    }
    if (this->autosaveInFlight && now - this->autosaveStartedAt >= AUTOSAVE_VISIBILITY_DELAY) {
        snapshot.state = SafetyState::Autosaving;
        return snapshot;
    }
    if (this->exportInFlight) {
        snapshot.state = SafetyState::Exporting;
        return snapshot;
    }

    if (this->lastResult) {
        const Result& result = *this->lastResult;

        // A failure is persistent: it stays until the operation is run again or dismissed.
        if (!result.succeeded) {
            snapshot.state = SafetyState::Error;
            snapshot.lastError = result.error;
            snapshot.failedOperation = result.operation;
            return snapshot;
        }

        const TimePoint deadline = result.finishedAt + CONFIRMATION_DURATION;
        if (now < deadline) {
            switch (result.operation) {
                case SafetyOperation::Save:
                    /*
                     * A save that completed while an edit was arriving did not write that edit, so
                     * the confirmation is withheld and the document keeps saying Modified.
                     */
                    if (this->modified) {
                        snapshot.state = SafetyState::Modified;
                        return snapshot;
                    }
                    snapshot.state = SafetyState::Saved;
                    snapshot.confirmationDeadline = deadline;
                    return snapshot;
                case SafetyOperation::Export:
                    snapshot.state = SafetyState::Exported;
                    snapshot.confirmationDeadline = deadline;
                    return snapshot;
                case SafetyOperation::Autosave:
                    // Not transient: the recovery copy stays the answer while the work is modified.
                    snapshot.state = this->idleState();
                    return snapshot;
                case SafetyOperation::None:
                    break;
            }
        }
    }

    snapshot.state = this->idleState();
    return snapshot;
}
