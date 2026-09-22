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
    void documentSaved();

private:
    void clearRedo();
    void printContents();

private:
    std::deque<UndoActionPtr> undoList;
    std::deque<UndoActionPtr> redoList;

    UndoAction* savedUndo = nullptr;
    /// The position of the contents of the recovery copy on disk; only ever advanced by a write
    /// that succeeded, and only ever to a position captured before that write started.
    const UndoAction* autosavedUndo = nullptr;

    std::vector<UndoRedoListener*> listener;

    Control* control = nullptr;
};
