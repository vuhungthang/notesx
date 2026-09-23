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

#include "GestureStroke.h"

#include <limits>

namespace xoj::gesture {

auto GestureStroke::bounds() const -> GestureBounds {
    if (this->points.empty()) {
        return {};
    }

    GestureBounds b{this->points.front().x, this->points.front().y, this->points.front().x, this->points.front().y};
    for (const StrokePoint& p: this->points) {
        b.minX = std::min(b.minX, p.x);
        b.minY = std::min(b.minY, p.y);
        b.maxX = std::max(b.maxX, p.x);
        b.maxY = std::max(b.maxY, p.y);
    }
    return b;
}

auto GestureStroke::pathLength() const -> double {
    double length = 0.0;
    for (std::size_t i = 1; i < this->points.size(); i++) {
        length += std::hypot(this->points[i].x - this->points[i - 1].x, this->points[i].y - this->points[i - 1].y);
    }
    return length;
}

auto GestureStroke::closureDistance() const -> double {
    if (this->points.size() < 2) {
        return std::numeric_limits<double>::infinity();
    }
    return std::hypot(this->points.back().x - this->points.front().x, this->points.back().y - this->points.front().y);
}

auto GestureStroke::hasTiming() const -> bool {
    return std::any_of(this->points.begin(), this->points.end(), [](const StrokePoint& p) { return p.t > 0.0; });
}

auto GestureStroke::duration() const -> double {
    if (this->points.size() < 2) {
        return 0.0;
    }
    return std::max(0.0, this->points.back().t - this->points.front().t);
}

auto GestureStroke::translated(double dx, double dy) const -> GestureStroke {
    std::vector<StrokePoint> moved;
    moved.reserve(this->points.size());
    for (const StrokePoint& p: this->points) {
        moved.push_back(StrokePoint{p.x + dx, p.y + dy, p.t});
    }
    return GestureStroke(std::move(moved));
}

auto GestureStroke::scaled(double factor) const -> GestureStroke {
    std::vector<StrokePoint> scaled;
    scaled.reserve(this->points.size());
    for (const StrokePoint& p: this->points) {
        scaled.push_back(StrokePoint{p.x * factor, p.y * factor, p.t});
    }
    return GestureStroke(std::move(scaled));
}

auto GestureStroke::resampled(double spacing) const -> GestureStroke {
    return GestureStroke(resamplePolyline(this->points, spacing));
}

auto resamplePolyline(const std::vector<StrokePoint>& points, double spacing) -> std::vector<StrokePoint> {
    if (points.size() < 2 || !(spacing > 0.0)) {
        return points;
    }

    std::vector<StrokePoint> out;
    out.push_back(points.front());

    // Distance still to travel before the next sample is due, carried across segments so the
    // output is not aligned to where the input happened to report.
    double remaining = spacing;
    StrokePoint previous = points.front();

    for (std::size_t i = 1; i < points.size(); i++) {
        const StrokePoint& current = points[i];
        double segment = std::hypot(current.x - previous.x, current.y - previous.y);
        if (segment <= 0.0) {
            previous = current;
            continue;
        }

        double start = 0.0;
        while (segment - start >= remaining) {
            start += remaining;
            const double ratio = start / segment;
            const StrokePoint interpolated{previous.x + (current.x - previous.x) * ratio,
                                           previous.y + (current.y - previous.y) * ratio,
                                           previous.t + (current.t - previous.t) * ratio};
            out.push_back(interpolated);
            remaining = spacing;
        }
        remaining -= (segment - start);
        previous = current;
    }

    // Keep the true end point, so closure distance is not lost to the sample grid.
    if (out.size() < 2 || std::hypot(out.back().x - points.back().x, out.back().y - points.back().y) > 0.0) {
        out.push_back(points.back());
    }
    return out;
}

}  // namespace xoj::gesture
