/*
 * Xournal++
 *
 * Plan 008: the decision layer - nothing happens without a setting, coverage and an undo group
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/gestures/GesturePolicy.h"

using namespace xoj::gesture;

namespace {
GesturePolicyInput circleInput(double confidence, bool overlaps = true, bool undoGroup = true) {
    GesturePolicyInput input;
    input.kind = GestureKind::Circle;
    input.confidence = confidence;
    input.overlapsExistingInk = overlaps;
    input.undoGroupAvailable = undoGroup;
    return input;
}
}  // namespace

TEST(GesturePolicyTest, aDisabledGestureLeavesOrdinaryInkAlone) {
    const GestureSettings settings = GestureSettings::defaults();  // circle off
    const GesturePolicyResult result = GesturePolicy::evaluate(settings, circleInput(0.99));
    EXPECT_EQ(result.decision, GestureDecision::Ignore);
    EXPECT_EQ(result.reason, GestureIgnoreReason::Disabled);
}

TEST(GesturePolicyTest, aConfidentEnabledGestureIsOnlyEverPreviewedFirst) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;

    const GesturePolicyResult result = GesturePolicy::evaluate(settings, circleInput(0.9));
    EXPECT_EQ(result.decision, GestureDecision::Preview);
}

TEST(GesturePolicyTest, theUsersFloorCanRaiseTheBarTheRecognizerSet) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;
    settings.circleConfidenceFloor = 0.95;

    EXPECT_EQ(GesturePolicy::evaluate(settings, circleInput(0.6)).reason, GestureIgnoreReason::ConfidenceTooLow);
    EXPECT_EQ(GesturePolicy::evaluate(settings, circleInput(0.98)).decision, GestureDecision::Preview);
}

TEST(GesturePolicyTest, aGestureThatCoveredNothingDoesNothing) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;

    const GesturePolicyResult result = GesturePolicy::evaluate(settings, circleInput(0.9, /*overlaps*/ false));
    EXPECT_EQ(result.decision, GestureDecision::Ignore);
    EXPECT_EQ(result.reason, GestureIgnoreReason::NoOverlap);
}

TEST(GesturePolicyTest, noActionWithoutAnUndoRecord) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;
    settings.scribbleToEraseEnabled = true;

    GesturePolicyInput scribble;
    scribble.kind = GestureKind::Scribble;
    scribble.confidence = 0.9;
    scribble.overlapsExistingInk = true;
    scribble.undoGroupAvailable = false;

    const GesturePolicyResult result = GesturePolicy::evaluate(settings, scribble);
    EXPECT_EQ(result.decision, GestureDecision::Ignore);
    EXPECT_EQ(result.reason, GestureIgnoreReason::NoUndoGroup);
    // And confirming cannot route around it.
    EXPECT_EQ(GesturePolicy::confirm(settings, scribble).decision, GestureDecision::Ignore);
}

TEST(GesturePolicyTest, confirmationOnlyFollowsAPreview) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;

    const GesturePolicyResult confirmed = GesturePolicy::confirm(settings, circleInput(0.9));
    EXPECT_EQ(confirmed.decision, GestureDecision::Confirm);

    settings.circleToSelectEnabled = false;
    EXPECT_EQ(GesturePolicy::confirm(settings, circleInput(0.9)).decision, GestureDecision::Ignore);
}

TEST(GesturePolicyTest, theScribbleGestureIsOffUntilItsSettingSaysOtherwise) {
    GestureSettings settings = GestureSettings::defaults();
    GesturePolicyInput scribble;
    scribble.kind = GestureKind::Scribble;
    scribble.confidence = 1.0;
    scribble.overlapsExistingInk = true;
    scribble.undoGroupAvailable = true;

    EXPECT_EQ(GesturePolicy::confirm(settings, scribble).decision, GestureDecision::Ignore);
    settings.scribbleToEraseEnabled = true;
    EXPECT_EQ(GesturePolicy::confirm(settings, scribble).decision, GestureDecision::Confirm);
}

TEST(GesturePolicyTest, everyIgnoreSaysWhy) {
    for (GestureIgnoreReason reason: {GestureIgnoreReason::Disabled, GestureIgnoreReason::ConfidenceTooLow,
                                      GestureIgnoreReason::NoOverlap, GestureIgnoreReason::NoUndoGroup}) {
        EXPECT_STRNE(gestureIgnoreReasonText(reason), "");
    }
    EXPECT_STREQ(gestureIgnoreReasonText(GestureIgnoreReason::None), "");
}
