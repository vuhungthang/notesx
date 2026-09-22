/*
 * Xournal++
 *
 * The editor's single source of truth for what it tells the user about the safety of the
 * current document: whether it matches an explicit save, whether a recovery copy covers it,
 * and what the last save, autosave or export did.
 *
 * Plan 004. The model holds no GTK and no Control: it is fed events and read through a
 * snapshot, so every transition is unit-testable and the visible save state has exactly one
 * source. A job never reports success on its own; it reports the outcome of a write that
 * already happened, and the model refuses to mark the document clean when the undo history
 * moved while that write was running.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>  // for system_clock
#include <optional>
#include <string>  // for string
#include <vector>  // for vector

#include "filesystem.h"  // for path

namespace xoj::safety {

using TimePoint = std::chrono::system_clock::time_point;

/**
 * The states a user can be shown.
 *
 * `Clean`, `Modified`, `Saving`, `Saved`, `Autosaving`, `Autosaved` and `Error` are states of the
 * document. `Exporting` and `Exported` belong to the export operation: an export is feedback about
 * a file that was written and never a statement about the document, so it must not make a modified
 * document look saved.
 */
enum class SafetyState {
    /// The document matches the last explicit save.
    Clean,
    /// Unsaved edits exist and no recovery copy covers them.
    Modified,
    /// An explicit save is in progress.
    Saving,
    /// A successful explicit save, announced for a short while.
    Saved,
    /// A recovery write is in progress.
    Autosaving,
    /// A recovery copy of the modified work exists on disk.
    Autosaved,
    /// An export is in progress.
    Exporting,
    /// A successful export, announced for a short while.
    Exported,
    /// The last save, autosave or export failed.
    Error,
};

/// The operation a state, a timestamp or an error belongs to.
enum class SafetyOperation { None, Save, Autosave, Export };

/**
 * Everything a view needs to describe the current safety state.
 *
 * The document's dirty state (`documentModified`) is reported separately from `state`, because
 * `state` may be an operation's own state: an export in progress must not hide that the document
 * has unsaved edits.
 */
struct SafetySnapshot {
    /// The state to show.
    SafetyState state = SafetyState::Clean;

    /// The document has edits that no explicit save covers. An export never changes this.
    bool documentModified = false;

    /// A recovery copy of this document exists on disk.
    bool recoveryCopyExists = false;
    /// The recovery copy was written after the last edit, so it covers the whole document.
    bool recoveryCopyIsCurrent = false;

    std::optional<TimePoint> lastSaveTime;
    fs::path lastSavePath;

    std::optional<TimePoint> recoveryTime;
    fs::path recoveryPath;

    std::optional<TimePoint> lastExportTime;
    fs::path lastExportPath;

    /// Set while `state` is a transient confirmation, so a view can schedule the moment it ends.
    std::optional<TimePoint> confirmationDeadline;

    /// The text of the last failure. Empty unless `state` is `Error`.
    std::string lastError;
    SafetyOperation failedOperation = SafetyOperation::None;
};

/**
 * The document-safety model.
 *
 * Events come from the undo history, from the explicit save and from the save, autosave and export
 * jobs. They are expected on the UI thread: the jobs reach it through `Job::callAfterRun()`, which
 * is how the existing save path already reports its result.
 */
class DocumentSafetyState {
public:
    /// Something changed; re-read the snapshot.
    class Listener {
    public:
        virtual void safetyStateChanged() = 0;
        virtual ~Listener() = default;
    };

    /// How long a successful operation stays announced before the document state shows again.
    static constexpr auto CONFIRMATION_DURATION = std::chrono::seconds(4);
    /**
     * An autosave that finishes sooner than this is never announced as `Autosaving`, so a fast
     * recovery write does not flicker through a state the user cannot read anyway.
     */
    static constexpr auto AUTOSAVE_VISIBILITY_DELAY = std::chrono::milliseconds(300);

    DocumentSafetyState() = default;
    DocumentSafetyState(const DocumentSafetyState&) = delete;
    DocumentSafetyState(DocumentSafetyState&&) = delete;
    auto operator=(const DocumentSafetyState&) -> DocumentSafetyState& = delete;
    auto operator=(DocumentSafetyState&&) -> DocumentSafetyState& = delete;
    virtual ~DocumentSafetyState() = default;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    /// The undo history's answer to "does the document differ from the last explicit save?".
    void documentStateChanged(bool modified);

    /**
     * A new or freshly opened document. Operations that were running against the previous document
     * are ignored when they complete, so a stale write cannot describe the new one.
     */
    void resetDocument();

    void saveStarted();
    /// The document was written to `target`.
    void saveSucceeded(const fs::path& target);
    void saveFailed(std::string error);

    void autosaveStarted();
    /// A recovery copy was written to `recoveryFile`.
    void autosaveSucceeded(const fs::path& recoveryFile);
    void autosaveFailed(std::string error);

    void exportStarted();
    void exportSucceeded(const fs::path& target);
    void exportFailed(std::string error);

    /// Dismiss the last failure without running the operation again.
    void clearError();

    /// The state as of `now`. The time is a parameter so the transient states are deterministic.
    auto getSnapshot(TimePoint now = std::chrono::system_clock::now()) const -> SafetySnapshot;

    /// Whether the document must not be closed without asking the user.
    auto isDocumentModified() const -> bool { return this->modified; }

private:
    struct Result {
        SafetyOperation operation = SafetyOperation::None;
        bool succeeded = false;
        std::string error;
        TimePoint finishedAt{};
    };

    void notify();
    /// The state to show when nothing is running and no result is being announced.
    auto idleState() const -> SafetyState;
    /// Whether a completion belongs to the document the operation was started for.
    auto belongsToCurrentDocument(unsigned long epoch) const -> bool;

    std::vector<Listener*> listeners;

    bool modified = false;
    /**
     * Bumped whenever the undo history moves on, which includes every edit. A save captures it
     * when it starts and compares it when it ends: a file written before an edit does not contain
     * that edit, so the document must stay Modified even though the write succeeded.
     */
    unsigned long modificationSerial = 0;
    unsigned long saveStartSerial = 0;
    unsigned long autosaveStartSerial = 0;
    /// The serial the recovery copy was taken at, for "does it cover the current edits?".
    unsigned long recoverySerial = 0;
    /// Bumped when the document is replaced; stale completions are dropped.
    unsigned long documentEpoch = 0;

    unsigned long saveEpoch = 0;
    unsigned long autosaveEpoch = 0;
    unsigned long exportEpoch = 0;

    bool saveInFlight = false;
    bool autosaveInFlight = false;
    bool exportInFlight = false;
    TimePoint autosaveStartedAt{};

    bool recoveryAvailable = false;
    fs::path recoveryPath;
    std::optional<TimePoint> recoveryTime;

    fs::path savePath;
    std::optional<TimePoint> saveTime;

    fs::path exportPath;
    std::optional<TimePoint> exportTime;

    std::optional<Result> lastResult;
};

}  // namespace xoj::safety
