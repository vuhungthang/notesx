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

#include <algorithm>  // for find_if
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/ToolPreset.h"
#include "control/settings/Settings.h"
#include "util/i18n.h"  // for _ (the built-in preset names are translatable)

/*
 * Plan 003, step 1: the preset model and its persistence.
 *
 * The model is tested on its own first - serialization, ordering, deletion, duplicate names -
 * because those rules are what the popover, the toolbar and the settings file all rely on. The
 * Settings tests then check that a real profile round trips and that an upgrade never replaces
 * what the user already had.
 */

namespace {

/// A pen preset that uses every field the pen supports.
auto fullPenPreset() -> ToolPreset {
    return ToolPreset{.id = "preset-1",
                      .name = "Black fine pen",
                      .toolType = TOOL_PEN,
                      .color = Colors::black,
                      .size = TOOL_SIZE_FINE,
                      .drawingType = DRAWING_TYPE_DEFAULT,
                      .fill = 128,
                      .favoriteOrder = 0};
}

/// Attributes as a settings file from a newer version might carry them.
auto attributesWithUnknownFields() -> std::map<std::string, std::string> {
    return {{"id", "preset-1"},           {"name", "Black fine pen"}, {"tool", "pen"},
            {"color", "ff000000"},        {"size", "thin"},           {"favorite", "0"},
            {"pressureCurve", "1.5,2.5"},  // not part of this version
            {"futureField", "whatever"}};
}

}  // namespace

TEST(ToolPresetTest, testRoundTripKeepsEveryApplicableField) {
    const ToolPreset original = fullPenPreset();

    const ToolPreset reloaded = ToolPreset::fromAttributes(original.toAttributes());

    EXPECT_EQ(reloaded, original);
}

TEST(ToolPresetTest, testRoundTripOfEveryToolSpecificField) {
    const ToolPreset eraser = ToolPreset{.id = "preset-2",
                                         .name = "Delete stroke eraser",
                                         .toolType = TOOL_ERASER,
                                         .size = TOOL_SIZE_THICK,
                                         .eraserType = ERASER_TYPE_DELETE_STROKE,
                                         .favoriteOrder = 1};

    EXPECT_EQ(ToolPreset::fromAttributes(eraser.toAttributes()), eraser);

    const ToolPreset highlighter = ToolPreset{.id = "preset-3",
                                              .name = "Yellow highlighter",
                                              .toolType = TOOL_HIGHLIGHTER,
                                              .color = Colors::yellow,
                                              .size = TOOL_SIZE_MEDIUM,
                                              .fill = 255,
                                              .builtin = true};

    EXPECT_EQ(ToolPreset::fromAttributes(highlighter.toAttributes()), highlighter);
}

TEST(ToolPresetTest, testUnknownFieldsAreIgnored) {
    const ToolPreset parsed = ToolPreset::fromAttributes(attributesWithUnknownFields());

    EXPECT_TRUE(parsed.isValid());
    EXPECT_EQ(parsed.id, "preset-1");
    EXPECT_EQ(parsed.name, "Black fine pen");
    EXPECT_EQ(parsed.toolType, TOOL_PEN);
    ASSERT_TRUE(parsed.color.has_value());
    EXPECT_EQ(*parsed.color, Colors::black);
    ASSERT_TRUE(parsed.size.has_value());
    EXPECT_EQ(*parsed.size, TOOL_SIZE_FINE);
    ASSERT_TRUE(parsed.favoriteOrder.has_value());
    EXPECT_EQ(*parsed.favoriteOrder, 0);

    // An unknown field is dropped rather than carried along: writing the preset back produces
    // only the fields this version knows about.
    const std::map<std::string, std::string> written = parsed.toAttributes();
    EXPECT_EQ(written.count("pressureCurve"), 0U);
    EXPECT_EQ(written.count("futureField"), 0U);
}

TEST(ToolPresetTest, testMissingOptionalFieldsAreAbsentNotDefaulted) {
    const ToolPreset parsed = ToolPreset::fromAttributes({{"id", "preset-1"}, {"name", "Eraser"}, {"tool", "eraser"}});

    EXPECT_TRUE(parsed.isValid());
    EXPECT_EQ(parsed.toolType, TOOL_ERASER);
    EXPECT_FALSE(parsed.color.has_value());
    EXPECT_FALSE(parsed.size.has_value());
    EXPECT_FALSE(parsed.drawingType.has_value());
    EXPECT_FALSE(parsed.fill.has_value());
    EXPECT_FALSE(parsed.eraserType.has_value());
    EXPECT_FALSE(parsed.favoriteOrder.has_value());
    EXPECT_FALSE(parsed.builtin);
}

TEST(ToolPresetTest, testUnrecognisedValuesAreTreatedAsAbsent) {
    // An unrecognised value must not silently become the enum's fallback: "octagon" is not a
    // drawing type, and turning it into DRAWING_TYPE_DEFAULT would be a silent downgrade.
    const ToolPreset parsed = ToolPreset::fromAttributes({{"id", "preset-1"},
                                                          {"name", "Odd"},
                                                          {"tool", "pen"},
                                                          {"size", "gigantic"},
                                                          {"drawingType", "octagon"},
                                                          {"eraserType", "laser"},
                                                          {"color", "not-a-colour"},
                                                          {"fill", "9001"},
                                                          {"favorite", "-3"}});

    EXPECT_TRUE(parsed.isValid());
    EXPECT_FALSE(parsed.size.has_value());
    EXPECT_FALSE(parsed.drawingType.has_value());
    EXPECT_FALSE(parsed.eraserType.has_value());
    EXPECT_FALSE(parsed.color.has_value());
    EXPECT_FALSE(parsed.fill.has_value());
    EXPECT_FALSE(parsed.favoriteOrder.has_value());
}

TEST(ToolPresetTest, testIncompletePresetsAreNotValid) {
    EXPECT_FALSE(ToolPreset{}.isValid());

    ToolPreset noName = fullPenPreset();
    noName.name.clear();
    EXPECT_FALSE(noName.isValid());

    ToolPreset noTool = fullPenPreset();
    noTool.toolType = TOOL_NONE;
    EXPECT_FALSE(noTool.isValid());

    // An id is not needed to apply a preset - a preset captured from the current configuration
    // has none yet - so it does not take part in this check.
    ToolPreset noId = fullPenPreset();
    noId.id.clear();
    EXPECT_TRUE(noId.isValid());
}

TEST(ToolPresetTest, testListKeepsInsertionOrder) {
    ToolPresetList list;
    const std::string first = list.add(ToolPreset{.name = "First", .toolType = TOOL_PEN});
    const std::string second = list.add(ToolPreset{.name = "Second", .toolType = TOOL_PEN});
    const std::string third = list.add(ToolPreset{.name = "Third", .toolType = TOOL_PEN});

    ASSERT_EQ(list.getPresets().size(), 3U);
    EXPECT_EQ(list.getPresets()[0].id, first);
    EXPECT_EQ(list.getPresets()[1].id, second);
    EXPECT_EQ(list.getPresets()[2].id, third);
    EXPECT_EQ(list.getPresets()[0].name, "First");
    EXPECT_EQ(list.getPresets()[2].name, "Third");
}

TEST(ToolPresetTest, testDeletionRemovesOnlyTheNamedPreset) {
    ToolPresetList list;
    const std::string first = list.add(ToolPreset{.name = "First", .toolType = TOOL_PEN});
    const std::string second = list.add(ToolPreset{.name = "Second", .toolType = TOOL_PEN});

    EXPECT_TRUE(list.remove(first));
    EXPECT_EQ(list.getPresets().size(), 1U);
    EXPECT_EQ(list.findById(first), nullptr);
    ASSERT_NE(list.findById(second), nullptr);
    EXPECT_EQ(list.findById(second)->name, "Second");

    // Removing something that is not there is not an error, it just does nothing.
    EXPECT_FALSE(list.remove(first));
    EXPECT_EQ(list.getPresets().size(), 1U);
}

TEST(ToolPresetTest, testDuplicateNamesAreMadeUnique) {
    ToolPresetList list;
    const std::string first = list.add(ToolPreset{.name = "Black fine pen", .toolType = TOOL_PEN});
    const std::string second = list.add(ToolPreset{.name = "Black fine pen", .toolType = TOOL_PEN});
    const std::string third = list.add(ToolPreset{.name = "Black fine pen", .toolType = TOOL_PEN});

    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    ASSERT_NE(list.findById(first), nullptr);
    ASSERT_NE(list.findById(second), nullptr);
    ASSERT_NE(list.findById(third), nullptr);
    EXPECT_EQ(list.findById(first)->name, "Black fine pen");
    EXPECT_EQ(list.findById(second)->name, "Black fine pen (2)");
    EXPECT_EQ(list.findById(third)->name, "Black fine pen (3)");
}

TEST(ToolPresetTest, testRenameKeepsNamesUniqueAndRejectsAnEmptyName) {
    ToolPresetList list;
    const std::string first = list.add(ToolPreset{.name = "First", .toolType = TOOL_PEN});
    const std::string second = list.add(ToolPreset{.name = "Second", .toolType = TOOL_PEN});

    EXPECT_TRUE(list.rename(second, "First"));
    EXPECT_EQ(list.findById(second)->name, "First (2)");
    EXPECT_EQ(list.findById(first)->name, "First");

    // Renaming to the name a preset already has is not a collision with itself.
    EXPECT_TRUE(list.rename(first, "First"));
    EXPECT_EQ(list.findById(first)->name, "First");

    EXPECT_FALSE(list.rename(first, ""));
    EXPECT_EQ(list.findById(first)->name, "First");
    EXPECT_FALSE(list.rename("preset-does-not-exist", "Whatever"));
}

TEST(ToolPresetTest, testFavoritesKeepTheirOrderAndAreLimited) {
    ToolPresetList list;
    const std::string first = list.add(ToolPreset{.name = "First", .toolType = TOOL_PEN});
    const std::string second = list.add(ToolPreset{.name = "Second", .toolType = TOOL_PEN});
    const std::string third = list.add(ToolPreset{.name = "Third", .toolType = TOOL_PEN});

    EXPECT_TRUE(list.setFavorite(first, true));
    EXPECT_TRUE(list.setFavorite(second, true));
    EXPECT_TRUE(list.setFavorite(third, true));

    std::vector<const ToolPreset*> favorites = list.getFavorites();
    ASSERT_EQ(favorites.size(), 3U);
    EXPECT_EQ(favorites[0]->id, first);
    EXPECT_EQ(favorites[1]->id, second);
    EXPECT_EQ(favorites[2]->id, third);

    // Reordering moves one entry and leaves the relative order of the rest alone.
    EXPECT_TRUE(list.moveFavorite(third, 0));
    favorites = list.getFavorites();
    ASSERT_EQ(favorites.size(), 3U);
    EXPECT_EQ(favorites[0]->id, third);
    EXPECT_EQ(favorites[1]->id, first);
    EXPECT_EQ(favorites[2]->id, second);

    EXPECT_TRUE(list.setFavorite(first, false));
    favorites = list.getFavorites();
    ASSERT_EQ(favorites.size(), 2U);
    EXPECT_EQ(favorites[0]->id, third);
    EXPECT_EQ(favorites[1]->id, second);
    EXPECT_FALSE(list.isFavorite(first));
    EXPECT_TRUE(list.isFavorite(second));

    // A preset that is not a favourite cannot be reordered.
    EXPECT_FALSE(list.moveFavorite(first, 0));
}

TEST(ToolPresetTest, testNoMoreFavoritesThanFocusCanShow) {
    ToolPresetList list;
    for (std::size_t i = 0; i < ToolPresetList::MAX_FAVORITES; i++) {
        const std::string id = list.add(ToolPreset{.name = "Preset " + std::to_string(i), .toolType = TOOL_PEN});
        EXPECT_TRUE(list.setFavorite(id, true)) << "favourite " << i << " was rejected";
    }

    const std::string extra = list.add(ToolPreset{.name = "One too many", .toolType = TOOL_PEN});
    EXPECT_FALSE(list.setFavorite(extra, true));
    EXPECT_FALSE(list.isFavorite(extra));
    EXPECT_EQ(list.getFavorites().size(), ToolPresetList::MAX_FAVORITES);

    // Removing a favourite frees the slot again.
    EXPECT_TRUE(list.setFavorite(list.getFavorites().front()->id, false));
    EXPECT_TRUE(list.setFavorite(extra, true));
}

TEST(ToolPresetTest, testSeededListCarriesTheBuiltInExamples) {
    const ToolPresetList list = ToolPresetList::seedDefaults();

    EXPECT_FALSE(list.empty());
    for (const ToolPreset& preset: list.getPresets()) {
        EXPECT_TRUE(preset.builtin);
        EXPECT_TRUE(preset.isValid());
    }

    // Focus shows three to five favourites directly, so the seeded list must not leave the
    // favourite strip empty.
    EXPECT_FALSE(list.getFavorites().empty());
    EXPECT_LE(list.getFavorites().size(), ToolPresetList::MAX_FAVORITES);
}

TEST(ToolPresetTest, testRestoreBuiltinsKeepsCustomAndEditedPresets) {
    ToolPresetList list;
    const std::string custom = list.add(ToolPreset{.name = "My own pen", .toolType = TOOL_PEN});

    list.restoreBuiltins();
    const std::size_t afterFirstRestore = list.getPresets().size();
    EXPECT_GT(afterFirstRestore, 1U);
    ASSERT_NE(list.findById(custom), nullptr);
    EXPECT_EQ(list.findById(custom)->name, "My own pen");

    // A second restore adds nothing: the examples are already there.
    list.restoreBuiltins();
    EXPECT_EQ(list.getPresets().size(), afterFirstRestore);

    // An example the user renamed stays renamed; an example the user removed comes back.
    const ToolPreset* renamed = nullptr;
    const ToolPreset* removed = nullptr;
    for (const ToolPreset& preset: list.getPresets()) {
        if (!preset.builtin) {
            continue;
        }
        if (renamed == nullptr) {
            renamed = &preset;
        }
        removed = &preset;
    }
    ASSERT_NE(renamed, nullptr);
    ASSERT_NE(removed, nullptr);
    const std::string renamedId = renamed->id;
    const std::string removedId = removed->id;
    const std::string removedName = removed->name;
    ASSERT_NE(renamedId, removedId);

    ASSERT_TRUE(list.rename(renamedId, "Renamed example"));
    ASSERT_TRUE(list.remove(removedId));

    list.restoreBuiltins();
    ASSERT_NE(list.findById(renamedId), nullptr);
    EXPECT_EQ(list.findById(renamedId)->name, "Renamed example");
    ASSERT_NE(list.findById(removedId), nullptr) << "the removed example was not restored";
    EXPECT_EQ(list.findById(removedId)->name, removedName);
    EXPECT_EQ(list.findByName(removedName), list.findById(removedId));
}

TEST(ToolPresetTest, testFromStoredDropsIncompleteAndRepeatedEntries) {
    const std::vector<ToolPreset> stored{fullPenPreset(),
                                         ToolPreset{.id = "preset-1", .name = "Duplicate id", .toolType = TOOL_PEN},
                                         ToolPreset{.id = "preset-2", .name = "", .toolType = TOOL_PEN},
                                         ToolPreset{.id = "preset-3", .name = "No tool", .toolType = TOOL_NONE},
                                         ToolPreset{.name = "No id", .toolType = TOOL_PEN},
                                         ToolPreset{.id = "preset-4", .name = "Eraser", .toolType = TOOL_ERASER}};

    const ToolPresetList list = ToolPresetList::fromStored(stored);

    ASSERT_EQ(list.getPresets().size(), 2U);
    EXPECT_EQ(list.getPresets()[0].name, "Black fine pen");
    EXPECT_EQ(list.getPresets()[1].name, "Eraser");
}

/*
 * Plan 003: the settings file.
 */

namespace {

/** Write a minimal settings file containing only the given elements. */
void writeSettingsFile(const fs::path& path, const std::string& body) {
    std::ofstream out(path);
    out << "<?xml version=\"1.0\"?>\n<settings>\n" << body << "</settings>\n";
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

TEST(ToolPresetSettingsTest, testFreshProfileIsSeededWithTheExamples) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsFresh");
    const fs::path file = dir / "settings.xml";

    Settings settings{file};
    settings.load();

    EXPECT_FALSE(settings.getToolPresets().empty());
    EXPECT_EQ(settings.getToolPresets(), ToolPresetList::seedDefaults());
    EXPECT_EQ(settings.getFavoritePresetCount(), static_cast<int>(ToolPresetList::MAX_FAVORITES));

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getToolPresets(), ToolPresetList::seedDefaults());

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testPresetsRoundTripThroughTheSettingsFile) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsRoundTrip");
    const fs::path file = dir / "settings.xml";

    std::string customId;
    {
        Settings settings{file};
        settings.load();

        ToolPresetList presets = settings.getToolPresets();
        customId = presets.add(ToolPreset{.name = "My teal pen",
                                          .toolType = TOOL_PEN,
                                          .color = Color(0xff008080U),
                                          .size = TOOL_SIZE_THICK,
                                          .drawingType = DRAWING_TYPE_RECTANGLE,
                                          .fill = 42});
        ASSERT_TRUE(presets.rename(customId, "Teal rectangle pen"));
        ASSERT_TRUE(presets.setFavorite(customId, true));
        ASSERT_TRUE(presets.moveFavorite(customId, 0));
        settings.setToolPresets(std::move(presets));
        settings.setFavoritePresetCount(3);
    }

    Settings reloaded{file};
    reloaded.load();

    ASSERT_EQ(reloaded.getToolPresets().getPresets().size(), ToolPresetList::seedDefaults().getPresets().size() + 1);
    const ToolPreset* stored = reloaded.getToolPresets().findById(customId);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->name, "Teal rectangle pen");
    EXPECT_EQ(stored->toolType, TOOL_PEN);
    ASSERT_TRUE(stored->color.has_value());
    EXPECT_EQ(*stored->color, Color(0xff008080U));
    ASSERT_TRUE(stored->size.has_value());
    EXPECT_EQ(*stored->size, TOOL_SIZE_THICK);
    ASSERT_TRUE(stored->drawingType.has_value());
    EXPECT_EQ(*stored->drawingType, DRAWING_TYPE_RECTANGLE);
    ASSERT_TRUE(stored->fill.has_value());
    EXPECT_EQ(*stored->fill, 42);
    EXPECT_FALSE(stored->builtin);

    // The favourite position the user chose survives the restart.
    const std::vector<const ToolPreset*> favorites = reloaded.getToolPresets().getFavorites();
    ASSERT_FALSE(favorites.empty());
    EXPECT_EQ(favorites.front()->id, customId);
    EXPECT_EQ(reloaded.getFavoritePresetCount(), 3);

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testUpgradeKeepsThePresetsTheProfileAlreadyHad) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsUpgrade");
    const fs::path file = dir / "settings.xml";

    // A profile from before Plan 003: a settings file without any <toolPresets> element.
    writeSettingsFile(file, "<property name=\"selectedToolbar\" value=\"Minimal Left\"/>\n"
                            "<property name=\"workspaceMode\" value=\"classic\"/>\n"
                            "<property name=\"favoritePresetCount\" value=\"2\"/>\n");

    Settings settings{file};
    settings.load();

    // The examples are available, and nothing the profile had was replaced.
    EXPECT_FALSE(settings.getToolPresets().empty());
    EXPECT_EQ(settings.getToolPresets(), ToolPresetList::seedDefaults());
    EXPECT_EQ(settings.getFavoritePresetCount(), 2);

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testAnEmptyPresetListStaysEmpty) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsEmpty");
    const fs::path file = dir / "settings.xml";

    {
        Settings settings{file};
        settings.load();
        settings.setToolPresets(ToolPresetList{});
        EXPECT_TRUE(settings.getToolPresets().empty());
    }

    // Deleting every preset must not be undone by re-seeding on the next start.
    Settings reloaded{file};
    reloaded.load();
    EXPECT_TRUE(reloaded.getToolPresets().empty());

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testUnreadableEntriesAreSkippedWithoutLosingTheRest) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsPartial");
    const fs::path file = dir / "settings.xml";

    writeSettingsFile(file, "<toolPresets version=\"1\">\n"
                            "  <preset id=\"preset-1\" name=\"Kept\" tool=\"pen\" color=\"ff000000\" size=\"thin\"/>\n"
                            "  <preset id=\"preset-2\" name=\"No tool at all\"/>\n"
                            "  <preset id=\"preset-3\" name=\"Unknown tool\" tool=\"teleporter\"/>\n"
                            "  <preset id=\"preset-4\" name=\"Eraser\" tool=\"eraser\" eraserType=\"whiteout\"/>\n"
                            "</toolPresets>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());

    ASSERT_EQ(settings.getToolPresets().getPresets().size(), 2U);
    EXPECT_EQ(settings.getToolPresets().getPresets()[0].name, "Kept");
    EXPECT_EQ(settings.getToolPresets().getPresets()[1].name, "Eraser");
    EXPECT_FALSE(settings.getToolPresets().empty());

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testANewerStorageVersionIsNotDowngraded) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsNewerVersion");
    const fs::path file = dir / "settings.xml";

    writeSettingsFile(file, "<toolPresets version=\"99\">\n"
                            "  <preset id=\"preset-1\" name=\"From the future\" tool=\"pen\"/>\n"
                            "</toolPresets>\n");

    Settings settings{file};
    ASSERT_NO_THROW(settings.load());

    // The unknown format is left alone; the profile keeps the examples instead of half-reading
    // a file this version does not understand.
    EXPECT_EQ(settings.getToolPresets(), ToolPresetList::seedDefaults());

    fs::remove_all(dir);
}

TEST(ToolPresetSettingsTest, testFavoriteCountIsClampedAndPersisted) {
    const fs::path dir = freshSettingsDir("xournalpp-test-units_presetsFavoriteCount");
    const fs::path file = dir / "settings.xml";

    Settings settings{file};
    settings.load();

    settings.setFavoritePresetCount(-4);
    EXPECT_EQ(settings.getFavoritePresetCount(), 0);

    settings.setFavoritePresetCount(99);
    EXPECT_EQ(settings.getFavoritePresetCount(), static_cast<int>(ToolPresetList::MAX_FAVORITES));

    settings.setFavoritePresetCount(3);

    Settings reloaded{file};
    reloaded.load();
    EXPECT_EQ(reloaded.getFavoritePresetCount(), 3);

    fs::remove_all(dir);
}
