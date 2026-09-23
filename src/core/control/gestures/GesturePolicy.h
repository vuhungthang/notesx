/*
 * Xournal++
 *
 * Where a recognised gesture becomes an action - or does not (Plan 008, steps 3, 5, 7)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "control/gestures/GestureRecognizer.h"
#include "control/gestures/GestureSettings.h"

namespace xoj::gesture {

/**
 * Plan 008: what should be done about a candidate.
 *
 * Ignore is the safe answer and the one every failure produces. Preview means the candidate may be
 * shown before anything happens; Confirm means the action may run, and only ever after a preview
 * was offered and the document side agreed an undo record exists.
 */
enum class GestureDecision {
    Ignore,
    Preview,
    Confirm,
};

/// Why a candidate was ignored. The UI uses this to explain itself, tests use it to be precise.
enum class GestureIgnoreReason {
    None,
    /// The gesture's setting is off.
    Disabled,
    /// The recognizer's or the user's confidence floor was not reached.
    ConfidenceTooLow,
    /// The gesture does not cover ink the document holds, and it must.
    NoOverlap,
    /// The document side could not give the action one undo group.
    NoUndoGroup,
};

/**
 * Plan 008: what the document side knows that the recognizer cannot.
 *
 * Everything here is a fact the caller looked up - whether the candidate's bounds cover existing
 * elements, whether an undo group could be opened. None of it is geometry, and none of it is
 * computed here: the policy is a pure decision over stated facts, so it is testable without a
 * document while the code that gathers the facts lives with the document.
 */
struct GesturePolicyInput {
    GestureKind kind = GestureKind::Circle;
    double confidence = 0.0;
    /**
     * Whether the gesture covers ink the document holds.
     *
     * Required for both gestures this plan ships: selecting nothing is a no-op with a destroyed
     * ink stroke, and erasing nothing is a no-op with a destroyed ink stroke. Defaults to false,
     * the safe side - a caller that has not checked the document cannot trigger an action.
     */
    bool overlapsExistingInk = false;
    /// Whether the document side can wrap the action in exactly one undo group.
    bool undoGroupAvailable = false;
};

struct GesturePolicyResult {
    GestureDecision decision = GestureDecision::Ignore;
    GestureIgnoreReason reason = GestureIgnoreReason::Disabled;
};

/**
 * Plan 008: the decision layer between a pure recognizer and the document.
 *
 * The recognizer says "this looks like a circle". This says whether anything may happen about it,
 * given the user's settings, the confidence they asked for, whether the gesture covers ink, and
 * whether the action can be undone in one step. No gesture action in this plan happens without
 * first passing through here, which is what makes "disabled gestures leave ordinary ink unchanged"
 * and "no action without an undo record" true by construction rather than by inspection.
 */
class GesturePolicy {
public:
    /// The confidence the user's settings demand of a gesture.
    static auto requiredConfidence(const GestureSettings& settings, GestureKind kind) -> double;

    /// What may be done about the candidate now: Ignore, or Preview.
    static auto evaluate(const GestureSettings& settings, const GesturePolicyInput& input) -> GesturePolicyResult;

    /// The document side has previewed and the user (or the timing rule) lets it through.
    static auto confirm(const GestureSettings& settings, const GesturePolicyInput& input) -> GesturePolicyResult;
};

/// A sentence for the reason, for feedback and for tests.
auto gestureIgnoreReasonText(GestureIgnoreReason reason) -> const char*;

}  // namespace xoj::gesture
