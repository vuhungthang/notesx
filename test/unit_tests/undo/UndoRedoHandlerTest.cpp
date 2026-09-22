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

#include <memory>   // for make_unique, unique_ptr
#include <string>   // for string
#include <utility>  // for move

#include <gtest/gtest.h>

#include "undo/UndoAction.h"       // for UndoAction, UndoActionPtr
#include "undo/UndoRedoHandler.h"  // for UndoRedoHandler

/*
 * Plan 004: the autosave marker, and the edit that arrives while the recovery file is being
 * written.
 *
 * The marker is what `Control::autosaveCallback()` consults on every tick, so it must record what
 * the copy on disk contains. The write snapshots the contents first and swaps the file in later,
 * and an edit can land in between: committing the position the write ends at would claim that edit
 * is covered when it is not, and the tick would then skip the write the edit needs.
 *
 * These tests drive the sequence AutosaveJob runs - capture the position with the snapshot, then
 * commit it only once the file is in place - against the handler itself, with no job, scheduler or
 * window, so the interleaving is deterministic rather than a matter of timing.
 */

namespace {

/// An undo action that does nothing: the handler only ever looks at the identity of the action.
class StubUndoAction: public UndoAction {
public:
    explicit StubUndoAction(std::string name): UndoAction(std::move(name)) {}

    auto undo(Control* /*control*/) -> bool override { return true; }
    auto redo(Control* /*control*/) -> bool override { return true; }
    auto getText() -> std::string override { return "stub edit"; }
};

/// One edit, as it reaches the undo history.
void addEdit(UndoRedoHandler& handler) { handler.addUndoAction(std::make_unique<StubUndoAction>("stub edit")); }

}  // namespace

/*
 * A fresh document with an edit in it has no recovery copy yet, so the first tick has work to do.
 */
TEST(UndoRedoHandlerAutosaveMarkerTest, testAFreshEditIsOwedARecoveryCopy) {
    UndoRedoHandler handler(nullptr);

    EXPECT_FALSE(handler.isChangedAutosave()) << "nothing was edited yet";

    addEdit(handler);

    EXPECT_TRUE(handler.isChangedAutosave());
}

/*
 * Plan 004, test plan: an edit that arrives while the recovery file is being written. The write
 * snapshots the contents and captures the position they sit at; the second edit lands after that
 * capture, so it is not in the file. Committing the captured position must leave that edit owed a
 * copy, and a later successful autosave - of a snapshot that does contain it - must clear it.
 */
TEST(UndoRedoHandlerAutosaveMarkerTest, testAnEditDuringTheWriteStaysPendingForTheNextAutosave) {
    UndoRedoHandler handler(nullptr);

    // The edit the user starts from.
    addEdit(handler);

    // Autosave starts: the contents are snapshotted and the position they sit at is captured.
    const auto snapshotPosition = handler.captureAutosavePosition();

    // A second edit arrives while the file is being serialized. It is not in that file.
    addEdit(handler);

    // The write succeeds and commits the captured position, not the one the write ended at.
    handler.documentAutosaved(snapshotPosition);

    EXPECT_TRUE(handler.isChangedAutosave()) << "an edit made during the write is not in the file";

    // The next autosave snapshots both edits, and committing its own captured position clears the
    // pending state: the copy on disk now covers the document.
    const auto nextPosition = handler.captureAutosavePosition();
    handler.documentAutosaved(nextPosition);

    EXPECT_FALSE(handler.isChangedAutosave());
}

/*
 * Plan 004, test plan: a failed write commits nothing. The marker still describes the last copy
 * that really reached the disk, so the edits made since it are still owed a recovery copy, and the
 * retry that snapshots them clears the pending state when it succeeds.
 */
TEST(UndoRedoHandlerAutosaveMarkerTest, testAFailedAutosaveDoesNotAdvanceTheMarker) {
    UndoRedoHandler handler(nullptr);

    addEdit(handler);
    handler.documentAutosaved(handler.captureAutosavePosition());
    ASSERT_FALSE(handler.isChangedAutosave());

    // A further edit, and the autosave that tries to cover it: the position is captured where the
    // contents are snapshotted, and the write then fails.
    addEdit(handler);
    const auto failedPosition = handler.captureAutosavePosition();

    EXPECT_TRUE(handler.isChangedAutosave()) << "the failed write must not claim the edit is recovered";

    // The retry captures the same contents and succeeds.
    handler.documentAutosaved(failedPosition);

    EXPECT_FALSE(handler.isChangedAutosave());
}

/*
 * Capturing a position only reads: it must not look like a write happened. If it advanced the
 * marker by itself, an autosave that then failed would leave its edit looking recovered.
 */
TEST(UndoRedoHandlerAutosaveMarkerTest, testCapturingAPositionMarksNothingAsRecovered) {
    UndoRedoHandler handler(nullptr);

    addEdit(handler);
    const auto position = handler.captureAutosavePosition();

    EXPECT_TRUE(handler.isChangedAutosave());
    EXPECT_EQ(position.top, handler.captureAutosavePosition().top) << "capturing twice sees the same contents";
}

/*
 * The documented convention of the marker: an autosave of the contents as they are clears the
 * pending state, and moving the document off that position afterwards makes the copy stale again.
 */
TEST(UndoRedoHandlerAutosaveMarkerTest, testUndoingTheAutosavedEditLeavesTheCopyStale) {
    UndoRedoHandler handler(nullptr);

    addEdit(handler);
    handler.documentAutosaved(handler.captureAutosavePosition());
    ASSERT_FALSE(handler.isChangedAutosave());

    // Undoing puts the document back at a position the copy does not hold.
    handler.undo();

    EXPECT_TRUE(handler.isChangedAutosave());
}
