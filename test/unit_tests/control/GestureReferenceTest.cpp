/*
 * Xournal++
 *
 * Plan 008 step 7: the gesture reference is generated from the live settings
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/gestures/GestureReference.h"
#include "control/gestures/GestureSettings.h"

using namespace xoj::gesture;

TEST(GestureReferenceTest, listsEveryGestureThisBuildHas) {
    const std::vector<GestureReferenceRow> rows = buildGestureReference(GestureSettings::defaults());
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].gestureId, "circle");
    EXPECT_EQ(rows[1].gestureId, "scribble");
}

TEST(GestureReferenceTest, everyLineSaysSomethingAndSaysWhatTurnsItOff) {
    for (const GestureReferenceRow& row: buildGestureReference(GestureSettings::defaults())) {
        EXPECT_FALSE(row.title.empty()) << row.gestureId;
        EXPECT_FALSE(row.detail.empty()) << row.gestureId;
        EXPECT_FALSE(row.binding.empty()) << row.gestureId;
        EXPECT_FALSE(row.settingId.empty()) << row.gestureId;
    }
}

TEST(GestureReferenceTest, theLinesFollowTheSettingsTheyAreReadFrom) {
    GestureSettings settings = GestureSettings::defaults();
    auto rowOf = [&settings](GestureKind kind) {
        const std::optional<GestureReferenceRow> row = gestureReferenceRow(settings, kind);
        EXPECT_TRUE(row.has_value());
        return *row;
    };

    // The defaults: both off, and the reference says so.
    EXPECT_FALSE(rowOf(GestureKind::Circle).enabled);
    EXPECT_FALSE(rowOf(GestureKind::Scribble).enabled);

    // Turn circle on in the live settings; the reference changes without anything else being told.
    settings.circleToSelectEnabled = true;
    EXPECT_TRUE(rowOf(GestureKind::Circle).enabled);
    EXPECT_FALSE(rowOf(GestureKind::Scribble).enabled) << "enabling one gesture does not enable the other";

    // And back off again.
    settings.circleToSelectEnabled = false;
    EXPECT_FALSE(rowOf(GestureKind::Circle).enabled);
}

TEST(GestureReferenceTest, theDisableHintNamesTheSettingItWouldTurnOff) {
    GestureSettings settings = GestureSettings::defaults();
    settings.scribbleToEraseEnabled = true;
    const std::optional<GestureReferenceRow> row = gestureReferenceRow(settings, GestureKind::Scribble);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->settingId, "scribbleToEraseEnabled");
    EXPECT_TRUE(row->enabled);
}

TEST(GestureReferenceTest, aGestureNobodyHasIsNotInvented) {
    const std::vector<GestureReferenceRow> rows = buildGestureReference(GestureSettings::defaults());
    for (const GestureReferenceRow& row: rows) {
        const std::optional<GestureKind> kind = gestureKindFromId(row.gestureId);
        EXPECT_TRUE(kind.has_value()) << row.gestureId << " names no gesture this build knows";
    }
}
