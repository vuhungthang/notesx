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

#include "GestureCommit.h"

#include <memory>
#include <utility>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Rectangle.h"

namespace xoj::gesture {

GestureCommit::GestureCommit(const GestureSettings& settings): settings(settings) {}

auto GestureCommit::anyEnabled() const -> bool {
    return gestureEnabled(this->settings, GestureKind::Circle) || gestureEnabled(this->settings, GestureKind::Scribble);
}

auto GestureCommit::recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> {
    // Only the gestures the user has left on are recognised. A gesture that is off cannot even
    // produce a candidate, which is the first of the two places the settings gate a gesture - the
    // second is GesturePolicy, which also sees the document's facts.
    if (gestureEnabled(this->settings, GestureKind::Circle)) {
        if (auto candidate = this->circle.recognize(stroke); candidate) {
            return candidate;
        }
    }
    if (gestureEnabled(this->settings, GestureKind::Scribble)) {
        if (auto candidate = this->scribble.recognize(stroke); candidate) {
            return candidate;
        }
    }
    return std::nullopt;
}

auto strokesIntersecting(const Layer* layer, const GestureBounds& area) -> std::vector<const Stroke*> {
    std::vector<const Stroke*> hits;
    if (layer == nullptr) {
        return hits;
    }

    const xoj::util::Rectangle<double> region(area.minX, area.minY, area.width(), area.height());
    for (const Element* e: layer->getElementsView()) {
        if (e->getType() != ELEMENT_STROKE) {
            continue;
        }
        const auto* stroke = dynamic_cast<const Stroke*>(e);
        if (stroke == nullptr) {
            continue;
        }
        if (!e->getBoundingBox().intersects(region).has_value()) {
            continue;
        }
        hits.push_back(stroke);
    }
    return hits;
}

auto eraseStrokesAsOneUndoGroup(const PageRef& page, Layer* layer, const std::vector<const Stroke*>& strokes,
                                UndoRedoHandler* undo, Document& doc) -> std::size_t {
    if (layer == nullptr) {
        return 0;
    }

    // One action for every stroke, so one Undo restores all of them: the plan's "one undo group".
    auto action = std::make_unique<DeleteUndoAction>(page, /*eraser=*/true);
    DeleteUndoAction* actionPtr = action.get();

    std::size_t removed = 0;
    std::vector<const Element*> removedElements;

    doc.lock();
    for (const Stroke* stroke: strokes) {
        const Element* element = stroke;
        auto [owned, pos] = layer->removeElement(stroke);
        if (pos < 0 || !owned) {
            continue;
        }
        actionPtr->addElement(layer, std::move(owned), pos);
        removedElements.push_back(element);
        removed++;
    }
    doc.unlock();

    if (removed == 0) {
        // Nothing was removed: making a record of nothing would put an undo step in the history
        // that does nothing, which is worse than not acting.
        return 0;
    }

    if (undo != nullptr) {
        undo->addUndoAction(std::move(action));
    }

    // Notify after the lock is released, the way the drawing path does once a stroke is in a layer.
    for (const Element* element: removedElements) {
        page->fireElementChanged(element);
    }
    return removed;
}

auto commitStylusGesture(const GestureSettings& settings, Control& control, const PageRef& page, Layer* layer,
                         const GestureStroke& stroke) -> GestureCommitResult {
    GestureCommitResult result;

    GestureCommit commit(settings);
    const std::optional<GestureCandidate> candidate = commit.recognize(stroke);
    if (!candidate) {
        return result;  // not a gesture: ordinary ink, exactly as before this plan
    }
    result.recognized = true;
    result.candidate = *candidate;
    result.kind = candidate->kind;

    if (candidate->kind == GestureKind::Circle) {
        /*
         * Circle-to-select cannot be one undo group, and so it does not run.
         *
         * Selecting is not a document edit in this application: there is no SelectUndoAction, and
         * the selection machinery (Selector, EditSelection) produces none. Suppressing the circle
         * ink leaves the document exactly as it was, so there is no edit to record either. An
         * action with no undo record is the thing the plan forbids, so `undoGroupAvailable` is
         * false and GesturePolicy refuses the candidate: the stroke stays ordinary ink.
         */
        GesturePolicyInput input;
        input.kind = candidate->kind;
        input.confidence = candidate->confidence;
        input.overlapsExistingInk = true;  // stated for completeness; the refusal below is first
        input.undoGroupAvailable = false;  // a selection is not undoable with the existing APIs
        const GesturePolicyResult policy = GesturePolicy::confirm(settings, input);
        result.decision = policy.decision;
        result.reason = policy.reason;
        return result;
    }

    // Scribble-to-erase: look up the ink the gesture covers and whether the action can be one undo
    // group. This is the only point where the document is touched, and it is on the main thread.
    Document* doc = control.getDocument();
    if (doc == nullptr) {
        return result;
    }

    doc->lock();
    const std::vector<const Stroke*> covered = strokesIntersecting(layer, candidate->bounds);
    doc->unlock();

    GesturePolicyInput input;
    input.kind = candidate->kind;
    input.confidence = candidate->confidence;
    input.overlapsExistingInk = !covered.empty();
    // The action is one DeleteUndoAction over every covered stroke - one undo group by
    // construction, so it is available exactly when there is something to erase.
    input.undoGroupAvailable = !covered.empty();

    const GesturePolicyResult policy = GesturePolicy::confirm(settings, input);
    result.decision = policy.decision;
    result.reason = policy.reason;

    if (policy.decision != GestureDecision::Confirm) {
        return result;
    }

    UndoRedoHandler* undo = control.getUndoRedoHandler();
    const std::size_t removed = eraseStrokesAsOneUndoGroup(page, layer, covered, undo, *doc);

    if (removed == 0) {
        // The lookup and the removal must agree. If they do not, nothing was erased and the stroke
        // stays ordinary ink rather than being silently swallowed without a record.
        result.decision = GestureDecision::Ignore;
        result.reason = GestureIgnoreReason::NoOverlap;
        return result;
    }

    result.outcome = GestureCommitOutcome::Committed;
    result.affectedElements = removed;
    return result;
}

}  // namespace xoj::gesture
