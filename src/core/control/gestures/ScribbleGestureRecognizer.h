/*
 * Xournal++
 *
 * Scribble-to-erase recognition (Plan 008, step 5)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>

#include "control/gestures/GestureRecognizer.h"

namespace xoj::gesture {

/**
 * Plan 008: the constraints a stroke must meet to be a scribble-to-erase gesture.
 *
 * This is the most dangerous recognizer in the plan, so it asks for more than any ordinary mark
 * provides, and it stays disabled by default.
 *
 * A scrub over ink and a patch of shading or hatching are both back-and-forth marks. What
 * separates them, on the evidence available before anything is interpreted:
 *
 *  - minReversals: the direction must turn over and over. A cross-out turns once; a few passes of
 *    shading turn a few times.
 *  - maxPassLength: a scrub is made of short strokes. Anyone shading or hatching a region draws
 *    long ones, so the average run between two turns is long; a scrub's is short.
 *  - minDensity: path length over the span's diagonal. A scrub is packed; an open hatch is not.
 *  - maxDiagonal: the scrub is local. A long sweep is a pan or a stroke, not an erase.
 *
 * These are conservative on purpose: a large, deliberate scrub drawn with long strokes may be
 * missed, and that is the acceptable direction of error for an action that destroys ink.
 *
 * Even a stroke that meets all of these is only a candidate: the policy still requires the gesture
 * to overlap ink the document actually holds, and the action is one undo group.
 */
struct ScribbleParameters {
    std::size_t minPoints = 16;
    /// How many times the lateral direction must reverse.
    std::size_t minReversals = 12;
    /// Path length divided by the span's diagonal.
    double minDensity = 8.0;
    /**
     * The average run between two reversals must be at most this long.
     *
     * The discriminator against shading and hatching: those are drawn with long strokes across the
     * region, a scrub with short ones in one spot.
     */
    double maxPassLength = 160.0;
    /// The shorter side of the span must be at least this large, so tiny noise is not a scrub.
    double minDiameter = 24.0;
    /// The span's diagonal must be at most this large: the scrub is local.
    double maxDiagonal = 400.0;
    /// A reversal only counts when the step is at least this long, in units of the span.
    double reversalStepFraction = 0.02;
    /// The confidence a candidate must reach to be handed to the policy at all.
    double acceptConfidence = 0.5;

    static auto defaults() -> ScribbleParameters;
};

/**
 * Plan 008: scribble-to-erase, as a pure function of a stroke.
 *
 * Returns a candidate - confidence and bounds - or nothing. It does not erase anything and does
 * not touch the document: the candidate goes to GesturePolicy, which runs where the document is.
 */
class ScribbleGestureRecognizer: public GestureRecognizer {
public:
    ScribbleGestureRecognizer();
    explicit ScribbleGestureRecognizer(ScribbleParameters parameters);

    auto recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> override;

    const ScribbleParameters& parameters() const { return this->params; }

private:
    ScribbleParameters params;
};

}  // namespace xoj::gesture
