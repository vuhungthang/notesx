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
