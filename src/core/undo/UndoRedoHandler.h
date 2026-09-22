/*
 * Xournal++
 *
 * Handles Undo and Redo
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <deque>   // for deque
#include <string>  // for string
#include <vector>  // for vector

#include "model/PageRef.h"  // for PageRef

#include "UndoAction.h"  // for UndoActionPtr

class Control;

class UndoRedoListener {
public:
    virtual void undoRedoChanged() = 0;
    virtual void undoRedoPageChanged(PageRef page) = 0;

    virtual ~UndoRedoListener() = default;
};

class UndoRedoHandler {
public:
    explicit UndoRedoHandler(Control* control);
    virtual ~UndoRedoHandler();

    void undo();
    void redo();

    bool canUndo();
    bool canRedo();

    void addUndoAction(UndoActionPtr action);

    std::string undoDescription();
    std::string redoDescription();

    void clearContents();

    void fireUpdateUndoRedoButtons(const std::vector<PageRef>& pages);
    void addUndoRedoListener(UndoRedoListener* listener);

    bool isChanged();
    bool isChangedAutosave();

    /**
     * The undo position the document contents of a recovery copy sit at.
     *
     * An autosave takes one of these where it snapshots the contents, and commits it only once the
     * recovery file really is on disk. Reading the position off the top of the undo history when
     * the write ends instead would record an edit that arrived while the file was being
     * serialized, which the copy does not contain: `isChangedAutosave()` would then answer false
     * and the next tick would skip the write that edit needs.
     */
    struct AutosavePosition {
        /// The action on top of the undo history when the snapshot was taken; null if it was empty.
        const UndoAction* top = nullptr;
    };

    /// The undo position of the contents an autosave is about to snapshot.
    auto captureAutosavePosition() const -> AutosavePosition;
    /// Record `position` as what the recovery copy that was just written contains.
    void documentAutosaved(AutosavePosition position);

    /**
     * The undo position the document contents of a written file sit at.
     *
     * A save takes one of these where it snapshots the contents, and commits it only once the file
     * really is on disk. Reading the position off the top of the undo history when the write ends
     * instead would record an edit that arrived while the file was being serialized, which the
     * file does not contain: `isChanged()` would then answer false for a document whose newest
     * edit is nowhere on disk, and closing the editor would not ask to save it.
     *
     * The token is an identity, never dereferenced: it names the action that was on top of the
     * undo history, and the history owns it. A job may only hold one while the document it was
     * captured for is the one loaded, which is what `Control::block()` - held for the whole of a
     * blocking save job - guarantees: the actions that could clear the history are disabled while
     * the job is in flight.
     */
    struct SavePosition {
        /// The action on top of the undo history when the snapshot was taken; null if it was empty.
        const UndoAction* top = nullptr;
    };

    /// The undo position of the contents a save is about to snapshot.
    auto captureSavePosition() const -> SavePosition;
    /**
     * Record `position` as what the file that was just written contains.
     *
     * Only a write that succeeded may commit its captured position, and only the one it captured.
     */
    void documentSaved(SavePosition position);
    /**
     * Mark the document as it is now as saved.
     *
     * This is the load and reset path: a document that was just loaded, or one that was just
     * discarded, has no edits of its own, so the state on screen is what the file holds and there
     * is no earlier snapshot for a position to describe. A save must not use this overload - the
     * position it has to record is the one its contents were snapshotted at, which
     * `captureSavePosition()` took before the write started.
     */
    void documentSaved();

private:
    void clearRedo();
    void printContents();

private:
    std::deque<UndoActionPtr> undoList;
    std::deque<UndoActionPtr> redoList;

    /// The position of the contents of the file on disk; only ever advanced by a write that
    /// succeeded, and only ever to a position captured before that write started.
    const UndoAction* savedUndo = nullptr;
    /// The position of the contents of the recovery copy on disk; only ever advanced by a write
    /// that succeeded, and only ever to a position captured before that write started.
    const UndoAction* autosavedUndo = nullptr;

    std::vector<UndoRedoListener*> listener;

    Control* control = nullptr;
};
