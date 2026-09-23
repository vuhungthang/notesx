/*
 * Xournal++
 *
 * Where a completed stylus stroke becomes a gesture action, or ordinary ink (Plan 008, steps 3-5, 7)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "control/gestures/CircleGestureRecognizer.h"
#include "control/gestures/GesturePolicy.h"
#include "control/gestures/ScribbleGestureRecognizer.h"
#include "model/PageRef.h"

class Control;
class Document;
class Layer;
class Stroke;
class UndoRedoHandler;

namespace xoj::gesture {

/**
 * Plan 008: what became of a completed stylus stroke at the commit seam.
 *
 * OrdinaryInk is the safe answer and the one every refusal produces: the caller finalizes the
 * stroke exactly as it would have without this plan. Committed means the gesture took the stroke,
 * an action ran, and there is exactly one undo record for everything that action did.
 */
enum class GestureCommitOutcome {
    OrdinaryInk,
    Committed,
};

/// The whole of what the seam did and why, for the caller, for feedback and for tests.
struct GestureCommitResult {
    GestureCommitOutcome outcome = GestureCommitOutcome::OrdinaryInk;
    /// The kind of the candidate, meaningful only when `recognized` is true.
    GestureKind kind = GestureKind::Circle;
    /// Whether a recognizer matched at all. False means the stroke was never a gesture.
    bool recognized = false;
    /// The recognizer's candidate. Valid when `recognized` is true.
    GestureCandidate candidate;
    /// What the policy decided; Ignore with a reason when the action did not run.
    GestureDecision decision = GestureDecision::Ignore;
    GestureIgnoreReason reason = GestureIgnoreReason::None;
    /// How many existing elements the action removed. Zero unless Committed.
    std::size_t affectedElements = 0;
};

/**
 * Plan 008: the recognition half of the commit seam - recognizers and settings, no document.
 *
 * This is the piece that can be run without a page, a view or a Control: given a stroke and the
 * user's settings it says whether any recognizer recognises the stroke. Everything that depends on
 * what the document holds happens in commitStylusGesture(), where the document is.
 */
class GestureCommit {
public:
    explicit GestureCommit(const GestureSettings& settings);

    /// The first enabled recognizer that recognises this stroke, or nothing.
    ///
    /// Only recognizers whose gesture the settings leave on are asked, so a disabled gesture never
    /// even reaches the policy - there is nothing to recognise.
    auto recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate>;

    /// Whether any gesture these settings leave on has a recognizer that may run.
    auto anyEnabled() const -> bool;

    const GestureSettings& getSettings() const { return this->settings; }

private:
    GestureSettings settings;
    CircleGestureRecognizer circle;
    ScribbleGestureRecognizer scribble;
};

/**
 * Plan 008: the whole strokes of `layer` whose bounding box intersects `area`.
 *
 * The documented rule for scribble-to-erase: a stroke is either wholly erased or left alone, and
 * which it is depends only on whether its bounding box overlaps the gesture's region. Partial
 * erasing is deliberately not offered here - it would mean a second kind of eraser undo, and the
 * plan requires one undo group, not one per stroke.
 */
auto strokesIntersecting(const Layer* layer, const GestureBounds& area) -> std::vector<const Stroke*>;

/**
 * Plan 008: remove the given whole strokes and record it as exactly one undo group.
 *
 * One DeleteUndoAction holds every stroke, so a single Undo restores all of them and a single Redo
 * removes them again. The strokes must belong to `layer`. The function takes the document lock
 * itself for the removals, and fires the page's element-changed notifications once it has released
 * it, the way the drawing path does after it has added a stroke.
 *
 * @return how many strokes were removed. Zero means nothing happened and no undo record was made.
 */
auto eraseStrokesAsOneUndoGroup(const PageRef& page, Layer* layer, const std::vector<const Stroke*>& strokes,
                                UndoRedoHandler* undo, Document& doc) -> std::size_t;

/**
 * Plan 008: the main-thread seam between a completed stylus stroke and the document.
 *
 * Called from the input path where the finished stroke is in hand and has not yet been committed,
 * with the page and layer the stroke was drawn on. It recognises the stroke, asks the document what
 * the gesture covers and whether the action can be one undo group, lets GesturePolicy decide, and -
 * only on Confirm - runs the action as one undo group.
 *
 * Nothing here runs on a background thread: it is called from the input handler that already owns
 * the document, and it takes the document lock only for the lookup and the removal.
 *
 * When it returns OrdinaryInk - the settings are off, the stroke is not a gesture, or the policy
 * refused it - the caller must finalize ordinary ink exactly as it always did. That is what makes
 * "disabled gestures leave original ink unchanged" and "no action without an undo record" true
 * rather than merely intended.
 */
auto commitStylusGesture(const GestureSettings& settings, Control& control, const PageRef& page, Layer* layer,
                         const GestureStroke& stroke) -> GestureCommitResult;

}  // namespace xoj::gesture
