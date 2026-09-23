/*
 * Xournal++
 *
 * Gesture strokes: the input the recognizers take (Plan 008, step 3)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace xoj::gesture {

/**
 * Plan 008: one sample of a stylus stroke, in page coordinates.
 *
 * Only this - a position and, when the backend provides it, a time in milliseconds since the
 * stroke started - crosses the recognition boundary. No GdkEvent, no XojPageView, no document:
 * a recognizer must be runnable from a unit test with nothing built.
 *
 * `t` is 0 when the backend gives no timing. A recognizer that depends on duration must say so
 * rather than assume a clock: see CircleGestureRecognizer's `useTiming`.
 */
struct StrokePoint {
    double x = 0.0;
    double y = 0.0;
    double t = 0.0;
};

/// An axis-aligned rectangle in page coordinates. Used to describe what a gesture affects.
struct GestureBounds {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    auto width() const -> double { return this->maxX - this->minX; }
    auto height() const -> double { return this->maxY - this->minY; }
    auto centerX() const -> double { return (this->minX + this->maxX) / 2.0; }
    auto centerY() const -> double { return (this->minY + this->maxY) / 2.0; }
    auto diagonal() const -> double { return std::hypot(this->width(), this->height()); }
    auto area() const -> double { return std::max(0.0, this->width()) * std::max(0.0, this->height()); }

    bool intersects(const GestureBounds& other) const {
        return this->minX <= other.maxX && other.minX <= this->maxX && this->minY <= other.maxY &&
               other.minY <= this->maxY;
    }
    bool contains(const GestureBounds& other) const {
        return this->minX <= other.minX && this->minY <= other.minY && this->maxX >= other.maxX &&
               this->maxY >= other.maxY;
    }
};

/**
 * Plan 008: a stroke as the recognizers see it.
 *
 * This is deliberately a plain value: the input handler hands the points over, and from then on
 * everything - recognizing, transforming for a corpus, testing - is arithmetic on doubles. The
 * transformations the corpus tests use (translated/scaled/resampled) return new strokes rather
 * than mutating, so a fixture can be replayed in several shapes.
 */
class GestureStroke {
public:
    GestureStroke() = default;
    explicit GestureStroke(std::vector<StrokePoint> points): points(std::move(points)) {}

    const std::vector<StrokePoint>& getPoints() const { return this->points; }
    bool empty() const { return this->points.empty(); }
    std::size_t size() const { return this->points.size(); }

    /// The bounding box of the samples; the whole stroke's bounds when it is empty is a point at 0.
    auto bounds() const -> GestureBounds;

    /// Total length of the polyline through the samples.
    auto pathLength() const -> double;

    /// Distance between the first and last sample; huge (infinity) for an empty stroke.
    auto closureDistance() const -> double;

    /// True when any sample carries a positive time, i.e. duration can be measured.
    bool hasTiming() const;

    /// Duration in the stroke's own units; 0 when there is no timing.
    auto duration() const -> double;

    /// The same shape moved by (dx, dy).
    auto translated(double dx, double dy) const -> GestureStroke;

    /// The same shape scaled about the origin. Times are left alone: scaling is a corpus concern.
    auto scaled(double factor) const -> GestureStroke;

    /**
     * The same path, sampled evenly every `spacing` units of arc length.
     *
     * Sampling density must not change what a recognizer sees (Plan 008's transformation tests).
     * Resampling to a fixed spacing is how a recognizer can be made insensitive to the rate the
     * hardware reports at.
     */
    auto resampled(double spacing) const -> GestureStroke;

private:
    std::vector<StrokePoint> points;
};

/// Samples a polyline evenly by arc length. Shared by resampled() and the recognizers.
auto resamplePolyline(const std::vector<StrokePoint>& points, double spacing) -> std::vector<StrokePoint>;

}  // namespace xoj::gesture
