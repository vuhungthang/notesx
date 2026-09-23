/*
 * Xournal++
 *
 * Plan 008: the gesture preferences survive the settings file, and migrate safely
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "control/gestures/GestureSettings.h"
#include "control/settings/Settings.h"

using namespace xoj::gesture;
namespace fs = std::filesystem;

namespace {

auto freshSettingsDir(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

auto readFile(const fs::path& path) -> std::string {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

/// Remove the <gestureSettings .../> element from a settings file, as an older profile would be.
auto withoutGestureElement(std::string xml) -> std::string {
    const std::string marker = "<gestureSettings";
    const std::size_t begin = xml.find(marker);
    if (begin == std::string::npos) {
        return xml;
    }
    const std::size_t end = xml.find("/>", begin);
    if (end == std::string::npos) {
        return xml;
    }
    xml.erase(begin, end + 2 - begin);
    return xml;
}

}  // namespace

TEST(GestureSettingsPersistenceTest, aFreshProfileIsConservative) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_gestureFresh");
    Settings settings(dir / "settings.xml");
    settings.load();

    const GestureSettings& gestures = settings.getGestureSettings();
    EXPECT_FALSE(gestures.circleToSelectEnabled);
    EXPECT_FALSE(gestures.scribbleToEraseEnabled);
    EXPECT_FALSE(gestures.tapUndoRedoEnabled);
}

TEST(GestureSettingsPersistenceTest, theSettingsRoundTripThroughTheFile) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_gestureRoundTrip");
    const fs::path file = dir / "settings.xml";

    {
        Settings settings(file);
        settings.transactionStart();
        GestureSettings gestures = GestureSettings::defaults();
        gestures.circleToSelectEnabled = true;
        gestures.circleConfidenceFloor = 0.8;
        gestures.scribbleConfidenceFloor = 0.3;
        gestures.feedbackEnabled = false;
        settings.setGestureSettings(gestures);
        settings.transactionEnd();
    }

    Settings reloaded(file);
    reloaded.load();
    const GestureSettings& gestures = reloaded.getGestureSettings();
    EXPECT_TRUE(gestures.circleToSelectEnabled);
    EXPECT_FALSE(gestures.scribbleToEraseEnabled);
    EXPECT_DOUBLE_EQ(gestures.circleConfidenceFloor, 0.8);
    EXPECT_DOUBLE_EQ(gestures.scribbleConfidenceFloor, 0.3);
    EXPECT_FALSE(gestures.feedbackEnabled);
}

TEST(GestureSettingsPersistenceTest, anOlderProfileWithoutTheElementGetsTheDefaults) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_gestureMigration");
    const fs::path file = dir / "settings.xml";

    // A profile written before this plan: no gesture element anywhere in it.
    {
        Settings settings(file);
        settings.transactionStart();
        settings.transactionEnd();
    }
    writeFile(file, withoutGestureElement(readFile(file)));

    Settings migrated(file);
    migrated.load();
    const GestureSettings& gestures = migrated.getGestureSettings();
    EXPECT_EQ(gestures, GestureSettings::defaults());
    EXPECT_FALSE(gestures.circleToSelectEnabled);
    EXPECT_FALSE(gestures.scribbleToEraseEnabled);
}

TEST(GestureSettingsPersistenceTest, aFileWrittenByANewerVersionDoesNotTurnAnythingOn) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_gestureNewerFormat");
    const fs::path file = dir / "settings.xml";

    {
        Settings settings(file);
        settings.transactionStart();
        settings.transactionEnd();
    }

    // Claim a version this build does not understand, with a dangerous setting apparently on.
    std::string xml = readFile(file);
    xml = withoutGestureElement(xml);
    const std::string element = "<gestureSettings version=\"99\" circleToSelectEnabled=\"true\" "
                                "scribbleToEraseEnabled=\"true\"/>";
    const std::size_t rootEnd = xml.rfind("</settings>");
    ASSERT_NE(rootEnd, std::string::npos);
    xml.insert(rootEnd, element + "\n");
    writeFile(file, xml);

    Settings settings(file);
    settings.load();
    EXPECT_EQ(settings.getGestureSettings(), GestureSettings::defaults());
}

TEST(GestureSettingsPersistenceTest, settingTheGesturePreferencesTakesEffectWithoutARestart) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_gestureRuntime");
    Settings settings(dir / "settings.xml");
    settings.load();

    EXPECT_FALSE(gestureEnabled(settings.getGestureSettings(), GestureKind::Circle));

    GestureSettings gestures = settings.getGestureSettings();
    gestures.circleToSelectEnabled = true;
    settings.setGestureSettings(gestures);

    // The recognizer's question is answered by the value in the live Settings object.
    EXPECT_TRUE(gestureEnabled(settings.getGestureSettings(), GestureKind::Circle));
}
