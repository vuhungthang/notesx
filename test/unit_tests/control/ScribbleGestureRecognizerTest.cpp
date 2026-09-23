/*
 * Xournal++
 *
 * Plan 008: scribble recognition without a document or a widget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <vector>

#include <gtest/gtest.h>

#include "control/gestures/ScribbleGestureRecognizer.h"

using namespace xoj::gesture;

namespace {
auto scrub(double cx, double cy, double width, double height, int passes, int perPass = 24) -> GestureStroke {
    std::vector<StrokePoint> points;
    for (int p = 0; p < passes; p++) {
        const double y = cy - height / 2 + height * p / (passes - 1);
        const double x0 = (p % 2 == 0) ? cx - width / 2 : cx + width / 2;
        const double x1 = (p % 2 == 0) ? cx + width / 2 : cx - width / 2;
        for (int i = 0; i < perPass; i++) {
            points.push_back(StrokePoint{x0 + (x1 - x0) * i / (perPass - 1), y, 0.0});
        }
    }
    return GestureStroke(std::move(points));
}

auto shading() -> GestureStroke {
    std::vector<StrokePoint> points;
    for (int row = 0; row < 6; row++) {
        const double y = 200.0 + 20.0 * row;
        const double x0 = (row % 2 == 0) ? 200.0 : 500.0;
        const double x1 = (row % 2 == 0) ? 500.0 : 200.0;
        for (int i = 0; i < 30; i++) {
            points.push_back(StrokePoint{x0 + (x1 - x0) * i / 29.0, y, 0.0});
        }
    }
    return GestureStroke(std::move(points));
}
}  // namespace

TEST(ScribbleGestureRecognizerTest, recognizesADenseLocalScrub) {
    ScribbleGestureRecognizer recognizer;
    const auto candidate = recognizer.recognize(scrub(300.0, 300.0, 100.0, 96.0, 16));
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->kind, GestureKind::Scribble);
    EXPECT_GE(candidate->confidence, 0.5);
}

TEST(ScribbleGestureRecognizerTest, survivesTheCorpusTransformations) {
    ScribbleGestureRecognizer recognizer;
    const GestureStroke base = scrub(300.0, 300.0, 100.0, 96.0, 16);
    EXPECT_TRUE(recognizer.recognize(base.translated(500.0, 300.0)).has_value());
    EXPECT_TRUE(recognizer.recognize(scrub(300.0, 300.0, 80.0, 76.0, 15, 28)).has_value());
}

TEST(ScribbleGestureRecognizerTest, shadingIsNotAScribble) {
    ScribbleGestureRecognizer recognizer;
    EXPECT_FALSE(recognizer.recognize(shading()).has_value());
}

TEST(ScribbleGestureRecognizerTest, aSingleCrossOutIsNotAScribble) {
    ScribbleGestureRecognizer recognizer;
    std::vector<StrokePoint> cross;
    for (int i = 0; i < 30; i++) {
        cross.push_back(StrokePoint{180.0 + 80.0 * i / 29.0, 180.0 + 80.0 * i / 29.0, 0.0});
    }
    for (int i = 0; i < 30; i++) {
        cross.push_back(StrokePoint{260.0 - 80.0 * i / 29.0, 180.0 + 80.0 * i / 29.0, 0.0});
    }
    EXPECT_FALSE(recognizer.recognize(GestureStroke(std::move(cross))).has_value());
}

TEST(ScribbleGestureRecognizerTest, aShortFlickIsNotAScribble) {
    ScribbleGestureRecognizer recognizer;
    std::vector<StrokePoint> flick{{200.0, 200.0}, {230.0, 205.0}, {205.0, 210.0}, {235.0, 214.0}};
    EXPECT_FALSE(recognizer.recognize(GestureStroke(std::move(flick))).has_value());
}

TEST(ScribbleGestureRecognizerTest, aHugeSweepIsNotALocalScrub) {
    ScribbleParameters params = ScribbleParameters::defaults();
    params.maxDiagonal = 100.0;
    ScribbleGestureRecognizer recognizer(params);
    EXPECT_FALSE(recognizer.recognize(scrub(300.0, 300.0, 100.0, 96.0, 16)).has_value());
}
