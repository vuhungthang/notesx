/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "control/settings/Settings.h"

TEST(SettingsTest, testLoadDoesNotThrowForNonExistingFilePath) {
    Settings settings{"non-existing-file-path"};
    EXPECT_NO_THROW(settings.load());
}

// Rudimentary test for Settings save/load - very crude
TEST(SettingsTest, testReadWrite) {
    auto saveReloadTest = [&](const fs::path& dir) {
        std::cout << "Test saving in " << dir << std::endl;
        const fs::path outPath = dir / "xournalpp-test-units_Settings_testReadWrite.xml";
        if (fs::exists(outPath)) {
            std::cout << "Removing file (already exist): " << dir << std::endl;
            fs::remove(outPath);
        };

        Settings settings(outPath);
        settings.transactionStart();
        settings.setAudioDisabled(true);               // bool
        settings.setDefaultSaveName(u8"foo/bar€_%H");  // u8string
        settings.setPreferredLocale("es");             // string
        PageTemplateSettings tp;
        tp.parse("xoj/"
                 "template\ncopyLastPageSettings=false\ncopyLastPageSize=true\nsize=5.123x8.764\nbackgroundType="
                 "cµßtom\nbackgroundTypeConfig=m1=3,®ændomString=↓↓↓\nbackgroundColor=#abcdef\n");
        settings.setPageTemplateSettings(tp);                          // string
        settings.setDisplayDpi(123);                                   // int
        settings.setStabilizerDrag(3.1415);                            // double
        settings.setBackgroundColor(Color(123, 45, 67));               // Color
        settings.setColorPaletteSetting("foo/bar€_palette");           // path
        settings.setEraserVisibility(ERASER_VISIBILITY_HOVER);         // enum
        settings.setFont(XojFont{"myfontname italic 34"});             // Font
        settings.latexSettings.editorFont = XojFont{"myfonttest 52"};  // Font
        settings.setPreloadPagesAfter(145);                            // unsigned int
        settings.transactionEnd();                                     // calls save()

        Settings loaded(outPath);
        loaded.load();

        // For each type, we test one that has been changed and one that should be default
        EXPECT_EQ(settings.isAudioDisabled(), loaded.isAudioDisabled());                                    // bool
        EXPECT_EQ(settings.isAutoloadPdfXoj(), loaded.isAutoloadPdfXoj());                                  // bool
        EXPECT_EQ(settings.getDefaultSaveName(), loaded.getDefaultSaveName());                              // u8string
        EXPECT_EQ(settings.getDefaultPdfExportName(), loaded.getDefaultPdfExportName());                    // u8string
        EXPECT_EQ(settings.getPreferredLocale(), loaded.getPreferredLocale());                              // string
        EXPECT_EQ(settings.getPageTemplateSettings(), loaded.getPageTemplateSettings());                    // string
        EXPECT_EQ(settings.getDisplayDpi(), loaded.getDisplayDpi());                                        // int
        EXPECT_EQ(settings.getAddHorizontalSpaceAmountLeft(), loaded.getAddHorizontalSpaceAmountLeft());    // int
        EXPECT_EQ(settings.getStabilizerDrag(), loaded.getStabilizerDrag());                                // double
        EXPECT_EQ(settings.getCursorHighlightBorderWidth(), loaded.getCursorHighlightBorderWidth());        // double
        EXPECT_EQ(settings.getBackgroundColor(), loaded.getBackgroundColor());                              // Color
        EXPECT_EQ(settings.getActiveSelectionColor(), loaded.getActiveSelectionColor());                    // Color
        EXPECT_EQ(settings.getColorPaletteSetting(), loaded.getColorPaletteSetting());                      // path
        EXPECT_EQ(settings.getLastOpenPath(), loaded.getLastOpenPath());                                    // path
        EXPECT_EQ(settings.getEraserVisibility(), loaded.getEraserVisibility());                            // enum
        EXPECT_EQ(settings.getActiveViewMode(), loaded.getActiveViewMode());                                // enum
        EXPECT_EQ(settings.getFont().getName(), loaded.getFont().getName());                                // Font
        EXPECT_EQ(settings.getFont().getSize(), loaded.getFont().getSize());                                // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getName(), loaded.latexSettings.editorFont.getName());  // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getSize(), loaded.latexSettings.editorFont.getSize());  // Font
        EXPECT_EQ(settings.getPreloadPagesAfter(), loaded.getPreloadPagesAfter());    // unsigned int
        EXPECT_EQ(settings.getPreloadPagesBefore(), loaded.getPreloadPagesBefore());  // unsigned int

        fs::remove(outPath);
    };
    saveReloadTest(fs::temp_directory_path());
}

/*
 * Plan 001: Lucide is the default icon theme for new profiles only. An existing
 * saved preference must survive, and an unrecognised value must keep following
 * the pre-existing fallback rather than crashing.
 */

namespace {

/** Write a minimal settings file containing only the given <property> elements. */
void writeSettingsFile(const fs::path& path, const std::string& properties) {
    std::ofstream out(path);
    out << "<?xml version=\"1.0\"?>\n<settings>\n" << properties << "</settings>\n";
}

/** A fresh, empty directory to hold one test's settings file. */
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

}  // namespace

TEST(SettingsTest, testIconThemeDefaultsToLucideForNewProfiles) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_iconThemeFresh");
    Settings settings{dir / "settings.xml"};

    // Defaults are applied by the constructor, before any file is read.
    EXPECT_EQ(settings.getIconTheme(), ICON_THEME_LUCIDE);

    fs::remove_all(dir);
}

TEST(SettingsTest, testIconThemeDefaultIsUsedWhenTheSettingsFileIsRegenerated) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_iconThemeDefault");
    const fs::path file = dir / "settings.xml";

    // load() regenerates a settings file when none exists yet; the regenerated
    // profile must carry the new default.
    Settings settings{file};
    settings.load();
    EXPECT_EQ(settings.getIconTheme(), ICON_THEME_LUCIDE);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getIconTheme(), ICON_THEME_LUCIDE);

    fs::remove_all(dir);
}

TEST(SettingsTest, testPersistedColorIconThemeIsPreserved) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_iconThemeColor");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, "<property name=\"iconTheme\" value=\"iconsColor\"/>\n");

    Settings settings{file};
    settings.load();

    EXPECT_EQ(settings.getIconTheme(), ICON_THEME_COLOR);

    fs::remove_all(dir);
}

TEST(SettingsTest, testUnknownPersistedIconThemeFallsBackWithoutCrashing) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_iconThemeUnknown");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, "<property name=\"iconTheme\" value=\"iconsNoSuchTheme\"/>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());

    // Pre-existing fallback behaviour: an unrecognised value maps to Color.
    EXPECT_EQ(settings.getIconTheme(), ICON_THEME_COLOR);

    // Saving rewrites the resolved value, so the unrecognised string does not survive.
    settings.transactionStart();
    settings.transactionEnd();  // calls save()

    const std::string contents = readFile(file);
    EXPECT_EQ(contents.find("iconsNoSuchTheme"), std::string::npos);
    EXPECT_NE(contents.find("iconsColor"), std::string::npos);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getIconTheme(), ICON_THEME_COLOR);

    fs::remove_all(dir);
}

/*
 * Plan 002: Focus and Classic workspaces.
 *
 * A fresh profile starts in Focus. A settings file that predates workspaces keeps the
 * established layout (Classic), so updating never silently changes it. Each workspace
 * remembers its own toolbar and menubar preference.
 */

namespace {

/** A settings file as written before Plan 002: no workspace keys at all. */
constexpr auto PRE_PLAN_002_SETTINGS = "<property name=\"iconTheme\" value=\"iconsColor\"/>\n"
                                       "<property name=\"selectedToolbar\" value=\"Minimal Left\"/>\n"
                                       "<property name=\"menubarVisible\" value=\"true\"/>\n";

}  // namespace

TEST(SettingsTest, testFreshProfileStartsInTheFocusWorkspace) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceFresh");

    Settings settings{dir / "settings.xml"};
    settings.load();

    EXPECT_EQ(settings.getWorkspaceMode(), WorkspaceMode::FOCUS);
    EXPECT_EQ(settings.getSelectedToolbar(), "Focus");
    EXPECT_FALSE(settings.isMenubarVisible());

    fs::remove_all(dir);
}

TEST(SettingsTest, testFreshProfileWorkspaceIsPersisted) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceFreshPersisted");
    const fs::path file = dir / "settings.xml";

    Settings settings{file};
    settings.load();

    const std::string contents = readFile(file);
    EXPECT_NE(contents.find("<property name=\"workspaceMode\" value=\"focus\"/>"), std::string::npos);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getWorkspaceMode(), WorkspaceMode::FOCUS);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "Focus");

    fs::remove_all(dir);
}

TEST(SettingsTest, testProfileWithoutWorkspaceFieldMigratesToClassic) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceMigration");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, PRE_PLAN_002_SETTINGS);

    Settings settings{file};
    settings.load();

    // The established profile keeps its toolbar and its menubar.
    EXPECT_EQ(settings.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_EQ(settings.getSelectedToolbar(), "Minimal Left");
    EXPECT_TRUE(settings.isMenubarVisible());

    // The migration is written out, so the next launch stays in Classic.
    settings.transactionStart();
    settings.transactionEnd();  // calls save()

    const std::string contents = readFile(file);
    EXPECT_NE(contents.find("<property name=\"workspaceMode\" value=\"classic\"/>"), std::string::npos);
    EXPECT_NE(contents.find("<property name=\"classicToolbar\" value=\"Minimal Left\"/>"), std::string::npos);
    EXPECT_NE(contents.find("<property name=\"focusToolbar\" value=\"Focus\"/>"), std::string::npos);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "Minimal Left");
    EXPECT_TRUE(reloaded.isMenubarVisible());

    fs::remove_all(dir);
}

TEST(SettingsTest, testWorkspaceRoundTripsFocusAndClassic) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceRoundTrip");
    const fs::path file = dir / "settings.xml";

    {
        Settings settings{file};
        settings.load();
        EXPECT_EQ(settings.getWorkspaceMode(), WorkspaceMode::FOCUS);

        settings.setWorkspaceMode(WorkspaceMode::CLASSIC);
        EXPECT_EQ(settings.getSelectedToolbar(), "Portrait");
        settings.setSelectedToolbar("All in");
    }

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "All in");

    reloaded.setWorkspaceMode(WorkspaceMode::FOCUS);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "Focus");
    reloaded.setWorkspaceMode(WorkspaceMode::CLASSIC);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "All in");

    fs::remove_all(dir);
}

TEST(SettingsTest, testSwitchingWorkspacesKeepsEachToolbarSelection) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceToolbars");
    const fs::path file = dir / "settings.xml";

    Settings settings{file};
    settings.load();

    settings.setSelectedToolbar("Minimal Top");  // chosen while in Focus
    settings.setWorkspaceMode(WorkspaceMode::CLASSIC);
    EXPECT_EQ(settings.getSelectedToolbar(), "Portrait");  // Classic's own default, untouched

    settings.setSelectedToolbar("Tablet mode");
    settings.setWorkspaceMode(WorkspaceMode::FOCUS);
    EXPECT_EQ(settings.getSelectedToolbar(), "Minimal Top");

    settings.setWorkspaceMode(WorkspaceMode::CLASSIC);
    EXPECT_EQ(settings.getSelectedToolbar(), "Tablet mode");

    // Both selections survive an application restart.
    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "Tablet mode");

    reloaded.setWorkspaceMode(WorkspaceMode::FOCUS);
    EXPECT_EQ(reloaded.getSelectedToolbar(), "Minimal Top");

    fs::remove_all(dir);
}

TEST(SettingsTest, testMenubarPreferenceIsKeptPerWorkspace) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceMenubar");
    const fs::path file = dir / "settings.xml";

    Settings settings{file};
    settings.load();
    EXPECT_FALSE(settings.isMenubarVisible());  // Focus hides the traditional menubar

    settings.setWorkspaceMode(WorkspaceMode::CLASSIC);
    EXPECT_TRUE(settings.isMenubarVisible());  // Classic keeps the pre-existing default

    settings.setMenubarVisible(false);  // the user hides it in Classic
    settings.setWorkspaceMode(WorkspaceMode::FOCUS);
    EXPECT_FALSE(settings.isMenubarVisible());

    settings.setMenubarVisible(true);  // the user shows it in Focus (F10)
    settings.setWorkspaceMode(WorkspaceMode::CLASSIC);
    EXPECT_FALSE(settings.isMenubarVisible());  // Classic's preference is not overwritten

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_FALSE(reloaded.isMenubarVisible());

    fs::remove_all(dir);
}

TEST(SettingsTest, testUnknownWorkspaceModeFallsBackToClassic) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_workspaceUnknown");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, "<property name=\"selectedToolbar\" value=\"Portrait\"/>\n"
                            "<property name=\"workspaceMode\" value=\"lecture\"/>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());

    // An unknown mode was not written by this version: never silently drop an
    // established user into Focus.
    EXPECT_EQ(settings.getWorkspaceMode(), WorkspaceMode::CLASSIC);
    EXPECT_EQ(settings.getSelectedToolbar(), "Portrait");

    fs::remove_all(dir);
}

