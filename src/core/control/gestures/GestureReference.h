/*
 * Xournal++
 *
 * The gesture reference (Plan 008, step 7)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "control/gestures/GestureRecognizer.h"
#include "control/gestures/GestureSettings.h"

namespace xoj::gesture {

/**
 * Plan 008: one line of the gesture reference.
 *
 * The reference is generated from the settings the application is running with, not written down:
 * what a gesture is called, how it is triggered and whether it is on are all read from the live
 * value, so turning a gesture off in the settings is what the reference then says. There is no
 * second list to keep in step - the same rule the shortcut reference follows.
 *
 * `settingId` is which field turns the gesture on and off. A "disable this gesture" action taken
 * from a misrecognised gesture's feedback needs exactly that, and nothing else.
 */
struct GestureReferenceRow {
    /// Stable id of the gesture, for a test or a feedback action to name it.
    std::string gestureId;
    /// User-visible name.
    std::string title;
    /// What it does, and what it will not do.
    std::string detail;
    /// How the user reaches it - the palette binding, the stylus button, the tap.
    std::string binding;
    /// Whether the live settings leave it on.
    bool enabled = false;
    /**
     * Whether this build can carry the gesture out at all.
     *
     * A gesture that is available is one the application would act on when it is on; one that is not
     * available must not be offered as if it were - circle-to-select recognises the circle and then
     * leaves it as ink, because a selection cannot be represented as one undo step (the plan's stop
     * condition), so the control for it is disabled and the reference says so rather than promising
     * a selection.
     */
    bool available = true;
    /// The settings field that decides `enabled`, so feedback can offer to turn it off.
    std::string settingId;
};

/**
 * Plan 008: the gesture reference, generated from the live settings.
 *
 * The rows are in a stable order and every gesture this build has appears, on or off - a gesture
 * nobody can reach because it is off is still worth a line saying so, which is the whole point of
 * a reference. A gesture this build cannot carry out at all says that instead of saying "On" or
 * "Off" (see GestureReferenceRow::available).
 */
auto buildGestureReference(const GestureSettings& settings) -> std::vector<GestureReferenceRow>;

/// The line for one gesture, or nothing when this build has no such gesture.
auto gestureReferenceRow(const GestureSettings& settings, GestureKind kind) -> std::optional<GestureReferenceRow>;

}  // namespace xoj::gesture
