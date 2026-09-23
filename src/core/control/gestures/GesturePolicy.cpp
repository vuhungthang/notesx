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

#include "GesturePolicy.h"

#include <algorithm>

namespace xoj::gesture {

auto GesturePolicy::requiredConfidence(const GestureSettings& settings, GestureKind kind) -> double {
    switch (kind) {
        case GestureKind::Circle:
            return std::clamp(settings.circleConfidenceFloor, 0.0, 1.0);
        case GestureKind::Scribble:
            return std::clamp(settings.scribbleConfidenceFloor, 0.0, 1.0);
    }
    return 1.0;  // an unknown gesture is never confident enough
}

auto GesturePolicy::evaluate(const GestureSettings& settings, const GesturePolicyInput& input) -> GesturePolicyResult {
    if (!gestureEnabled(settings, input.kind)) {
        return {GestureDecision::Ignore, GestureIgnoreReason::Disabled};
    }
    if (input.confidence < requiredConfidence(settings, input.kind)) {
        return {GestureDecision::Ignore, GestureIgnoreReason::ConfidenceTooLow};
    }
    if (!input.overlapsExistingInk) {
        // Both gestures this plan ships destroy the ink they are drawn with. Acting on a gesture
        // that covered nothing would still have destroyed that ink and done nothing else.
        return {GestureDecision::Ignore, GestureIgnoreReason::NoOverlap};
    }
    if (!input.undoGroupAvailable) {
        // A destructive action that cannot be undone in one step must not run (Plan 008's stop
        // condition). The caller says whether it can; without that, nothing happens.
        return {GestureDecision::Ignore, GestureIgnoreReason::NoUndoGroup};
    }
    return {GestureDecision::Preview, GestureIgnoreReason::None};
}

auto GesturePolicy::confirm(const GestureSettings& settings, const GesturePolicyInput& input) -> GesturePolicyResult {
    const GesturePolicyResult preview = evaluate(settings, input);
    if (preview.decision != GestureDecision::Preview) {
        return preview;
    }
    return {GestureDecision::Confirm, GestureIgnoreReason::None};
}

auto gestureIgnoreReasonText(GestureIgnoreReason reason) -> const char* {
    switch (reason) {
        case GestureIgnoreReason::None:
            return "";
        case GestureIgnoreReason::Disabled:
            return "the gesture is turned off";
        case GestureIgnoreReason::ConfidenceTooLow:
            return "the gesture was not certain enough";
        case GestureIgnoreReason::NoOverlap:
            return "the gesture did not cover anything";
        case GestureIgnoreReason::NoUndoGroup:
            return "the action could not be undone in one step";
    }
    return "";
}

}  // namespace xoj::gesture
