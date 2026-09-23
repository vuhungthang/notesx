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

#include "CircleGestureRecognizer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace xoj::gesture {

namespace {
constexpr double PI = 3.14159265358979323846;

/// The distance the stroke is resampled to before measuring, from its own size. Fixed physical
/// spacing would make a large circle and a small one see different sample counts; a spacing that
/// follows the size keeps the recognizer scale-invariant up to the minDiameter floor.
auto resampleSpacingFor(const GestureBounds& b) -> double {
    const double diagonal = b.diagonal();
    if (!(diagonal > 0.0)) {
        return 2.0;
    }
    return std::clamp(diagonal / 64.0, 1.5, 8.0);
}

/// How close to a full turn the stroke came, in radians, about the given centre.
auto signedTurnAbout(const std::vector<StrokePoint>& points, double cx, double cy) -> double {
    double turned = 0.0;
    bool previous = false;
    double lastAngle = 0.0;
    for (const StrokePoint& p: points) {
        const double dx = p.x - cx;
        const double dy = p.y - cy;
        if (std::hypot(dx, dy) < 1e-6) {
            continue;  // a sample on the centre says nothing about the turn
        }
        const double angle = std::atan2(dy, dx);
        if (previous) {
            double delta = angle - lastAngle;
            while (delta > PI) {
                delta -= 2.0 * PI;
            }
            while (delta < -PI) {
                delta += 2.0 * PI;
            }
            turned += delta;
        }
        lastAngle = angle;
        previous = true;
    }
    return std::abs(turned);
}

/// Mean radius and its relative standard deviation about the given centre.
auto radialSpread(const std::vector<StrokePoint>& points, double cx, double cy, double& meanOut) -> double {
    std::vector<double> radii;
    radii.reserve(points.size());
    for (const StrokePoint& p: points) {
        radii.push_back(std::hypot(p.x - cx, p.y - cy));
    }
    if (radii.empty()) {
        meanOut = 0.0;
        return 1.0;
    }
    const double sum = std::accumulate(radii.begin(), radii.end(), 0.0);
    const double mean = sum / static_cast<double>(radii.size());
    meanOut = mean;
    if (mean <= 1e-9) {
        return 1.0;
    }
    double variance = 0.0;
    for (double r: radii) {
        variance += (r - mean) * (r - mean);
    }
    variance /= static_cast<double>(radii.size());
    return std::sqrt(variance) / mean;
}
}  // namespace

auto CircleParameters::defaults() -> CircleParameters { return CircleParameters{}; }

CircleGestureRecognizer::CircleGestureRecognizer(): params(CircleParameters::defaults()) {}

CircleGestureRecognizer::CircleGestureRecognizer(CircleParameters parameters): params(std::move(parameters)) {}

auto CircleGestureRecognizer::recognize(const GestureStroke& stroke) const -> std::optional<GestureCandidate> {
    if (stroke.size() < this->params.minPoints) {
        return std::nullopt;
    }

    const GestureBounds bounds = stroke.bounds();
    // A deliberate circling gesture is large. This is the constraint that keeps letter-sized
    // loops - O, a, e, a zero in an equation - out of the gesture's reach.
    if (std::min(bounds.width(), bounds.height()) < this->params.minDiameter) {
        return std::nullopt;
    }

    if (bounds.height() > 1e-9) {
        const double aspect = bounds.width() / bounds.height();
        if (aspect < this->params.minAspectRatio || aspect > this->params.maxAspectRatio) {
            return std::nullopt;
        }
    }

    const GestureStroke sampled = stroke.resampled(resampleSpacingFor(bounds));
    const std::vector<StrokePoint>& points = sampled.getPoints();
    if (points.size() < this->params.minPoints) {
        return std::nullopt;
    }

    const double pathLength = sampled.pathLength();
    if (pathLength <= 1e-9) {
        return std::nullopt;
    }

    // Closure: the hand must have come back near where it started. Measured against the stroke's
    // own length so a large and a small loop are judged the same.
    const double closure = sampled.closureDistance();
    if (closure > this->params.maxClosureFraction * pathLength) {
        return std::nullopt;
    }
    if (closure > this->params.maxClosureOfDiameter * std::min(bounds.width(), bounds.height())) {
        return std::nullopt;
    }

    // The turned angle: a loop, not an arc, a spiral or a back-and-forth.
    const double turn = signedTurnAbout(points, bounds.centerX(), bounds.centerY());
    if (turn < this->params.minAngularCoverage) {
        return std::nullopt;
    }

    // How round it is: the radius must not wander much about the centre.
    double meanRadius = 0.0;
    const double variation = radialSpread(points, bounds.centerX(), bounds.centerY(), meanRadius);
    if (variation > this->params.maxRadiusVariation) {
        return std::nullopt;
    }

    if (this->params.useTiming && sampled.hasTiming() && sampled.duration() < this->params.minDurationMs) {
        return std::nullopt;
    }

    // Confidence from the margins on each constraint, so a stroke that just scrapes past every
    // limit is not reported as a confident circle.
    const double closureScore =
            std::clamp(1.0 - closure / std::max(1e-9, this->params.maxClosureFraction * pathLength), 0.0, 1.0);
    const double turnScore = std::clamp(
            (turn - this->params.minAngularCoverage) / (2.0 * PI - this->params.minAngularCoverage) + 0.5, 0.0, 1.0);
    const double radialScore = std::clamp(1.0 - variation / std::max(1e-9, this->params.maxRadiusVariation), 0.0, 1.0);
    const double confidence = 0.2 + 0.8 * (closureScore + turnScore + radialScore) / 3.0;

    if (confidence < this->params.acceptConfidence) {
        return std::nullopt;
    }

    return GestureCandidate{GestureKind::Circle, confidence, bounds};
}

}  // namespace xoj::gesture
