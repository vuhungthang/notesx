/*
 * Xournal++
 *
 * The gesture recognition boundary (Plan 008, step 3)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>

#include "control/gestures/GestureStroke.h"

namespace xoj::gesture {

/**
 * Plan 008: which gesture a candidate is about.
 *
 * These are the two recognizers this plan ships. A gesture the application learns later is another
 * enumerator and another recognizer; nothing here switches on the kind except the policy, which
 * reads the setting that turns the gesture on.
 */
enum class GestureKind {
    Circle,
    Scribble,
};

/// The stable id of a kind: the settings file stores these, so they outlive the enumerator order.
auto gestureKindId(GestureKind kind) -> const char*;
/// Parse the id back; an unknown id is nullopt, never a silently-chosen default.
auto gestureKindFromId(const std::string& id) -> std::optional<GestureKind>;

/**
 * Plan 008: what a recognizer returns when it recognises something.
 *
 * A candidate says how sure the recognizer is and what the gesture covers. It says nothing about
 * what should happen: selecting, erasing and refusing are policy, and the policy runs where the
 * document is - never inside the recognizer.
 */
struct GestureCandidate {
    GestureKind kind = GestureKind::Circle;
    /// In [0, 1]. Higher means the recognizer is more sure; the policy sets the accepted floor.
    double confidence = 0.0;
    /// What the gesture covers, for previewing and for the overlap check.
    GestureBounds bounds;
};

/**
 * Plan 008: the recognition boundary.
 *
 * A recognizer is a pure function of a stroke: the same stroke always gives the same answer, no
 * clock, no document, no widget. That is what makes the corpus replayable - a fixture is a stroke
 * and an expected classification, and the test can run it with nothing built around it.
 *
 * "No match" is the common answer and is not an error.
 */
class GestureRecognizer {
public:
    virtual ~GestureRecognizer() = default;

    /// @return a candidate when this stroke is this gesture, nullopt otherwise.
    virtual auto recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> = 0;
};

}  // namespace xoj::gesture
