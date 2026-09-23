/*
 * Xournal++
 *
 * Circle-to-select recognition (Plan 008, step 4)
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
 * Plan 008: the constraints a stroke must meet to be a circle-to-select gesture.
 *
 * The thresholds are the whole of the policy about what a "circle" is, and they are deliberately
 * conservative. The recognizer's job is to leave ordinary handwriting alone: a handwritten O, a,
 * e, a small zero in an equation and a shading loop are all closed loops, and the only thing that
 * separates them from a deliberate circling gesture is scale and regularity. The defaults encode
 * that:
 *
 *  - minDiameter rejects letter-sized loops. A deliberate circling gesture is drawn large; the
 *    letters a stylus writes while taking notes are not.
 *  - minAngularCoverage rejects arcs, incomplete loops and spirals, which never close the turn.
 *  - maxClosureFraction and maxRadiusVariation reject open curves and wobbly, non-circular loops.
 *
 * Every field is a compatibility-sensitive setting (Plan 008's maintenance note): changing one
 * requires replaying the whole fixture corpus, not only the positive examples.
 */
struct CircleParameters {
    /// Below this many samples after resampling the stroke is too coarse to judge.
    std::size_t minPoints = 8;
    /// The shorter side of the bounding box must be at least this large: letter-sized loops fail.
    double minDiameter = 90.0;
    /// Endpoints must be within this fraction of the total path length of each other.
    double maxClosureFraction = 0.22;
    /**
     * Endpoints must also be within this fraction of the bounding box's shorter side.
     *
     * This is what rejects a spiral: it turns a full loop and more, so the angle and the path
     * length look right, but it ends a long way from where it started.
     */
    double maxClosureOfDiameter = 0.35;
    /// The turned angle about the centroid must reach this many radians: a full loop, not an arc.
    double minAngularCoverage = 5.5;  // ≈ 1.75 π
    /// Standard deviation of the radius, as a fraction of the mean radius: how round it is.
    double maxRadiusVariation = 0.42;
    /// Bounding-box aspect ratio bounds, to reject flattened ellipses and straight lines.
    double minAspectRatio = 0.6;
    double maxAspectRatio = 1.67;
    /**
     * Whether the stroke's duration takes part in the decision.
     *
     * False by default, and it only has an effect when the stroke actually carries timing: a
     * recognizer must not invent a clock. The fixture corpus has no timestamps, so it exercises
     * the geometry alone; a recorded stroke exercises this too.
     */
    bool useTiming = false;
    /// A very quick flick that happens to close is not the deliberate gesture; milliseconds.
    double minDurationMs = 60.0;
    /// The confidence a candidate must reach to be handed to the policy at all.
    double acceptConfidence = 0.5;

    static auto defaults() -> CircleParameters;
};

/**
 * Plan 008: circle-to-select, as a pure function of a stroke.
 *
 * Returns a candidate - confidence and bounds - or nothing. In particular it does not decide to
 * suppress the ink, nor does it select anything: the candidate goes to GesturePolicy, which runs
 * where the document is.
 */
class CircleGestureRecognizer: public GestureRecognizer {
public:
    CircleGestureRecognizer();
    explicit CircleGestureRecognizer(CircleParameters parameters);

    auto recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> override;

    const CircleParameters& parameters() const { return this->params; }

private:
    CircleParameters params;
};

}  // namespace xoj::gesture
