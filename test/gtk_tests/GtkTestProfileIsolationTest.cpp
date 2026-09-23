/*
 * Xournal++
 *
 * Plan 008 review: a GTK case runs against a profile of its own
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication

#include "control/Control.h"                   // for Control
#include "control/gestures/GestureSettings.h"  // for GestureSettings
#include "control/settings/Settings.h"         // for Settings
#include "dialog/GtkTest.h"                    // for GtkTest (the fixture this is about)
#include "gui/GladeSearchpath.h"               // for GladeSearchpath
#include "gui/MainWindow.h"                    // for MainWindow
#include "util/PathUtil.h"                     // for getConfigFile

#include "config-dev.h"  // for SETTINGS_XML_FILE
#include "config-test.h"

namespace fs = std::filesystem;

namespace {

auto readIfExists(const fs::path& path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

/*
 * Plan 008 review finding: the gesture cases that run a real Control were writing the profile the
 * user's desktop reads. A scribble-to-erase case called setGestureSettings(), which saves at once,
 * and the save went to Util::getConfigFile(SETTINGS_XML_FILE) - the real settings.xml. That is a
 * destructive gesture switched on in a live profile by running the tests.
 *
 * This case is the guard on the fix: every GtkTest now runs against its own configuration folder,
 * handed to the application through the fixture before anything is built. It checks the whole path
 * - that the application reads the private folder, that a save lands there, and that the user's own
 * settings file is exactly what it was. If the isolation is removed, the very first assertion fails
 * before a Control exists, so the guard cannot itself write to the profile it is protecting.
 */
class GtkTestProfileIsolationTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ASSERT_EQ(Util::getConfigFile(SETTINGS_XML_FILE), this->testSettingsFile())
                << "the application reads its settings from the case's own configuration folder";
        ASSERT_NE(this->testSettingsFile(), this->userSettingsFile()) << "which is not the user's own settings file";
        EXPECT_TRUE(this->testSettingsFile().string().starts_with(fs::temp_directory_path().string()))
                << "the folder is scratch, so no case writes into the repository either";
        EXPECT_TRUE(this->userProfileUntouched()) << "and the profile is untouched before anything runs";

        GladeSearchpath glade;
        glade.addSearchDirectory(GET_UI_FOLDER);
        glade.addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        auto control = std::make_unique<Control>(G_APPLICATION(app), &glade, true);

        // The save that the finding was about: setGestureSettings() writes the profile immediately.
        ASSERT_FALSE(this->testSettingsFile().empty());
        xoj::gesture::GestureSettings gesture = control->getSettings()->getGestureSettings();
        gesture.scribbleToEraseEnabled = true;
        control->getSettings()->setGestureSettings(gesture);

        const std::string stored = readIfExists(this->testSettingsFile());
        EXPECT_NE(stored.find("scribbleToEraseEnabled=\"true\""), std::string::npos)
                << "the save went to the case's own settings file: " << this->testSettingsFile();
        EXPECT_TRUE(this->userProfileUntouched())
                << "and the user's own settings file is byte for byte what the case started from";
    }
};
TEST_F(GtkTestProfileIsolationTest, aCaseRunsAgainstAProfileOfItsOwn) {}
