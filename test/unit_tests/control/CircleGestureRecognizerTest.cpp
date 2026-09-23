/*
 * Xournal++
 *
 * Plan 008: circle recognition without a document, a widget or a clock
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "control/gestures/CircleGestureRecognizer.h"

using namespace xoj::gesture;

namespace {
constexpr double PI = 3.14159265358979323846;

auto circleStroke(double cx, double cy, double r, int n, double sweep = 2 * PI, double start = 0.0) -> GestureStroke {
    std::vector<StrokePoint> points;
    const int steps = n;
    for (int i = 0; i <= steps; i++) {
        const double a = start + sweep * static_cast<double>(i) / steps;
        points.push_back(StrokePoint{cx + r * std::cos(a), cy + r * std::sin(a), 0.0});
    }
    return GestureStroke(std::move(points));
}
}  // namespace

TEST(CircleGestureRecognizerTest, recognizesALargeDeliberateCircle) {
    CircleGestureRecognizer recognizer;
    const auto candidate = recognizer.recognize(circleStroke(300.0, 300.0, 120.0, 64));
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->kind, GestureKind::Circle);
    EXPECT_GE(candidate->confidence, 0.5);
    EXPECT_TRUE(candidate->bounds.contains(GestureBounds{300.0, 300.0, 300.0, 300.0}));
}

TEST(CircleGestureRecognizerTest, survivesTheCorpusTransformations) {
    CircleGestureRecognizer recognizer;
    const GestureStroke base = circleStroke(300.0, 300.0, 120.0, 64);

    EXPECT_TRUE(recognizer.recognize(base.translated(400.0, -200.0)).has_value());
    EXPECT_TRUE(recognizer.recognize(base.scaled(1.5)).has_value());
    EXPECT_TRUE(recognizer.recognize(base.resampled(9.0)).has_value());
    // Drawn the other way round is the same gesture.
    EXPECT_TRUE(recognizer.recognize(circleStroke(300.0, 300.0, 120.0, 64, -2 * PI)).has_value());
}

TEST(CircleGestureRecognizerTest, letterSizedLoopsAreNotCircles) {
    CircleGestureRecognizer recognizer;
    // A handwritten O at writing size: closed, round - and not a gesture.
    EXPECT_FALSE(recognizer.recognize(circleStroke(100.0, 100.0, 12.0, 40)).has_value());
    EXPECT_FALSE(recognizer.recognize(circleStroke(100.0, 100.0, 30.0, 40)).has_value());
}

TEST(CircleGestureRecognizerTest, arcsAndSpiralsAreNotCircles) {
    CircleGestureRecognizer recognizer;
    EXPECT_FALSE(recognizer.recognize(circleStroke(300.0, 300.0, 120.0, 64, PI)).has_value());
    EXPECT_FALSE(recognizer.recognize(circleStroke(300.0, 300.0, 120.0, 64, 1.7 * PI)).has_value());

    std::vector<StrokePoint> spiral;
    for (int i = 0; i < 120; i++) {
        const double frac = i / 119.0;
        const double a = 2 * PI * 2.5 * frac;
        const double r = 12.0 + 80.0 * frac;
        spiral.push_back(StrokePoint{300.0 + r * std::cos(a), 300.0 + r * std::sin(a), 0.0});
    }
    EXPECT_FALSE(recognizer.recognize(GestureStroke(std::move(spiral))).has_value());
}

TEST(CircleGestureRecognizerTest, aStraightLineIsNotACircle) {
    CircleGestureRecognizer recognizer;
    std::vector<StrokePoint> line;
    for (int i = 0; i <= 64; i++) {
        line.push_back(StrokePoint{100.0 + 6.0 * i, 100.0 + 6.0 * i, 0.0});
    }
    EXPECT_FALSE(recognizer.recognize(GestureStroke(std::move(line))).has_value());
}

TEST(CircleGestureRecognizerTest, timingIsNotInvented) {
    // The recognizer's timing rule is off by default, and a stroke with no timestamps cannot be
    // judged by it either way.
    CircleGestureRecognizer recognizer;
    EXPECT_TRUE(recognizer.recognize(circleStroke(300.0, 300.0, 120.0, 64)).has_value());

    CircleParameters timing = CircleParameters::defaults();
    timing.useTiming = true;
    CircleGestureRecognizer timed(timing);
    // No timestamps present: still recognised, because there is no clock to consult.
    EXPECT_TRUE(timed.recognize(circleStroke(300.0, 300.0, 120.0, 64)).has_value());
}

TEST(CircleGestureRecognizerTest, aTooQuickFlickFailsWhenTimingIsUsed) {
    CircleParameters params = CircleParameters::defaults();
    params.useTiming = true;
    params.minDurationMs = 200.0;
    CircleGestureRecognizer recognizer(params);

    std::vector<StrokePoint> points;
    for (int i = 0; i <= 64; i++) {
        const double a = 2 * PI * i / 64.0;
        points.push_back(StrokePoint{300.0 + 120.0 * std::cos(a), 300.0 + 120.0 * std::sin(a), i * 1.0});
    }
    EXPECT_FALSE(recognizer.recognize(GestureStroke(std::move(points))).has_value());
}
