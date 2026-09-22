/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>  // for seconds, milliseconds
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/DocumentSafetyState.h"

/*
 * Plan 004, step 1: the document-safety model.
 *
 * The model takes no Control and no GTK, so every transition the plan names is exercised here
 * directly. Time is passed in rather than read, which is what makes the transient states - a
 * short-lived Saved confirmation and an autosave that is not announced while it is fast -
 * deterministic instead of flaky.
 */

namespace {

using xoj::safety::DocumentSafetyState;
using xoj::safety::SafetyOperation;
using xoj::safety::SafetyState;
using xoj::safety::TimePoint;

/// A time far enough in the future that no confirmation is still running.
constexpr auto LONG_AFTER = std::chrono::hours(1);

auto stateAt(const DocumentSafetyState& model, TimePoint now) -> SafetyState {
    return model.getSnapshot(now).state;
}

/// Counts how often the model told its listeners that something changed.
class RecordingListener: public DocumentSafetyState::Listener {
public:
    void safetyStateChanged() override { this->changes++; }
    int changes = 0;
};

}  // namespace

/*
 * A document that has just been created or opened matches no save yet, but it has no unsaved
 * edits either: it is Clean until the user writes something.
 */
TEST(DocumentSafetyStateTest, testAFreshDocumentIsClean) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Clean);
    EXPECT_FALSE(snapshot.documentModified);
    EXPECT_FALSE(snapshot.recoveryCopyExists);
    EXPECT_FALSE(snapshot.lastSaveTime.has_value());
    EXPECT_FALSE(snapshot.recoveryTime.has_value());
    EXPECT_FALSE(snapshot.confirmationDeadline.has_value());
}

/*
 * Plan 004, step 1: clean -> modified -> autosaving -> autosaved.
 *
 * An autosave that is quick enough is never announced, so the state does not flicker through
 * something the user cannot read. Once it has been running longer than the visibility delay, it
 * is shown; when it succeeds, the document reports that a recovery copy exists.
 */
TEST(DocumentSafetyStateTest, testCleanToModifiedToAutosavingToAutosaved) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    EXPECT_EQ(stateAt(model, now), SafetyState::Modified) << "an edit with no recovery copy is Modified";
    EXPECT_TRUE(model.isDocumentModified());

    model.autosaveStarted();
    EXPECT_EQ(stateAt(model, now), SafetyState::Modified)
            << "an autosave that has only just started must not be announced";
    EXPECT_EQ(stateAt(model, now + DocumentSafetyState::AUTOSAVE_VISIBILITY_DELAY + std::chrono::seconds(1)),
              SafetyState::Autosaving)
            << "a slow autosave is announced";

    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");
    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Autosaved);
    EXPECT_TRUE(snapshot.documentModified) << "an autosave is not an explicit save";
    EXPECT_TRUE(snapshot.recoveryCopyExists);
    EXPECT_TRUE(snapshot.recoveryCopyIsCurrent) << "the copy was taken from the current edits";
    EXPECT_EQ(snapshot.recoveryPath, fs::path("/tmp/.notes.autosave.xopp"));
    EXPECT_TRUE(snapshot.recoveryTime.has_value());

    // Autosaved is not a confirmation that runs out: the copy is still there an hour later.
    EXPECT_EQ(stateAt(model, now + LONG_AFTER), SafetyState::Autosaved);
}

/*
 * Plan 004, step 1: modified -> saving -> clean.
 *
 * A successful explicit save is announced for a short while and then leaves the document Clean.
 * The recovery copy is no longer the answer to "is my work safe?" once the file itself is.
 */
TEST(DocumentSafetyStateTest, testModifiedToSavingToSavedToClean) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.autosaveStarted();
    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");
    ASSERT_EQ(stateAt(model, now), SafetyState::Autosaved);

    model.saveStarted();
    EXPECT_EQ(stateAt(model, now), SafetyState::Saving);
    EXPECT_TRUE(model.isDocumentModified()) << "a save in progress is not a save";

    model.saveSucceeded("/tmp/notes.xopp");
    auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Saved);
    EXPECT_FALSE(snapshot.documentModified);
    EXPECT_FALSE(snapshot.recoveryCopyExists) << "the saved file replaced the recovery copy";
    ASSERT_TRUE(snapshot.confirmationDeadline.has_value());
    EXPECT_EQ(snapshot.lastSavePath, fs::path("/tmp/notes.xopp"));

    // The confirmation is short-lived; the document is Clean after it.
    EXPECT_EQ(stateAt(model, *snapshot.confirmationDeadline), SafetyState::Clean);
    EXPECT_EQ(stateAt(model, now + LONG_AFTER), SafetyState::Clean);
}

/*
 * Plan 004, step 1 and the test plan's "edits arriving while a save is running": a file written
 * before an edit does not contain that edit.
 *
 * The undo history moved while the job was writing, so the write is acknowledged as a file but
 * the document keeps saying Modified. Reporting Saved here would be the one lie that costs the
 * user their work, because the close prompt trusts this state.
 */
TEST(DocumentSafetyStateTest, testAnEditDuringASaveKeepsTheDocumentModified) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.saveStarted();

    // The user keeps drawing while the job writes.
    model.documentStateChanged(true);

    model.saveSucceeded("/tmp/notes.xopp");

    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Modified) << "the file does not hold the edit that arrived during the save";
    EXPECT_TRUE(snapshot.documentModified);
    EXPECT_TRUE(snapshot.lastSaveTime.has_value()) << "the write really happened and is still reported";
    EXPECT_FALSE(snapshot.confirmationDeadline.has_value());

    // The next save, with no edit in between, is the one that makes it clean.
    model.saveStarted();
    model.saveSucceeded("/tmp/notes.xopp");
    EXPECT_EQ(stateAt(model, now), SafetyState::Saved);
    EXPECT_FALSE(model.isDocumentModified());
}

/*
 * Plan 004, step 1: an edit that arrives while an autosave is running leaves the recovery copy
 * stale, and the snapshot says so rather than claiming the copy covers the document.
 */
TEST(DocumentSafetyStateTest, testAnEditDuringAnAutosaveLeavesTheRecoveryCopyStale) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.autosaveStarted();
    model.documentStateChanged(true);
    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");

    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Autosaved);
    EXPECT_TRUE(snapshot.recoveryCopyExists);
    EXPECT_FALSE(snapshot.recoveryCopyIsCurrent) << "the copy predates the last edit";

    // A later autosave covers the newer edits again.
    model.autosaveStarted();
    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");
    EXPECT_TRUE(model.getSnapshot(now).recoveryCopyIsCurrent);
}

/*
 * Plan 004, step 1: a failed save is an Error that stays visible, because the work is not on
 * disk. Editing does not make it go away.
 */
TEST(DocumentSafetyStateTest, testAFailedSaveIsAPersistentError) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.saveStarted();
    model.saveFailed("Permission denied");

    auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Error);
    EXPECT_EQ(snapshot.failedOperation, SafetyOperation::Save);
    EXPECT_EQ(snapshot.lastError, "Permission denied");
    EXPECT_TRUE(snapshot.documentModified);

    // The user keeps working; the failure is still the thing they have to know about.
    model.documentStateChanged(true);
    EXPECT_EQ(stateAt(model, now), SafetyState::Error);
    EXPECT_EQ(stateAt(model, now + LONG_AFTER), SafetyState::Error) << "an error does not time out";

    // Running the operation again clears it, and so does dismissing it.
    model.saveStarted();
    EXPECT_EQ(stateAt(model, now), SafetyState::Saving);
    model.saveFailed("Permission denied");
    model.clearError();
    EXPECT_EQ(stateAt(model, now), SafetyState::Modified);
}

/*
 * Plan 004, step 1: a failed autosave is an Error too - the difference is that the document is
 * still only Modified underneath, which is what makes it worth a stronger close warning.
 */
TEST(DocumentSafetyStateTest, testAFailedAutosaveIsAnErrorOverAModifiedDocument) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.autosaveStarted();
    model.autosaveFailed("No space left on device");

    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Error);
    EXPECT_EQ(snapshot.failedOperation, SafetyOperation::Autosave);
    EXPECT_EQ(snapshot.lastError, "No space left on device");
    EXPECT_TRUE(snapshot.documentModified);
    EXPECT_FALSE(snapshot.recoveryCopyExists) << "a failed autosave leaves no recovery copy";

    // A successful autosave afterwards is the way out.
    model.autosaveStarted();
    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");
    EXPECT_EQ(stateAt(model, now), SafetyState::Autosaved);
}

/*
 * Plan 004, step 1 and the done criterion "Export feedback does not alter dirty state".
 *
 * An export writes a copy somewhere else. Whatever it does, the document is exactly as saved or
 * as unsaved as it was before.
 */
TEST(DocumentSafetyStateTest, testExportFeedbackNeverChangesTheDirtyState) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    // An export of a clean document leaves it clean.
    model.exportStarted();
    EXPECT_EQ(stateAt(model, now), SafetyState::Exporting);
    EXPECT_FALSE(model.isDocumentModified());

    model.exportSucceeded("/tmp/notes.pdf");
    auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Exported);
    EXPECT_FALSE(snapshot.documentModified) << "a successful export must not mark a document modified";
    EXPECT_FALSE(snapshot.recoveryCopyExists);
    EXPECT_TRUE(snapshot.lastExportTime.has_value());
    EXPECT_EQ(snapshot.lastExportPath, fs::path("/tmp/notes.pdf"));

    // The confirmation runs out and the document state shows again.
    ASSERT_TRUE(snapshot.confirmationDeadline.has_value());
    EXPECT_EQ(stateAt(model, *snapshot.confirmationDeadline), SafetyState::Clean);
}

TEST(DocumentSafetyStateTest, testASuccessfulExportOfAModifiedDocumentLeavesItModified) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.exportStarted();
    model.exportSucceeded("/tmp/notes.pdf");

    auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Exported);
    EXPECT_TRUE(snapshot.documentModified) << "the document itself is still not saved";
    EXPECT_FALSE(snapshot.recoveryCopyExists);

    snapshot = model.getSnapshot(now + LONG_AFTER);
    EXPECT_EQ(snapshot.state, SafetyState::Modified) << "and it says so once the export is forgotten";
    EXPECT_TRUE(snapshot.documentModified);
}

TEST(DocumentSafetyStateTest, testAFailedExportIsAnErrorAndKeepsTheDocumentState) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.autosaveStarted();
    model.autosaveSucceeded("/tmp/.notes.autosave.xopp");
    ASSERT_EQ(stateAt(model, now), SafetyState::Autosaved);

    model.exportStarted();
    model.exportFailed("Disk quota exceeded");

    auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Error);
    EXPECT_EQ(snapshot.failedOperation, SafetyOperation::Export);
    EXPECT_EQ(snapshot.lastError, "Disk quota exceeded");
    EXPECT_TRUE(snapshot.recoveryCopyExists) << "the export's failure says nothing about the recovery copy";

    model.clearError();
    EXPECT_EQ(stateAt(model, now), SafetyState::Autosaved) << "the document state was never touched";
}

/*
 * Plan 004, step 1: an undo that walks the history back to the last saved position makes the
 * document clean again, and an undo in the middle of the history does not.
 */
TEST(DocumentSafetyStateTest, testUndoingBackToTheSavedStateMakesTheDocumentClean) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();
    // Past the confirmation of the save below, so the document state is what is being read.
    const auto later = now + DocumentSafetyState::CONFIRMATION_DURATION + std::chrono::seconds(1);

    model.documentStateChanged(true);
    model.saveStarted();
    model.saveSucceeded("/tmp/notes.xopp");
    EXPECT_FALSE(model.isDocumentModified());

    model.documentStateChanged(true);
    EXPECT_EQ(stateAt(model, later), SafetyState::Modified);

    model.documentStateChanged(false);
    EXPECT_EQ(stateAt(model, later), SafetyState::Clean);
    EXPECT_FALSE(model.isDocumentModified());
}

/*
 * Plan 004, step 1: a save, autosave or export started for the previous document must not
 * describe the new one. This is the case a user hits by opening a file while an autosave of the
 * old one is still queued.
 */
TEST(DocumentSafetyStateTest, testACompletionFromAPreviousDocumentIsIgnored) {
    DocumentSafetyState model;
    const auto now = std::chrono::system_clock::now();

    model.documentStateChanged(true);
    model.saveStarted();
    model.autosaveStarted();

    model.resetDocument();
    EXPECT_EQ(stateAt(model, now), SafetyState::Clean);

    model.saveSucceeded("/tmp/old.xopp");
    model.autosaveSucceeded("/tmp/.old.autosave.xopp");
    model.exportFailed("late failure");

    const auto snapshot = model.getSnapshot(now);
    EXPECT_EQ(snapshot.state, SafetyState::Clean) << "nothing the previous document did describes this one";
    EXPECT_FALSE(snapshot.documentModified);
    EXPECT_FALSE(snapshot.recoveryCopyExists);
    EXPECT_FALSE(snapshot.lastSaveTime.has_value());
    EXPECT_FALSE(snapshot.lastExportTime.has_value());
}

/*
 * Plan 004, step 1: the model is observable, and a listener that unregisters itself while it is
 * being notified does not take the notification down with it.
 */
TEST(DocumentSafetyStateTest, testListenersAreNotifiedAndCanUnregisterWhileBeingNotified) {
    DocumentSafetyState model;

    RecordingListener listener;
    model.addListener(&listener);
    model.addListener(&listener);  // the same listener twice is still one listener

    model.documentStateChanged(true);
    EXPECT_EQ(listener.changes, 1);

    model.saveStarted();
    model.saveSucceeded("/tmp/notes.xopp");
    EXPECT_EQ(listener.changes, 3);

    model.removeListener(&listener);
    model.documentStateChanged(true);
    EXPECT_EQ(listener.changes, 3) << "a removed listener is not told anything";

    // Self-unregistration during a notification is safe: the walk does not use an iterator.
    class SelfRemoving: public DocumentSafetyState::Listener {
    public:
        explicit SelfRemoving(DocumentSafetyState& model): model(model) {}
        void safetyStateChanged() override {
            this->calls++;
            if (this->calls == 1) {
                this->model.removeListener(this);
            }
        }
        DocumentSafetyState& model;
        int calls = 0;
    };

    SelfRemoving selfRemoving(model);
    model.addListener(&selfRemoving);
    model.documentStateChanged(true);
    model.documentStateChanged(true);
    EXPECT_EQ(selfRemoving.calls, 1);
}
