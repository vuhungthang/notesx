/*
 * Xournal++
 *
 * Plan 008: the stroke value type the recognizers take
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>

#include <gtest/gtest.h>

#include "control/gestures/GestureStroke.h"

using namespace xoj::gesture;

namespace {
auto makeCircle(double cx, double cy, double r, int n) -> GestureStroke {
    std::vector<StrokePoint> points;
    for (int i = 0; i <= n; i++) {
        const double a = 2.0 * 3.14159265358979323846 * i / n;
        points.push_back(StrokePoint{cx + r * std::cos(a), cy + r * std::sin(a), 0.0});
    }
    return GestureStroke(std::move(points));
}
}  // namespace

TEST(GestureStrokeTest, boundsSurroundEverySample) {
    GestureStroke stroke({StrokePoint{10.0, -5.0}, StrokePoint{-3.0, 8.0}, StrokePoint{4.0, 2.0}});
    const GestureBounds b = stroke.bounds();
    EXPECT_DOUBLE_EQ(b.minX, -3.0);
    EXPECT_DOUBLE_EQ(b.maxX, 10.0);
    EXPECT_DOUBLE_EQ(b.minY, -5.0);
    EXPECT_DOUBLE_EQ(b.maxY, 8.0);
    EXPECT_DOUBLE_EQ(b.width(), 13.0);
    EXPECT_DOUBLE_EQ(b.height(), 13.0);
}

TEST(GestureStrokeTest, emptyStrokeHasNoLengthAndInfiniteClosure) {
    GestureStroke stroke;
    EXPECT_TRUE(stroke.empty());
    EXPECT_DOUBLE_EQ(stroke.pathLength(), 0.0);
    EXPECT_FALSE(std::isfinite(stroke.closureDistance()));
}

TEST(GestureStrokeTest, closureDistanceIsBetweenTheEnds) {
    GestureStroke stroke({StrokePoint{0.0, 0.0}, StrokePoint{100.0, 0.0}, StrokePoint{100.0, 10.0}});
    EXPECT_NEAR(stroke.closureDistance(), std::hypot(100.0, 10.0), 1e-9);
    EXPECT_DOUBLE_EQ(stroke.pathLength(), 110.0);
}

TEST(GestureStrokeTest, resamplingIgnoresTheInputSamplingRate) {
    const GestureStroke dense = makeCircle(0.0, 0.0, 100.0, 400);
    const GestureStroke sparse = makeCircle(0.0, 0.0, 100.0, 20);

    const GestureStroke denseResampled = dense.resampled(5.0);
    const GestureStroke sparseResampled = sparse.resampled(5.0);

    // Both must come out at very nearly the same number of samples; that is what makes a
    // recognizer insensitive to what the hardware happened to report. The lengths agree to within
    // the error of approximating a circle by the coarser input polygon.
    EXPECT_NEAR(static_cast<double>(denseResampled.size()), static_cast<double>(sparseResampled.size()), 3.0);
    EXPECT_NEAR(denseResampled.pathLength(), sparseResampled.pathLength(), 5.0);
}

TEST(GestureStrokeTest, translationAndScaleMoveTheShape) {
    const GestureStroke circle = makeCircle(0.0, 0.0, 50.0, 40);
    const GestureStroke moved = circle.translated(25.0, -10.0);
    EXPECT_DOUBLE_EQ(moved.bounds().centerX(), 25.0);
    EXPECT_DOUBLE_EQ(moved.bounds().centerY(), -10.0);

    const GestureStroke bigger = circle.scaled(2.0);
    EXPECT_NEAR(bigger.bounds().width(), 200.0, 1e-6);
    EXPECT_NEAR(bigger.pathLength(), 2.0 * circle.pathLength(), 1e-6);
}

TEST(GestureStrokeTest, timingIsOnlyPresentWhenSamplesCarryIt) {
    GestureStroke without({StrokePoint{0.0, 0.0}, StrokePoint{1.0, 1.0}});
    EXPECT_FALSE(without.hasTiming());

    GestureStroke with({StrokePoint{0.0, 0.0, 0.0}, StrokePoint{1.0, 1.0, 120.0}});
    EXPECT_TRUE(with.hasTiming());
    EXPECT_DOUBLE_EQ(with.duration(), 120.0);
}
