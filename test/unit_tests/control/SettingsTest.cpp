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

#include <algorithm>
#include <cstdint>
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

/*
 * Plan 006: the dashboard's pinned files and watched folders.
 *
 * What has to survive a restart is exactly the paths the user chose. The dashboard is an index over
 * files the user owns, so saving these lists must not create, move or touch anything - a pinned file
 * that is gone is still a pin the user can see and remove.
 */

namespace {

/// A document to pin, so a test can prove that pinning it changes nothing about the file.
auto writeNote(const fs::path& path) -> fs::path {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "PK\x03\x04not really a document\n";
    out.close();
    return path;
}

auto entryCount(const fs::path& folder) -> std::size_t {
    std::size_t count = 0;
    for (const auto& entry: fs::directory_iterator(folder)) {
        (void)entry;
        count++;
    }
    return count;
}

}  // namespace

TEST(SettingsTest, testDashboardPinsAndFoldersRoundTrip) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardSettings");
    const fs::path file = dir / "settings.xml";
    const fs::path unicodeNote = dir / fs::path(std::u8string(u8"a\u03c9kward \u24abath.xopp"));

    {
        Settings settings{file};
        settings.load();
        ASSERT_TRUE(settings.getDashboardPinnedFiles().empty());
        ASSERT_TRUE(settings.getDashboardFolders().empty());

        ASSERT_TRUE(settings.addDashboardFolder(dir / "notes", false));
        ASSERT_TRUE(settings.addDashboardFolder(dir / "books", true));
        EXPECT_FALSE(settings.addDashboardFolder(dir / "notes", false)) << "a folder is listed once";

        ASSERT_TRUE(settings.pinDashboardFile(dir / "a.xopp"));
        ASSERT_TRUE(settings.pinDashboardFile(dir / "b.xopp"));
        ASSERT_TRUE(settings.pinDashboardFile(unicodeNote));
        EXPECT_FALSE(settings.pinDashboardFile(dir / "a.xopp")) << "a file is pinned once";
    }

    Settings reloaded{file};
    reloaded.load();

    const std::vector<fs::path> pinned = reloaded.getDashboardPinnedFiles();
    ASSERT_EQ(pinned.size(), 3U);
    EXPECT_EQ(pinned[0], dir / "a.xopp") << "the pins keep the order the user made them in";
    EXPECT_EQ(pinned[1], dir / "b.xopp");
    EXPECT_EQ(pinned[2], unicodeNote) << "a path is stored as it is, whatever it contains";

    const std::vector<DashboardFolder>& folders = reloaded.getDashboardFolders();
    ASSERT_EQ(folders.size(), 2U);
    EXPECT_EQ(folders[0].path, (dir / "notes").string());
    EXPECT_FALSE(folders[0].recursive) << "a folder is shallow unless the user asks for its subtree";
    EXPECT_EQ(folders[1].path, (dir / "books").string());
    EXPECT_TRUE(folders[1].recursive);

    const std::string contents = readFile(file);
    EXPECT_NE(contents.find("<dashboard"), std::string::npos);
    EXPECT_NE(contents.find("recursive=\"true\""), std::string::npos);

    // Unpinning and unlisting are the other side of the same list.
    EXPECT_TRUE(reloaded.unpinDashboardFile(dir / "a.xopp"));
    EXPECT_FALSE(reloaded.unpinDashboardFile(dir / "a.xopp"));
    EXPECT_TRUE(reloaded.removeDashboardFolder(dir / "notes"));
    EXPECT_FALSE(reloaded.removeDashboardFolder(dir / "notes"));

    Settings again{file};
    again.load();
    EXPECT_EQ(again.getDashboardPinnedFiles().size(), 2U);
    ASSERT_EQ(again.getDashboardFolders().size(), 1U);
    EXPECT_EQ(again.getDashboardFolders()[0].path, (dir / "books").string());

    fs::remove_all(dir);
}

TEST(SettingsTest, testDashboardListsAreDeduplicatedWhicheverWayTheyAreSet) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardDedupe");
    Settings settings{dir / "settings.xml"};
    settings.load();

    settings.setDashboardPinnedFiles({dir / "a.xopp", dir / "a.xopp", dir / "b.xopp"});
    EXPECT_EQ(settings.getDashboardPinnedFiles().size(), 2U);

    settings.setDashboardFolders(
            {DashboardFolder{(dir / "notes").string(), false}, DashboardFolder{(dir / "notes").string(), true}});
    EXPECT_EQ(settings.getDashboardFolders().size(), 2U) << "the setter stores what it is given";

    fs::remove_all(dir);
}

TEST(SettingsTest, testDashboardPathsPointAtFilesWithoutTouchingThem) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardUntouched");
    const fs::path note = writeNote(dir / "notes.xopp");
    const fs::path absent = dir / "not-there.xopp";
    const auto modified = fs::last_write_time(note);
    const auto size = fs::file_size(note);

    {
        Settings settings{dir / "settings.xml"};
        settings.load();
        settings.pinDashboardFile(note);
        settings.pinDashboardFile(absent);
        ASSERT_TRUE(settings.addDashboardFolder(dir, false));
    }

    Settings reloaded{dir / "settings.xml"};
    reloaded.load();
    EXPECT_EQ(reloaded.getDashboardPinnedFiles().size(), 2U);
    ASSERT_EQ(reloaded.getDashboardFolders().size(), 1U);

    // The dashboard lists what is there; it never makes something be there, and it never changes
    // what it lists.
    EXPECT_TRUE(fs::exists(note));
    EXPECT_FALSE(fs::exists(absent)) << "a listed path that is gone is not created";
    EXPECT_EQ(fs::last_write_time(note), modified);
    EXPECT_EQ(fs::file_size(note), size);
    EXPECT_EQ(entryCount(dir), 2U) << "the folder gained the settings file and nothing else";

    // The same holds for removing the folder: only the settings file changes.
    EXPECT_TRUE(reloaded.removeDashboardFolder(dir));
    EXPECT_TRUE(fs::exists(note));
    EXPECT_EQ(entryCount(dir), 2U);

    fs::remove_all(dir);
}

TEST(SettingsTest, testProfileFromBeforeTheDashboardLoadsWithoutOne) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardOldProfile");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, "<property name=\"iconTheme\" value=\"iconsColor\"/>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());
    EXPECT_TRUE(settings.getDashboardPinnedFiles().empty());
    EXPECT_TRUE(settings.getDashboardFolders().empty());

    // Saving writes the element, so the next launch has one to read.
    settings.transactionStart();
    settings.transactionEnd();
    EXPECT_NE(readFile(file).find("<dashboard"), std::string::npos);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_TRUE(reloaded.getDashboardPinnedFiles().empty());

    fs::remove_all(dir);
}

TEST(SettingsTest, testADashboardEntryWithoutAPathIsSkipped) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardBadEntry");
    const fs::path file = dir / "settings.xml";
    writeSettingsFile(file, "<dashboard>\n"
                            "  <pinned path=\"" +
                                    (dir / "kept.xopp").string() +
                                    "\"/>\n"
                                    "  <pinned/>\n"
                                    "  <folder recursive=\"true\"/>\n"
                                    "  <nonsense path=\"x\"/>\n"
                                    "</dashboard>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());

    // A malformed entry is skipped, and the good ones around it are still read.
    ASSERT_EQ(settings.getDashboardPinnedFiles().size(), 1U);
    EXPECT_EQ(settings.getDashboardPinnedFiles()[0], dir / "kept.xopp");
    EXPECT_TRUE(settings.getDashboardFolders().empty());

    fs::remove_all(dir);
}

/*
 * Plan 006: listing a folder's subtree is a setting of its own, off until the user asks for it.
 * What changes is what the dashboard walks; the folder is never touched.
 */
TEST(SettingsTest, testDashboardFolderRecursionIsASettingOfItsOwn) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardRecursion");
    const fs::path file = dir / "settings.xml";
    const fs::path folder = dir / "notes";

    Settings settings{file};
    settings.load();
    ASSERT_TRUE(settings.addDashboardFolder(folder, false));
    ASSERT_EQ(settings.getDashboardFolders().size(), 1U);
    EXPECT_FALSE(settings.getDashboardFolders().front().recursive) << "deep listing is off until the user asks for it";

    // Asking for it is a change; asking again is not.
    EXPECT_TRUE(settings.setDashboardFolderRecursive(folder, true));
    EXPECT_FALSE(settings.setDashboardFolderRecursive(folder, true));
    EXPECT_FALSE(settings.setDashboardFolderRecursive(dir / "not-listed", true));

    Settings reloaded{file};
    reloaded.load();
    ASSERT_EQ(reloaded.getDashboardFolders().size(), 1U);
    EXPECT_TRUE(reloaded.getDashboardFolders().front().recursive);

    EXPECT_TRUE(reloaded.setDashboardFolderRecursive(folder, false));
    Settings reloadedAgain{file};
    reloadedAgain.load();
    EXPECT_FALSE(reloadedAgain.getDashboardFolders().front().recursive);

    fs::remove_all(dir);
}

/*
 * Plan 006, step 5 and its verification: "adding/removing folders changes only settings; no user
 * file timestamps or locations change".
 *
 * The files are real files in a temporary folder, and what is compared is what the filesystem says
 * about them before and after every dashboard action there is: the note is where it was, with what
 * it held, last written at the same moment, and the folder holds exactly what it held.
 */
TEST(SettingsTest, testPinningAndFoldersChangeTheSettingsAndNothingElse) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_dashboardTouchNothing");
    const fs::path file = dir / "settings.xml";
    const fs::path folder = dir / "notes";
    const fs::path note = folder / "note.xopp";
    const fs::path other = folder / "other.xopp";
    fs::create_directories(folder);
    {
        std::ofstream(note, std::ios::binary | std::ios::trunc) << "the note";
        std::ofstream(other, std::ios::binary | std::ios::trunc) << "another note";
    }

    const fs::file_time_type noteTime = fs::last_write_time(note);
    const fs::file_time_type otherTime = fs::last_write_time(other);
    const std::uintmax_t noteSize = fs::file_size(note);
    const fs::file_time_type folderTime = fs::last_write_time(folder);

    Settings settings{file};
    settings.load();

    EXPECT_TRUE(settings.pinDashboardFile(note));
    EXPECT_TRUE(settings.addDashboardFolder(folder, false));
    EXPECT_FALSE(settings.addDashboardFolder(folder, false)) << "the same folder is not listed twice";
    EXPECT_TRUE(settings.setDashboardFolderRecursive(folder, true));
    EXPECT_TRUE(settings.setDashboardFolderRecursive(folder, false)) << "and back again";
    EXPECT_TRUE(settings.unpinDashboardFile(note));
    EXPECT_TRUE(settings.removeDashboardFolder(folder));
    settings.save();

    // The lists are the dashboard's; the files are the user's. Every one of the changes above is
    // now undone, and none of them had anything to do with the files themselves.
    EXPECT_TRUE(settings.getDashboardFolders().empty());
    EXPECT_TRUE(settings.getDashboardPinnedFiles().empty());

    EXPECT_TRUE(fs::exists(note)) << "a dashboard action never removes a user's file";
    EXPECT_TRUE(fs::exists(other)) << "nor one it was never told about";
    EXPECT_EQ(fs::last_write_time(note), noteTime) << "a dashboard action never rewrites a user's file";
    EXPECT_EQ(fs::last_write_time(other), otherTime);
    EXPECT_EQ(fs::file_size(note), noteSize);
    EXPECT_EQ(fs::last_write_time(folder), folderTime) << "nor the folder it is in";

    std::vector<std::string> entries;
    for (const fs::directory_entry& entry: fs::directory_iterator(folder)) {
        entries.push_back(entry.path().filename().string());
    }
    ASSERT_EQ(entries.size(), 2U) << "the folder holds what it held";
    std::sort(entries.begin(), entries.end());
    EXPECT_EQ(entries[0], "note.xopp");
    EXPECT_EQ(entries[1], "other.xopp");

    // A pinned path that is gone is kept as a path and creates nothing.
    const fs::path gone = folder / "gone.xopp";
    ASSERT_TRUE(settings.pinDashboardFile(gone));
    EXPECT_FALSE(fs::exists(gone)) << "pinning a path is not a way of making one";

    fs::remove_all(dir);
}

/*
 * Plan 007, steps 2 to 4: what the palette and the guidance remember about the user.
 *
 * The commands the user ran from the palette are kept in the profile so that the next palette shows
 * them, and the guidance and the tips record what the user has already been told so that nothing is
 * said twice. All of it is local to the profile: nothing here leaves the machine, and a fresh
 * profile starts with nothing remembered.
 */
TEST(SettingsTest, testRecentCommandsAreKeptMostRecentFirst) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_recentCommands");
    Settings settings{dir / "settings.xml"};
    EXPECT_TRUE(settings.getRecentCommands().empty()) << "a fresh profile has run no command";

    settings.addRecentCommand("win.undo");
    settings.addRecentCommand("win.export-as-pdf");
    EXPECT_EQ(settings.getRecentCommands(), (std::vector<std::string>{"win.export-as-pdf", "win.undo"}));

    // Running the same command again moves it to the front instead of listing it twice.
    settings.addRecentCommand("win.undo");
    EXPECT_EQ(settings.getRecentCommands(), (std::vector<std::string>{"win.undo", "win.export-as-pdf"}));

    Settings loaded{dir / "settings.xml"};
    loaded.load();
    EXPECT_EQ(loaded.getRecentCommands(), settings.getRecentCommands()) << "the list survives a restart";

    fs::remove_all(dir);
}

TEST(SettingsTest, testRecentCommandsStayBounded) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_recentCommandsBounded");
    Settings settings{dir / "settings.xml"};
    for (size_t i = 0; i < 40; i++) {
        settings.addRecentCommand("win.command-" + std::to_string(i));
    }
    EXPECT_LE(settings.getRecentCommands().size(), 10U) << "the palette shows a handful, not a history";
    EXPECT_EQ(settings.getRecentCommands().front(), "win.command-39");
    fs::remove_all(dir);
}

TEST(SettingsTest, testGuidanceAndTipsAreRememberedAndCanBeReplayed) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_interfaceTips");
    Settings settings{dir / "settings.xml"};
    EXPECT_FALSE(settings.hasSeenInterfaceGuidance()) << "a fresh profile has not seen the guidance";
    EXPECT_TRUE(settings.isInterfaceTipsEnabled()) << "tips are offered until the user turns them off";
    EXPECT_FALSE(settings.hasSeenTip("tool-properties"));

    settings.setInterfaceGuidanceSeen(true);
    settings.markTipSeen("tool-properties");
    settings.setInterfaceTipsEnabled(false);

    Settings loaded{dir / "settings.xml"};
    loaded.load();
    EXPECT_TRUE(loaded.hasSeenInterfaceGuidance());
    EXPECT_TRUE(loaded.hasSeenTip("tool-properties"));
    EXPECT_FALSE(loaded.isInterfaceTipsEnabled()) << "a tip the user turned off stays off";

    // "Show interface tips again" in the settings: everything is forgotten, so the guidance appears
    // once more and every tip may be shown again.
    loaded.resetInterfaceTips();
    EXPECT_FALSE(loaded.hasSeenInterfaceGuidance());
    EXPECT_TRUE(loaded.isInterfaceTipsEnabled());
    EXPECT_FALSE(loaded.hasSeenTip("tool-properties"));

    Settings replayed{dir / "settings.xml"};
    replayed.load();
    EXPECT_FALSE(replayed.hasSeenInterfaceGuidance()) << "the reset is what is persisted, not the history";

    fs::remove_all(dir);
}
