/*
 * Xournal++
 *
 * Plan 008: the gesture preferences round-trip and migrate safely
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "control/gestures/GestureSettings.h"

using namespace xoj::gesture;

TEST(GestureSettingsTest, defaultsAreConservative) {
    const GestureSettings settings = GestureSettings::defaults();
    EXPECT_FALSE(settings.circleToSelectEnabled) << "circle-to-select must be off by default";
    EXPECT_FALSE(settings.scribbleToEraseEnabled) << "scribble-to-erase must stay off until its threshold is approved";
    EXPECT_FALSE(settings.tapUndoRedoEnabled) << "tap undo/redo must be off: the tap test did not pass";
    EXPECT_FALSE(settings.quickPaletteEnabled) << "the quick palette is on only once explicitly bound";
    EXPECT_TRUE(settings.feedbackEnabled);
}

TEST(GestureSettingsTest, roundTripsThroughItsStoredForm) {
    GestureSettings settings;
    settings.quickPaletteEnabled = true;
    settings.circleToSelectEnabled = true;
    settings.scribbleToEraseEnabled = false;
    settings.circleConfidenceFloor = 0.75;
    settings.scribbleConfidenceFloor = 0.25;
    settings.feedbackEnabled = false;

    const GestureSettings reloaded = GestureSettings::fromAttributes(settings.toAttributes());
    EXPECT_EQ(reloaded, settings);
}

TEST(GestureSettingsTest, unknownKeysAreIgnored) {
    std::map<std::string, std::string> attributes = GestureSettings::defaults().toAttributes();
    attributes["someFutureSetting"] = "true";

    const GestureSettings settings = GestureSettings::fromAttributes(attributes);
    EXPECT_EQ(settings, GestureSettings::defaults());
}

TEST(GestureSettingsTest, missingKeysFallBackToTheConservativeDefault) {
    // An older profile, written before the floor fields existed.
    std::map<std::string, std::string> attributes{{"circleToSelectEnabled", "true"}};

    const GestureSettings settings = GestureSettings::fromAttributes(attributes);
    EXPECT_TRUE(settings.circleToSelectEnabled);
    EXPECT_DOUBLE_EQ(settings.circleConfidenceFloor, GestureSettings::defaults().circleConfidenceFloor);
    EXPECT_FALSE(settings.scribbleToEraseEnabled);
}

TEST(GestureSettingsTest, garbageNumbersDoNotBecomeGarbageFloors) {
    std::map<std::string, std::string> attributes{{"circleConfidenceFloor", "not-a-number"}};
    const GestureSettings settings = GestureSettings::fromAttributes(attributes);
    EXPECT_DOUBLE_EQ(settings.circleConfidenceFloor, GestureSettings::defaults().circleConfidenceFloor);
}

TEST(GestureSettingsTest, aNewerFormatIsNotHalfRead) {
    // Written by a future version that may have redefined the fields: read nothing of it, and in
    // particular do not let a stale "true" turn a destructive gesture on.
    std::map<std::string, std::string> attributes{
            {"circleToSelectEnabled", "true"}, {"scribbleToEraseEnabled", "true"}, {"quickPaletteEnabled", "true"}};

    const GestureSettings settings = GestureSettings::migrated(GestureSettings::STORAGE_VERSION + 1, attributes);
    EXPECT_EQ(settings, GestureSettings::defaults());
}

TEST(GestureSettingsTest, anOlderFormatMigratesFieldByField) {
    // A profile from before this plan existed: no gesture element at all, which the caller passes
    // as an empty attribute map at version 0.
    const GestureSettings settings = GestureSettings::migrated(0, {});
    EXPECT_EQ(settings, GestureSettings::defaults());
    EXPECT_FALSE(settings.circleToSelectEnabled);
    EXPECT_FALSE(settings.scribbleToEraseEnabled);
}

TEST(GestureSettingsTest, theCurrentFormatIsRead) {
    std::map<std::string, std::string> attributes = GestureSettings::defaults().toAttributes();
    attributes["scribbleToEraseEnabled"] = "true";
    const GestureSettings settings = GestureSettings::migrated(GestureSettings::STORAGE_VERSION, attributes);
    EXPECT_TRUE(settings.scribbleToEraseEnabled);
}

TEST(GestureSettingsTest, aRuntimeToggleDecidesWhetherTheRecognizerMayRun) {
    GestureSettings settings = GestureSettings::defaults();
    EXPECT_FALSE(gestureEnabled(settings, GestureKind::Circle));
    EXPECT_FALSE(gestureEnabled(settings, GestureKind::Scribble));

    // No restart, no reload: the same value the recognizer is asked about.
    settings.circleToSelectEnabled = true;
    EXPECT_TRUE(gestureEnabled(settings, GestureKind::Circle));
    settings.circleToSelectEnabled = false;
    EXPECT_FALSE(gestureEnabled(settings, GestureKind::Circle));
}
