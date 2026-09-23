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

#include "ScribbleGestureRecognizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xoj::gesture {

namespace {
/**
 * How many times the path reverses its lateral direction.
 *
 * The stroke is walked from one sample to the next; steps shorter than `stepFloor` are ignored, so
 * a jittery pen does not read as an endless scrub. A change in the sign of the horizontal step -
 * the direction a scrub actually alternates in - counts as one reversal; the vertical step is
 * counted the same way and the larger of the two is reported, so a scrub drawn up and down reads
 * the same as one drawn left and right.
 */
auto countReversals(const std::vector<StrokePoint>& points, double stepFloor) -> std::size_t {
    auto reversalsAlong = [&](bool horizontal) {
        std::size_t reversals = 0;
        int previousSign = 0;
        for (std::size_t i = 1; i < points.size(); i++) {
            const double dx = points[i].x - points[i - 1].x;
            const double dy = points[i].y - points[i - 1].y;
            const double step = std::hypot(dx, dy);
            if (step < stepFloor) {
                continue;
            }
            const double component = horizontal ? dx : dy;
            if (std::abs(component) < step * 0.5) {
                continue;  // this step is mostly along the other axis
            }
            const int sign = component > 0.0 ? 1 : -1;
            if (previousSign != 0 && sign != previousSign) {
                reversals++;
            }
            previousSign = sign;
        }
        return reversals;
    };

    return std::max(reversalsAlong(true), reversalsAlong(false));
}
}  // namespace

auto ScribbleParameters::defaults() -> ScribbleParameters { return ScribbleParameters{}; }

ScribbleGestureRecognizer::ScribbleGestureRecognizer(): params(ScribbleParameters::defaults()) {}

ScribbleGestureRecognizer::ScribbleGestureRecognizer(ScribbleParameters parameters): params(std::move(parameters)) {}

auto ScribbleGestureRecognizer::recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> {
    if (stroke.size() < this->params.minPoints) {
        return std::nullopt;
    }

    const GestureBounds bounds = stroke.bounds();
    const double span = bounds.diagonal();
    if (span < std::max(this->params.minDiameter, 1e-9)) {
        return std::nullopt;
    }
    if (span > this->params.maxDiagonal) {
        return std::nullopt;
    }

    const double pathLength = stroke.pathLength();
    if (pathLength <= 1e-9) {
        return std::nullopt;
    }

    const double density = pathLength / span;
    if (density < this->params.minDensity) {
        return std::nullopt;
    }

    const std::size_t reversals = countReversals(stroke.getPoints(), this->params.reversalStepFraction * span);
    if (reversals < this->params.minReversals) {
        return std::nullopt;
    }

    // The average run between two turns. Shading and hatching make long runs; a scrub short ones.
    const double passLength = pathLength / static_cast<double>(reversals + 1);
    if (passLength > this->params.maxPassLength) {
        return std::nullopt;
    }

    const double densityScore = std::clamp(1.0 - this->params.minDensity / std::max(1e-9, density), 0.0, 1.0);
    const double reversalScore = std::clamp(static_cast<double>(reversals - this->params.minReversals) /
                                                            static_cast<double>(2 * this->params.minReversals) +
                                                    0.5,
                                            0.0, 1.0);
    const double passScore = std::clamp(1.0 - passLength / std::max(1e-9, this->params.maxPassLength), 0.0, 1.0);
    const double confidence = 0.2 + 0.8 * (densityScore + reversalScore + passScore) / 3.0;

    if (confidence < this->params.acceptConfidence) {
        return std::nullopt;
    }

    return GestureCandidate{GestureKind::Scribble, confidence, bounds};
}

}  // namespace xoj::gesture
